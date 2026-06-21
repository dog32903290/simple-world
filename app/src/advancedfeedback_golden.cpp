// advancedfeedback_golden — --selftest-advancedfeedback. The CROSS-FRAME × MULTI-PASS golden for the
// AdvancedFeedback feedback×warp op. Single-frame cannot catch feedback bugs (the trail only exists
// across frames), so this is a 2-FRAME scenario run on BOTH the flat cook AND the resident (PRODUCTION)
// cook — R-2 iron rule (only-flat = a production black hole).
//
// Scenario. A centered bright BLOCK (RadialPoints cluster -> DrawPoints -> RenderTarget) feeds
// AdvancedFeedback's Image (the CURRENT content). We cook TWICE on the SAME PointGraph (so the node's
// persistent trail pair + toggle survive — the whole point), driving Zoom=0.998 (the implicit trail
// shrink) and Offset=(0, 0.06) (a strong upward shift so the geometric warp is unmistakable in 8-bit
// readback; the .t3 default 0.003 is below the LSB tolerance — a parameter, same composite path):
//     frame 0 : block PRESENT -> AdvancedFeedback Output: the trail pair clears, current block drawn
//               -> block region LIT, the row ABOVE the block edge still dark.
//     frame 1 : block GONE (Count 0 -> all-black input) -> Output: the previous trail, warped by the
//               Transform shift (Offset.y), is composited; the LIT band has MOVED off the original
//               block edge (geometric shift) AND every texel stays in [0,255] (the FeedbackAdjust
//               value-range stabilizer kept it from white-out / black-hole).
// Asserts (FIXED `want`):
//   (a) TRAIL PRESENT & SHIFTED: a probe ABOVE the frame-0 block edge (previously dark) is LIT at
//       frame 1 (the trail moved up via Offset.y — the warp is geometrically load-bearing).
//   (b) VALUES CLAMPED: frame-1 readback has no all-white blow-out and no all-black collapse at the
//       trail center (the stabilizer holds the loop).
//   (c) WARP LOAD-BEARING: frame-1 lit-from-previously-dark can ONLY pass if the toggle flipped, the
//       right half was read, the warp chain ran, and the current was composited over.
// injectBug (advancedFeedbackInjectBug): SUPPRESSES the toggle flip -> frame 1 reads/writes the SAME
// half it wrote at frame 0, folding it with the all-black input -> the carried trail is mis-addressed
// -> the shifted-trail assert collapses -> exit 1. `want` stays FIXED (the bug removes the trail, it
// does NOT flip the expected).
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/point_graph.h"          // PointGraph::cook / cookResident / debugCookedFeedbackOutput
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph

namespace sw {

bool& advancedFeedbackInjectBug();  // point_ops_advancedfeedback.cpp

namespace {

// Whole-texture readback + luminance (R+G+B) probe at (x,y). -1 for null/empty/out-of-bounds.
int lumAt(MTL::Texture* tex, uint32_t x, uint32_t y) {
  if (!tex) return -1;
  uint32_t w = (uint32_t)tex->width(), h = (uint32_t)tex->height();
  if (w == 0 || h == 0 || x >= w || y >= h) return -1;
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  size_t i = ((size_t)y * w + x) * 4;
  return (int)px[i] + px[i + 1] + px[i + 2];
}

// Max per-channel value at (x,y) (to catch white-out: all three at 255). -1 if null.
int maxChanAt(MTL::Texture* tex, uint32_t x, uint32_t y) {
  if (!tex) return -1;
  uint32_t w = (uint32_t)tex->width(), h = (uint32_t)tex->height();
  if (w == 0 || h == 0 || x >= w || y >= h) return -1;
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  size_t i = ((size_t)y * w + x) * 4;
  int m = px[i];
  if (px[i + 1] > m) m = px[i + 1];
  if (px[i + 2] > m) m = px[i + 2];
  return m;
}

// Build the block-input graph: RadialPoints (centered cluster) -> DrawPoints -> RenderTarget(Custom RS)
// -> AdvancedFeedback (id = afId). Zoom 0.998 (implicit shrink) + Offset.y BIG so the warp shift is
// readable in 8-bit; ShiftBrightness bumped slightly so the decayed trail stays above the LSB floor
// at frame 1 (the .t3 default 0 would let LimitDarks/scale crush the carried trail below readback —
// a parameter, exercising the SAME stabilizer path, not a fork).
void buildBlockGraph(Graph& g, uint32_t RS, int afId) {
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 160.0f; gen.params["Radius"] = 0.20f;  // a solid centered block
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RS; rt.params["CustomH"] = (float)RS;
  g.nodes.push_back(rt);
  Node af; af.id = afId; af.type = "AdvancedFeedback";
  af.params["Zoom"] = 0.998f;          // the implicit trail shrink (Transform Scale)
  af.params["Offset.x"] = 0.0f;
  af.params["Offset.y"] = 0.06f;       // STRONG upward shift -> the trail visibly moves (load-bearing)
  af.params["ShiftBrightness"] = 0.03f;// keep the carried trail above the 8-bit floor across 1 frame
  af.params["Displacement"] = 15.0f;   // OUTER default (×0.01 -> 0.15 in the warp)
  af.params["BlurRadius"] = 4.0f;      // OUTER default
  g.nodes.push_back(af);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  g.connections.push_back({103, pinId(3, 1), pinId(afId, 0)});
}

// Run the 2-frame scenario; fills the probes. `resident` selects the production cook path.
// cx/cy = block center; aboveY = a row ABOVE the block top edge (dark at frame 0, lit-by-shift at
// frame 1). Returns false if any cook produced a null output.
bool runTwoFrames(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, uint32_t RS,
                  uint32_t cx, uint32_t cy, uint32_t aboveY, bool resident,
                  int& centerF0, int& aboveF0, int& aboveF1, int& centerF1, int& maxF1) {
  const int afId = 4;
  PointGraph pg(dev, lib, q, 64, 64);
  Graph g;
  buildBlockGraph(g, RS, afId);
  EvaluationContext ctx{}; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  auto cook = [&](int frame) -> MTL::Texture* {
    ctx.frameIndex = frame;
    if (resident) {
      SymbolLibrary slib = libFromGraph(g);
      ResidentEvalGraph reg = buildEvalGraph(slib, "Root");
      pg.cookResident(reg, ctx, nullptr, /*targetPath=*/"4");
      return pg.debugCookedFeedbackOutput(afId, 0, /*resident=*/true);
    }
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/afId);
    return pg.debugCookedFeedbackOutput(afId, /*ordinal=*/0);
  };

  // Frame 0: block present.
  MTL::Texture* out0 = cook(0);
  if (!out0) return false;
  centerF0 = lumAt(out0, cx, cy);
  aboveF0 = lumAt(out0, cx, aboveY);

  // Frame 1: block gone (all-black current) -> only the warped carried trail survives.
  g.node(1)->params["Count"] = 0.0f;
  MTL::Texture* out1 = cook(1);
  if (!out1) return false;
  aboveF1 = lumAt(out1, cx, aboveY);
  centerF1 = lumAt(out1, cx, cy);
  maxF1 = maxChanAt(out1, cx, cy);
  return true;
}

}  // namespace

int runAdvancedFeedbackSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-advancedfeedback] FAIL: no metallib\n");
    dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  MTL::CommandQueue* q = dev->newCommandQueue();

  const uint32_t RS = 96;
  const uint32_t cx = RS / 2, cy = RS / 2;
  // Block radius 0.20 -> ~±0.20*RS ≈ ±19px around center. The block top edge sits ~cy-19. A row a few
  // px ABOVE that edge is dark at frame 0; the Offset.y=0.06 (≈ +6px in this convention) upward shift
  // carries the trail there at frame 1. Probe ~8px above the edge.
  const uint32_t aboveY = cy - 26;  // above the frame-0 block; within the shifted-trail reach

  bool ok = true;
  advancedFeedbackInjectBug() = injectBug;

  // ---------------- FLAT cook, 2 frames ------------------------------------------------------------
  int centerF0 = -1, aboveF0 = -1, aboveF1 = -1, centerF1 = -1, maxF1 = -1;
  if (!runTwoFrames(dev, lib, q, RS, cx, cy, aboveY, /*resident=*/false,
                    centerF0, aboveF0, aboveF1, centerF1, maxF1)) {
    std::printf("[selftest-advancedfeedback] flat FAIL: null output\n");
    ok = false;
  } else {
    // (a) frame 0: block center lit, the above-edge row dark (baseline).
    bool blockLitF0 = centerF0 > 60;
    bool aboveDarkF0 = aboveF0 < 30;
    // (b)+(c) frame 1: the carried trail MOVED up -> the above-edge row is now LIT (was dark), the
    //     center stayed in-range (not white-out, not black-hole).
    bool trailShifted = aboveF1 > aboveF0 + 12;          // previously-dark row lit by the up-shift
    bool noWhiteOut = maxF1 < 255;                       // stabilizer held (no blown channel)
    bool centerInRange = centerF1 > 6 && centerF1 < 760; // trail present, not collapsed/saturated
    bool flatGood = blockLitF0 && aboveDarkF0 && trailShifted && noWhiteOut && centerInRange;
    if (!flatGood) {
      std::printf("[selftest-advancedfeedback] flat %s centerF0=%d aboveF0=%d aboveF1=%d centerF1=%d "
                  "maxF1=%d (want centerF0>60, aboveF0<30, aboveF1>aboveF0+12 shift, maxF1<255 clamp, "
                  "centerF1 in (6,760))\n",
                  injectBug ? "diverged under -bug (expected, tooth bites)" : "FAIL",
                  centerF0, aboveF0, aboveF1, centerF1, maxF1);
      ok = false;
    }
  }

  // ---------------- RESIDENT (production) cook, 2 frames (R-2 production proof) ---------------------
  if (!injectBug) {
    int rC0 = -1, rA0 = -1, rA1 = -1, rC1 = -1, rM1 = -1;
    if (!runTwoFrames(dev, lib, q, RS, cx, cy, aboveY, /*resident=*/true,
                      rC0, rA0, rA1, rC1, rM1)) {
      std::printf("[selftest-advancedfeedback] resident FAIL: null output\n");
      ok = false;
    } else {
      bool rGood = rC0 > 60 && rA0 < 30 && rA1 > rA0 + 12 && rM1 < 255 && rC1 > 6 && rC1 < 760;
      if (!rGood) {
        std::printf("[selftest-advancedfeedback] resident FAIL centerF0=%d aboveF0=%d aboveF1=%d "
                    "centerF1=%d maxF1=%d (want centerF0>60, aboveF0<30, aboveF1>aboveF0+12, maxF1<255,"
                    " centerF1 in (6,760))\n", rC0, rA0, rA1, rC1, rM1);
        ok = false;
      } else {
        std::printf("[selftest-advancedfeedback] resident: centerF0=%d aboveF0=%d aboveF1=%d(shift) "
                    "centerF1=%d maxF1=%d(clamp) PASS\n", rC0, rA0, rA1, rC1, rM1);
      }
    }
  }

  advancedFeedbackInjectBug() = false;
  if (!injectBug && ok)
    std::printf("[selftest-advancedfeedback] flat+resident 2-frame feedback×warp: trail shifts & "
                "clamps (flat centerF0=%d aboveF1=%d centerF1=%d maxF1=%d)\n",
                centerF0, aboveF1, centerF1, maxF1);

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-advancedfeedback] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
