// keepintexturearray_golden — --selftest-keepintexturearray. The CROSS-FRAME N-SLICE RING golden for
// the persistent-accumulator seam's first leaf (the first feedback op needing > 2 slices). Run on BOTH
// the flat cook AND the resident (PRODUCTION) cookResident — R-2 iron rule (only-flat = a production
// black hole).
//
// SCENARIO (closed-form, independent-of-impl — the expected slice content is authored by US, not read
// back from the op's own helper): N=4 ring. Cook 4 frames on the SAME PointGraph (so the ring survives).
// Each frame writes a DISTINCT solid color into a DISTINCT slice via WriteIndex = frameIndex:
//   frame 0: WriteIndex=0, color C0=(255,0,0)   → ring slice 0 = C0
//   frame 1: WriteIndex=1, color C1=(0,255,0)   → ring slice 1 = C1
//   frame 2: WriteIndex=2, color C2=(0,0,255)   → ring slice 2 = C2
//   frame 3: WriteIndex=3, color C3=(255,255,0) → ring slice 3 = C3
// TriggerWrite=TRUE every frame. On frame 3 we read EACH ReadIndex 0..3 (re-cooking with the SAME ring,
// TriggerWrite still writes slice 3 = C3 again — idempotent) and assert SelectedSlice == C[ReadIndex].
// The load-bearing tooth: ReadIndex reaches the AUTHORED history frame (KeepInTextureArray.cs:43,120-126).
//
// A closed-form oracle (GOLDEN_STANDARD 特徵1): C[ri] is the color WE cleared into slice ri — never read
// from the op. The probe is at the DIVERGING middle (ReadIndex 1 and 2, NOT just 0 — reading slice ≠ the
// last-written one is exactly what "history buffer" means; 特徵2). injectBug forces readSlice=0, so
// ReadIndex 1/2/3 all return C0 ≠ C1/C2/C3 → the tooth diverges (特徵3, real cook-path corruption).
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/point_graph.h"          // PointGraph::cook / cookResident / debug accessors
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph

namespace sw {

bool& keepInTextureArrayInjectBug();  // point_ops_keepintexturearray.cpp

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

// The 4 authored slice colors (opaque; 8-bit unorm of the float clears, no sRGB on RGBA8Unorm).
const uint8_t kColors[4][4] = {
    {255, 0, 0, 255},    // C0
    {0, 255, 0, 255},    // C1
    {0, 0, 255, 255},    // C2
    {255, 255, 0, 255},  // C3
};
const float kClears[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}};

// node 1 = RenderTarget (solid ClearColor, empty Command chain = just the clear); node 10 =
// KeepInTextureArray. SourceTexture (port idx 0) <- RenderTarget out (port idx 1). ArraySize=4.
void buildGraph(Graph& g) {
  Node rt; rt.id = 1; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = 4.0f;
  rt.params["CustomH"] = 4.0f;
  rt.params["ClearColor.w"] = 1.0f;
  g.nodes.push_back(rt);

  Node kt; kt.id = 10; kt.type = "KeepInTextureArray";
  kt.params["ArraySize"] = 4.0f;
  kt.params["TriggerWrite"] = 1.0f;  // write every frame
  kt.params["WriteIndex"] = 0.0f;
  kt.params["ReadIndex"] = 0.0f;
  g.nodes.push_back(kt);

  // RenderTarget out (port idx 1) -> KeepInTextureArray SourceTexture (port idx 0).
  g.connections.push_back({500, pinId(1, /*out*/ 1), pinId(10, /*SourceTexture*/ 0)});
}

void setClear(Graph& g, const float rgb[3]) {
  Node* rt = g.node(1);
  rt->params["ClearColor.x"] = rgb[0];
  rt->params["ClearColor.y"] = rgb[1];
  rt->params["ClearColor.z"] = rgb[2];
  rt->params["ClearColor.w"] = 1.0f;
}

}  // namespace

int runKeepInTextureArraySelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();

  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-keepintexturearray] FAIL: no metallib\n");
    dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  MTL::CommandQueue* q = dev->newCommandQueue();

  EvaluationContext ctx{};
  ctx.deltaTime = 1.0f / 60.0f;

  keepInTextureArrayInjectBug() = injectBug;
  bool ok = true;

  // Write frames 0..2 (fill slices 0,1,2). Slice 3 is written on the read frame below.
  auto writeFrame = [&](PointGraph& pg, Graph& g, int frame, bool resident) {
    setClear(g, kClears[frame]);
    g.node(10)->params["WriteIndex"] = (float)frame;
    g.node(10)->params["ReadIndex"] = (float)frame;  // read = the just-written (not asserted here)
    ctx.frameIndex = frame;
    if (resident) {
      SymbolLibrary l = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(l, "Root");
      pg.cookResident(rg, ctx, nullptr, /*targetPath=*/"10");
    } else {
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
    }
  };

  // Read frame: WriteIndex=3 (fill slice 3 = C3), then for each ReadIndex 0..3 re-cook and assert
  // SelectedSlice == C[ReadIndex]. The ring already holds slices 0..2 from writeFrame; this frame adds 3.
  auto readAndAssert = [&](PointGraph& pg, Graph& g, bool resident, const char* tag) {
    // First, write slice 3.
    setClear(g, kClears[3]);
    g.node(10)->params["WriteIndex"] = 3.0f;
    for (int ri = 0; ri < 4; ++ri) {
      g.node(10)->params["ReadIndex"] = (float)ri;
      ctx.frameIndex = 3 + ri;
      if (resident) {
        SymbolLibrary l = libFromGraph(g);
        ResidentEvalGraph rg = buildEvalGraph(l, "Root");
        pg.cookResident(rg, ctx, nullptr, /*targetPath=*/"10");
      } else {
        pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
      }
      MTL::Texture* sel = pg.debugCookedFeedbackOutput(10, /*ordinal=*/0, resident);  // SelectedSlice
      uint8_t got[4];
      bool okr = readTexel00(sel, got);
      // GOOD: SelectedSlice(ReadIndex=ri) == the authored C[ri]. BUG: readSlice forced 0 → ri=1/2/3
      // return C0 ≠ C[ri] → diverge. (ri=0 returns C0 in both paths — that's the non-diverging endpoint;
      // the diverging middle is ri=1/2/3.)
      if (!okr || !texelNear(got, kColors[ri])) {
        std::printf("[selftest-keepintexturearray] %s ReadIndex=%d SelectedSlice got=(%d,%d,%d,%d) "
                    "want=(%d,%d,%d,%d) FAIL\n",
                    tag, ri, okr ? got[0] : -1, okr ? got[1] : -1, okr ? got[2] : -1, okr ? got[3] : -1,
                    kColors[ri][0], kColors[ri][1], kColors[ri][2], kColors[ri][3]);
        ok = false;
      }
    }
  };

  // ---------------- FLAT cook ----------------
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    buildGraph(g);
    writeFrame(pg, g, 0, /*resident=*/false);
    writeFrame(pg, g, 1, /*resident=*/false);
    writeFrame(pg, g, 2, /*resident=*/false);
    readAndAssert(pg, g, /*resident=*/false, "flat");
  }

  // ---------------- RESIDENT (production) cook ----------------
  // The cross-frame ring must be LIVE on cookResident (R-2), not a flat-only path. Skipped in -bug mode
  // (the flat tooth already bit; the injection is in the shared leaf, so flat exercised it).
  if (!injectBug) {
    PointGraph pg2(dev, lib, q, 64, 64);
    Graph g2;
    buildGraph(g2);
    writeFrame(pg2, g2, 0, /*resident=*/true);
    writeFrame(pg2, g2, 1, /*resident=*/true);
    writeFrame(pg2, g2, 2, /*resident=*/true);
    readAndAssert(pg2, g2, /*resident=*/true, "resident");
  }

  keepInTextureArrayInjectBug() = false;
  if (!injectBug && ok)
    std::printf("[selftest-keepintexturearray] flat+resident N=4 ring: ReadIndex reaches authored history "
                "slice\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-keepintexturearray] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
