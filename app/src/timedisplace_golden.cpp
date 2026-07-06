// timedisplace_golden — --selftest-timedisplace. Proves the NEW tested surface of TimeDisplace: the
// per-pixel slice-select SHADER off the persistent N-slice ring (timedisplace.metal, ported from
// TimeDisplace.hlsl:43-47). The ring-write machinery is already proven by keepintexturearray_golden; here
// the tooth is the DISPLACEMENT math:  slice = (SliceIndex + round(brightness*DisplaceAmount)) % N.
//
// SCENARIO (closed-form, independent-of-impl; flat + resident = R-2). N=4 ring. Cook 4 setup frames
// feeding distinct solid colors so the ring holds [C0,C1,C2,C3] at slices [0,1,2,3] (the op writes slice
// (head mod N), head advancing 0,1,2,3). Then TWO probe cooks with a SOLID DisplaceMap + a pinned
// SliceIndex, asserting the output = the AUTHORED ring slice the formula selects:
//   Probe A: DisplaceMap brightness=0, Displacement=0 → sliceOffset=0 → slice=SliceIndex → out=C[SliceIndex].
//            (identity in time — the head frame.)
//   Probe B: DisplaceMap brightness=1.0 (white), Displacement=2 → sliceOffset=round(1*2)=2 →
//            slice=(SliceIndex+2)%4 → out=C[(SliceIndex+2)%4]. (reaches 2 frames back — the diverging
//            middle: a NON-zero offset, the whole point of "time" displacement.)
// SliceIndex is pinned so the probe's OWN write (which advances the ring) does not confuse the read: we
// pin SliceIndex to a slice we did NOT overwrite this cook, and choose C values so the assert is exact.
//
// Expected values are the colors WE authored into the ring (never read from the op → P5-safe). The Probe B
// offset (2) lands OFF the endpoint (P2-safe: a wrong DisplaceAmount scale / dropped offset → the assert
// diverges). injectBug forces DisplaceAmount→0, so Probe B reads C[SliceIndex] (offset 0) ≠ the +2 slice.
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/point_graph.h"          // PointGraph::cook / cookResident / debug accessors
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph

namespace sw {

bool& timeDisplaceInjectBug();  // point_ops_timedisplace.cpp

namespace {

bool readTexel00(MTL::Texture* tex, uint8_t out[4]) {
  if (!tex) return false;
  uint32_t w = (uint32_t)tex->width(), h = (uint32_t)tex->height();
  if (w == 0 || h == 0) return false;
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  out[0] = px[0]; out[1] = px[1]; out[2] = px[2]; out[3] = px[3];
  return true;
}

bool texelNear(const uint8_t got[4], const uint8_t want[4], int tol = 2) {
  for (int i = 0; i < 4; ++i) {
    int d = (int)got[i] - (int)want[i];
    if (d < 0) d = -d;
    if (d > tol) return false;
  }
  return true;
}

const uint8_t kColors[4][4] = {
    {200, 10, 10, 255},   // C0
    {10, 200, 10, 255},   // C1
    {10, 10, 200, 255},   // C2
    {200, 200, 10, 255},  // C3
};
const float kClears[4][3] = {
    {200 / 255.0f, 10 / 255.0f, 10 / 255.0f},
    {10 / 255.0f, 200 / 255.0f, 10 / 255.0f},
    {10 / 255.0f, 10 / 255.0f, 200 / 255.0f},
    {200 / 255.0f, 200 / 255.0f, 10 / 255.0f},
};

// node 1 = Image source RenderTarget; node 2 = DisplaceMap source RenderTarget; node 10 = TimeDisplace.
// Image (port idx 0) <- node1 out; DisplaceMap (port idx 1) <- node2 out. ArraySize=4.
void buildGraph(Graph& g) {
  Node img; img.id = 1; img.type = "RenderTarget";
  img.params["Resolution"] = 4.0f; img.params["CustomW"] = 4.0f; img.params["CustomH"] = 4.0f;
  img.params["ClearColor.w"] = 1.0f;
  g.nodes.push_back(img);

  Node dm; dm.id = 2; dm.type = "RenderTarget";
  dm.params["Resolution"] = 4.0f; dm.params["CustomW"] = 4.0f; dm.params["CustomH"] = 4.0f;
  dm.params["ClearColor.w"] = 1.0f;
  g.nodes.push_back(dm);

  Node td; td.id = 10; td.type = "TimeDisplace";
  td.params["ArraySize"] = 4.0f;
  td.params["Displacement"] = 0.0f;
  td.params["SliceIndex"] = 0.0f;
  g.nodes.push_back(td);

  g.connections.push_back({500, pinId(1, /*out*/ 1), pinId(10, /*Image*/ 0)});
  g.connections.push_back({501, pinId(2, /*out*/ 1), pinId(10, /*DisplaceMap*/ 1)});
}

void setClear(Graph& g, int nodeId, float r, float gg, float b) {
  Node* n = g.node(nodeId);
  n->params["ClearColor.x"] = r; n->params["ClearColor.y"] = gg; n->params["ClearColor.z"] = b;
  n->params["ClearColor.w"] = 1.0f;
}

}  // namespace

int runTimeDisplaceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();

  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-timedisplace] FAIL: no metallib\n");
    dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  MTL::CommandQueue* q = dev->newCommandQueue();

  EvaluationContext ctx{};
  ctx.deltaTime = 1.0f / 60.0f;
  timeDisplaceInjectBug() = injectBug;
  bool ok = true;

  // Run the full scenario on one PointGraph (the write head advances across cooks — the whole point). We
  // set DisplaceMap black + Displacement 0 during setup so the setup cooks read the head (irrelevant) and
  // never displace; the assert only fires on the two probes.
  auto runScenario = [&](bool resident, const char* tag) {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    buildGraph(g);

    auto cook = [&](int frame) {
      ctx.frameIndex = frame;
      if (resident) {
        SymbolLibrary l = libFromGraph(g);
        ResidentEvalGraph rg = buildEvalGraph(l, "Root");
        pg.cookResident(rg, ctx, nullptr, /*targetPath=*/"10");
      } else {
        pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
      }
    };

    // SETUP: 4 cooks fill ring slices 0..3 with C0..C3 (write head = frame index). DisplaceMap black,
    // Displacement 0 → no displacement, SliceIndex irrelevant (we don't assert the setup reads).
    setClear(g, 2, 0, 0, 0);  // DisplaceMap = black (brightness 0)
    g.node(10)->params["Displacement"] = 0.0f;
    for (int f = 0; f < 4; ++f) {
      setClear(g, 1, kClears[f][0], kClears[f][1], kClears[f][2]);  // Image = C_f
      g.node(10)->params["SliceIndex"] = (float)f;
      cook(f);
    }
    // Ring now = [C0,C1,C2,C3]. The next cook (frame 4) will write slice 4%4=0 (overwrites C0).

    // PROBE A: DisplaceMap black (brightness 0) + Displacement 0 → sliceOffset 0 → out = ring[SliceIndex].
    // The probe writes Image=C0-again into slice 0 (head=4→0), so ring[0] stays C0. Pin SliceIndex=1 →
    // out must be C1 (a slice we did NOT overwrite). Reads the AUTHORED history frame with zero offset.
    setClear(g, 1, kClears[0][0], kClears[0][1], kClears[0][2]);  // Image = C0 (rewrites slice 0 = C0)
    setClear(g, 2, 0, 0, 0);                                      // DisplaceMap black
    g.node(10)->params["Displacement"] = 0.0f;
    g.node(10)->params["SliceIndex"] = 1.0f;  // read slice 1 = C1
    cook(4);
    {
      MTL::Texture* out = pg.debugCookedFeedbackOutput(10, /*ordinal=*/0, resident);
      uint8_t got[4];
      bool okr = readTexel00(out, got);
      if (!okr || !texelNear(got, kColors[1])) {
        std::printf("[selftest-timedisplace] %s ProbeA(offset0,SliceIndex=1) got=(%d,%d,%d,%d) "
                    "want C1=(%d,%d,%d,%d) FAIL\n", tag, okr ? got[0] : -1, okr ? got[1] : -1,
                    okr ? got[2] : -1, okr ? got[3] : -1, kColors[1][0], kColors[1][1], kColors[1][2],
                    kColors[1][3]);
        ok = false;
      }
    }

    // PROBE B (the diverging tooth): DisplaceMap WHITE (brightness 1) + Displacement 2 → sliceOffset=
    // round(1*2)=2 → slice=(SliceIndex+2)%4. The probe writes Image=C1 into slice 5%4=1 (rewrites C1),
    // so ring[1] stays C1. Pin SliceIndex=1 → slice=(1+2)%4=3 → out must be C3 (a slice NOT overwritten).
    // A NON-zero offset reaching a back-frame — the essence of time displacement.
    setClear(g, 1, kClears[1][0], kClears[1][1], kClears[1][2]);  // Image = C1 (rewrites slice 1 = C1)
    setClear(g, 2, 1, 1, 1);                                      // DisplaceMap WHITE (brightness 1)
    g.node(10)->params["Displacement"] = 2.0f;
    g.node(10)->params["SliceIndex"] = 1.0f;  // slice = (1+2)%4 = 3 → C3
    cook(5);
    {
      MTL::Texture* out = pg.debugCookedFeedbackOutput(10, /*ordinal=*/0, resident);
      uint8_t got[4];
      bool okr = readTexel00(out, got);
      // GOOD: out = C3 (offset 2 reached). BUG (DisplaceAmount forced 0): offset 0 → out = C[SliceIndex] =
      // ring[1] = C1 ≠ C3 → diverge.
      if (!okr || !texelNear(got, kColors[3])) {
        std::printf("[selftest-timedisplace] %s ProbeB(offset2,SliceIndex=1) got=(%d,%d,%d,%d) "
                    "want C3=(%d,%d,%d,%d) FAIL\n", tag, okr ? got[0] : -1, okr ? got[1] : -1,
                    okr ? got[2] : -1, okr ? got[3] : -1, kColors[3][0], kColors[3][1], kColors[3][2],
                    kColors[3][3]);
        ok = false;
      }
    }
  };

  runScenario(/*resident=*/false, "flat");
  if (!injectBug) runScenario(/*resident=*/true, "resident");

  timeDisplaceInjectBug() = false;
  if (!injectBug && ok)
    std::printf("[selftest-timedisplace] flat+resident: DisplaceMap brightness → sliceOffset → correct "
                "history slice\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-timedisplace] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
