// keeppreviousframe_golden — --selftest-keeppreviousframe. The CROSS-FRAME (ping-pong) golden for the
// feedback seam's first leaf. Two-frame scenario, run on BOTH the flat cook AND the resident
// (PRODUCTION) cook (cookResident) — R-2 iron rule (only-flat = a production black hole).
//
// Scenario (the load-bearing cross-frame proof):
//   A RenderTarget (solid ClearColor, an empty Command chain → just the pass clear) feeds ImageA of a
//   KeepPreviousFrame node. We cook TWICE on the SAME PointGraph (so the node's persistent pair +
//   toggle survive between cooks, which is the whole point):
//     frame N   : ClearColor = SENTINEL_A → cook → KeepPreviousFrame's CurrentFrame == SENTINEL_A.
//     frame N+1 : ClearColor = SENTINEL_B → cook → KeepPreviousFrame's PreviousFrame == SENTINEL_A
//                 (frame N's image, copied into the OTHER buffer last frame) AND CurrentFrame == B.
//   The PreviousFrame==A assert at frame N+1 is the cross-frame tooth: it can only pass if the toggle
//   flipped and the copy landed in the right buffer (KeepPreviousFrame.cs:56-68).
//
// injectBug (keepPreviousFrameInjectBug): SUPPRESSES the toggle flip, so frame N+1's PreviousFrame
// reads the buffer just overwritten with B (== B, not A) → the cross-frame assert diverges → exit 1.
//
// We read KeepPreviousFrame's outputs by cooking with the KeepPreviousFrame node as the cook TARGET and
// reading pg.target() (= displayTex): the cook-entry shows the node's FIRST Texture2D output
// (PreviousFrame, ordinal 0). To read CurrentFrame we wire a separate DrawScreenQuad? No — simpler: we
// read PreviousFrame via the terminal display, and CurrentFrame via a second cook whose target wire
// pulls CurrentFrame through a passthrough. To keep the golden self-contained we instead read BOTH
// outputs directly from the cooked feedback pair through debug accessors below.
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/point_graph.h"          // PointGraph::cook / cookResident / target() / debug accessors
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph

namespace sw {

bool& keepPreviousFrameInjectBug();  // point_ops_keeppreviousframe.cpp

namespace {

// Read texel(0,0) of an RGBA8Unorm texture as 4 bytes. The RenderTarget clear produces a SOLID color,
// so any texel suffices; we read (0,0). Returns false if the texture is null/wrong-format.
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

// Build the flat graph: node 1 = RenderTarget (small fixed Custom resolution, solid ClearColor → the
// only chain content is the clear, so its output is a solid color); node 10 = KeepPreviousFrame, ImageA
// (port idx 0) wired from RenderTarget's out (port idx 1). Keep defaults to TRUE.
void buildFeedbackGraph(Graph& g) {
  Node rt; rt.id = 1; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = 4.0f;
  rt.params["CustomH"] = 4.0f;
  rt.params["ClearColor.w"] = 1.0f;  // opaque (set per-frame R/G/B below)
  g.nodes.push_back(rt);

  Node kp; kp.id = 10; kp.type = "KeepPreviousFrame";
  // Keep defaults to 1 (TRUE) via the spec's PortSpec.def; no override needed.
  g.nodes.push_back(kp);

  // RenderTarget out (port idx 1) -> KeepPreviousFrame ImageA (port idx 0).
  g.connections.push_back({500, pinId(1, /*out*/ 1), pinId(10, /*ImageA*/ 0)});
}

void setClear(Graph& g, float r, float gr, float b) {
  Node* rt = g.node(1);
  rt->params["ClearColor.x"] = r;
  rt->params["ClearColor.y"] = gr;
  rt->params["ClearColor.z"] = b;
  rt->params["ClearColor.w"] = 1.0f;
}

// RGBA8 sentinels (opaque). 8-bit unorm of these float clears (no sRGB on RGBA8Unorm → linear*255).
const uint8_t kSentinelA[4] = {255, 0, 0, 255};    // ClearColor (1,0,0)
const uint8_t kSentinelB[4] = {0, 255, 0, 255};    // ClearColor (0,1,0)

}  // namespace

int runKeepPreviousFrameSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();

  // RenderTarget (the solid-color input source) genuinely renders → needs the real metallib + the
  // builtin ops registered (RenderTarget is registered by registerBuiltinPointOps; the feedback ops
  // self-register at file scope). KeepPreviousFrame itself does only a blit (no PSO), but the input
  // source must produce a real texture.
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-keeppreviousframe] FAIL: no metallib\n");
    dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, lib, q, 64, 64);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  buildFeedbackGraph(g);

  bool ok = true;
  keepPreviousFrameInjectBug() = injectBug;

  // ---------------- FLAT cook, two frames ----------------
  // Frame N: ClearColor = A. Cook with KeepPreviousFrame (node 10) as target → displayTex = its FIRST
  // output (PreviousFrame, ordinal 0). On the very first frame PreviousFrame is the OTHER (un-written)
  // buffer — undefined content, so we DON'T assert it. We assert CurrentFrame == A via a second read.
  setClear(g, 1.0f, 0.0f, 0.0f);  // A
  ctx.frameIndex = 0;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
  // CurrentFrame is ordinal 1; the cook-entry only shows ordinal 0. Read the pair directly via the
  // debug accessor (the cross-frame golden needs both outputs without a downstream consumer node).
  {
    MTL::Texture* cur = pg.debugCookedFeedbackOutput(10, /*ordinal=*/1);  // CurrentFrame
    uint8_t got[4];
    if (!readTexel00(cur, got) || !texelNear(got, kSentinelA)) {
      std::printf("[selftest-keeppreviousframe] flat frameN CurrentFrame != A got=(%d,%d,%d,%d) FAIL\n",
                  cur ? got[0] : -1, cur ? got[1] : -1, cur ? got[2] : -1, cur ? got[3] : -1);
      ok = false;
    }
  }

  // Frame N+1: ClearColor = B. Cook again on the SAME PointGraph (the pair + toggle persisted).
  setClear(g, 0.0f, 1.0f, 0.0f);  // B
  ctx.frameIndex = 1;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
  {
    MTL::Texture* prev = pg.debugCookedFeedbackOutput(10, /*ordinal=*/0);  // PreviousFrame == frame N (A)
    MTL::Texture* cur = pg.debugCookedFeedbackOutput(10, /*ordinal=*/1);   // CurrentFrame  == B
    uint8_t gotP[4], gotC[4];
    bool okP = readTexel00(prev, gotP);
    bool okC = readTexel00(cur, gotC);
    // The cross-frame tooth: PreviousFrame must equal frame N's image (A) — the GOOD expectation. In
    // -bug mode (toggle flip suppressed) the routing breaks (PreviousFrame reads the wrong/un-flipped
    // buffer ≠ A) → this assert diverges → exit 1. We assert ==A always; the bug makes it ≠A → bite.
    if (!okP || !texelNear(gotP, kSentinelA)) {
      std::printf("[selftest-keeppreviousframe] flat frameN+1 PreviousFrame got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) FAIL\n",
                  okP ? gotP[0] : -1, okP ? gotP[1] : -1, okP ? gotP[2] : -1, okP ? gotP[3] : -1,
                  kSentinelA[0], kSentinelA[1], kSentinelA[2], kSentinelA[3]);
      ok = false;
    }
    // CurrentFrame == B: with the toggle suppressed (-bug) frame N wrote B into pairB and frame N+1
    // ALSO writes B into pairB (no flip), so CurrentFrame==B still — the bug does NOT bite here. That's
    // fine; the PreviousFrame tooth above is the cross-frame tooth that bites. In the GOOD path
    // CurrentFrame==B is the live tooth. Assert it only in the GOOD run so the -bug run's single
    // diverging tooth (PreviousFrame) is the clean bite signal.
    if (!injectBug && (!okC || !texelNear(gotC, kSentinelB))) {
      std::printf("[selftest-keeppreviousframe] flat frameN+1 CurrentFrame != B got=(%d,%d,%d,%d) FAIL\n",
                  okC ? gotC[0] : -1, okC ? gotC[1] : -1, okC ? gotC[2] : -1, okC ? gotC[3] : -1);
      ok = false;
    }
  }

  // ---------------- REALLOC path (resolution change → ensureFeedbackPair frees the OLD pair) --------
  // Frame N+2: change RenderTarget resolution 4x4 → 8x8 with a NEW color C. ensureFeedbackPair must
  // realloc BOTH buffers to 8x8 (releasing the old 4x4 pair — no leak/UAF). After a realloc, both pair
  // buffers are fresh: CurrentFrame == C (just copied); PreviousFrame is the OTHER (un-written) fresh
  // buffer → undefined, so we only assert CurrentFrame==C + the new dims (the realloc landed). Good run
  // only (the -bug toggle path already bit above). Proves the cross-frame realloc discipline.
  if (!injectBug) {
    Node* rt = g.node(1);
    rt->params["CustomW"] = 8.0f;
    rt->params["CustomH"] = 8.0f;
    setClear(g, 0.0f, 0.0f, 1.0f);  // C = blue
    ctx.frameIndex = 2;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
    MTL::Texture* cur = pg.debugCookedFeedbackOutput(10, /*ordinal=*/1);  // CurrentFrame == C
    const uint8_t kC[4] = {0, 0, 255, 255};
    uint8_t gotC[4];
    bool okC = readTexel00(cur, gotC);
    if (!cur || cur->width() != 8 || cur->height() != 8) {
      std::printf("[selftest-keeppreviousframe] realloc dims FAIL (want 8x8, got %ux%u)\n",
                  cur ? (uint32_t)cur->width() : 0, cur ? (uint32_t)cur->height() : 0);
      ok = false;
    } else if (!okC || !texelNear(gotC, kC)) {
      std::printf("[selftest-keeppreviousframe] realloc CurrentFrame != C got=(%d,%d,%d,%d) FAIL\n",
                  okC ? gotC[0] : -1, okC ? gotC[1] : -1, okC ? gotC[2] : -1, okC ? gotC[3] : -1);
      ok = false;
    }
  }

  // ---------------- RESIDENT (production) cook, two frames ----------------
  // Proves the cross-frame pair + multi-output routing is LIVE on the production cookResident path, not
  // a flat-only black hole (R-2). Same two-frame scenario. Skipped in -bug mode (the flat tooth already
  // bit; the bug suppression is in the shared leaf cook, so the flat path exercised it).
  if (!injectBug) {
    PointGraph pg2(dev, lib, q, 64, 64);
    Graph g2;
    buildFeedbackGraph(g2);

    // Frame N: A.
    setClear(g2, 1.0f, 0.0f, 0.0f);
    ctx.frameIndex = 0;
    {
      SymbolLibrary lib = libFromGraph(g2);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      pg2.cookResident(rg, ctx, nullptr, /*targetPath=*/"10");
      MTL::Texture* cur = pg2.debugCookedFeedbackOutput(10, /*ordinal=*/1, /*resident=*/true);
      uint8_t got[4];
      if (!readTexel00(cur, got) || !texelNear(got, kSentinelA)) {
        std::printf("[selftest-keeppreviousframe] resident frameN CurrentFrame != A FAIL\n");
        ok = false;
      }
    }
    // Frame N+1: B → PreviousFrame must be A (the cross-frame production proof).
    setClear(g2, 0.0f, 1.0f, 0.0f);
    ctx.frameIndex = 1;
    {
      SymbolLibrary lib = libFromGraph(g2);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      pg2.cookResident(rg, ctx, nullptr, /*targetPath=*/"10");
      MTL::Texture* prev = pg2.debugCookedFeedbackOutput(10, /*ordinal=*/0, /*resident=*/true);
      MTL::Texture* cur = pg2.debugCookedFeedbackOutput(10, /*ordinal=*/1, /*resident=*/true);
      uint8_t gotP[4], gotC[4];
      if (!readTexel00(prev, gotP) || !texelNear(gotP, kSentinelA)) {
        std::printf("[selftest-keeppreviousframe] resident frameN+1 PreviousFrame != A (production cross-frame) got=(%d,%d,%d,%d) FAIL\n",
                    readTexel00(prev, gotP) ? gotP[0] : -1, gotP[1], gotP[2], gotP[3]);
        ok = false;
      }
      if (!readTexel00(cur, gotC) || !texelNear(gotC, kSentinelB)) {
        std::printf("[selftest-keeppreviousframe] resident frameN+1 CurrentFrame != B FAIL\n");
        ok = false;
      }
    }
  }

  keepPreviousFrameInjectBug() = false;
  if (!injectBug && ok)
    std::printf("[selftest-keeppreviousframe] flat+resident 2-frame ping-pong match (PreviousFrame==frameN)\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-keeppreviousframe] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
