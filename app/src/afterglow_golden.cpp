// afterglow_golden — --selftest-afterglow. The CROSS-FRAME × MULTI-PASS golden for the feedback×blur
// COMPOSITION (AfterGlow). Single-frame cannot catch feedback bugs (the trail only exists across
// frames), so this is a 2-FRAME scenario, run on BOTH the flat cook AND the resident (PRODUCTION)
// cook (cookResident) — R-2 iron rule (only-flat = a production black hole).
//
// Scenario (the load-bearing cross-frame × spread proof). A small CENTERED point cluster (a bright
// "block") is rendered into a RenderTarget that feeds AfterGlow's Image. We cook TWICE on the SAME
// PointGraph (so the node's persistent trail pair + toggle survive between cooks — the whole point):
//     frame N   : cluster PRESENT -> AfterGlow Output: the block region is LIT (the glow composited
//                 the blurred current image into the fresh trail).
//     frame N+1 : cluster GONE (Count 0 -> input all black) -> AfterGlow Output: (a) a previously-
//                 black neighbour OUTSIDE the block is now LIT (the trail = the DECAYED+BLURRED
//                 previous frame persists and SPREAD), AND (b) the block center is dimmer than at
//                 frame N (decay<1 applied).
//   Frame N+1's "neighbour lit although the current input is all-black" is the cross-frame tooth: it
//   can only pass if the toggle flipped, the right half was read, and the composite folded prev*decay
//   + blur(prev-content). The same scenario runs on flat AND resident.
//
// injectBug (afterGlowInjectBug): SUPPRESSES the toggle flip. Frame N+1 then reads/writes the SAME
// half it wrote at frame N, folding it with the all-black input -> the carried trail is NOT addressed
// correctly -> the frame-N+1 lit-from-black asserts collapse -> exit 1. The `want` stays FIXED (we
// assert the trail persists & spreads; the bug removes it, it does NOT flip the expected).
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/point_graph.h"          // PointGraph::cook / cookResident / debugCookedFeedbackOutput
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph

namespace sw {

bool& afterGlowInjectBug();  // point_ops_afterglow.cpp

namespace {

// Whole-texture readback + luminance probe at (x,y). Returns -1 for null/empty/out-of-bounds.
int lumAt(MTL::Texture* tex, uint32_t x, uint32_t y) {
  if (!tex) return -1;
  uint32_t w = (uint32_t)tex->width(), h = (uint32_t)tex->height();
  if (w == 0 || h == 0 || x >= w || y >= h) return -1;
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  size_t i = ((size_t)y * w + x) * 4;
  return (int)px[i] + px[i + 1] + px[i + 2];
}

// Whole-texture readback + SINGLE-channel probe at (x,y). channel: 0=R 1=G 2=B 3=A. Returns -1 for
// null/empty/out-of-bounds. Used by the Color param-coverage scenario to prove the glow tint is
// routed per-channel (Color.x->R, Color.y->G, Color.z->B), which lumAt's RGB sum cannot see.
int chanAt(MTL::Texture* tex, uint32_t x, uint32_t y, uint32_t channel) {
  if (!tex || channel > 3) return -1;
  uint32_t w = (uint32_t)tex->width(), h = (uint32_t)tex->height();
  if (w == 0 || h == 0 || x >= w || y >= h) return -1;
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  tex->getBytes(px.data(), w * 4, MTL::Region::Make2D(0, 0, w, h), 0);
  size_t i = ((size_t)y * w + x) * 4;
  return (int)px[i + channel];
}

// Build the block-input graph: RadialPoints (small centered cluster) -> DrawPoints -> RenderTarget
// (Custom RS) -> AfterGlow (id = agId). Params bump DecayRate + BlurAmount so the decay step and the
// blur spread are unmistakable in 8-bit readback (the default 0.0157 decay is below the 2-LSB
// tolerance; larger values exercise the SAME composite path — a parameter, not a fork).
void buildBlockGraph(Graph& g, uint32_t RS, int agId) {
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 96.0f; gen.params["Radius"] = 0.22f;  // small centered cluster (a block)
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RS; rt.params["CustomH"] = (float)RS;
  g.nodes.push_back(rt);
  Node ag; ag.id = agId; ag.type = "AfterGlow";
  ag.params["DecayRate"] = 0.25f;        // survival 0.75 -> a clear, readable decay step
  ag.params["GlowImpact"] = 1.0f;        // strong glow so the trail is bright at frame N
  ag.params["BlurAmount"] = 22.0f;       // wide blur so the block spreads to off-block neighbours
  ag.params["ContrastOffset2"] = 0.0f;   // no added constant (default -0.76 would crush to black)
  g.nodes.push_back(ag);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  g.connections.push_back({103, pinId(3, 1), pinId(agId, 0)});
}

}  // namespace

int runAfterGlowSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-afterglow] FAIL: no metallib\n");
    dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  MTL::CommandQueue* q = dev->newCommandQueue();

  const uint32_t RS = 96;
  const uint32_t cx = RS / 2, cy = RS / 2;
  const uint32_t offX = cx + 13;  // OUTSIDE the small cluster, WITHIN the blur reach (BlurAmount 22)

  bool ok = true;
  afterGlowInjectBug() = injectBug;

  // ---------------- FLAT cook, 2 frames (the cross-frame × spread tooth — runs in BOTH modes) ------
  // Frame N: cluster present. Frame N+1: Count=0 (black input). Read AfterGlow Output (ordinal 0).
  int fCenterN = -1, fOff = -1, fCenterN1 = -1;
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    buildBlockGraph(g, RS, /*agId=*/4);

    EvaluationContext ctx{}; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    ctx.frameIndex = 0;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/4);
    fCenterN = lumAt(pg.debugCookedFeedbackOutput(4, /*ordinal=*/0), cx, cy);

    g.node(1)->params["Count"] = 0.0f;  // cluster gone -> all-black input
    ctx.frameIndex = 1;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/4);
    MTL::Texture* out = pg.debugCookedFeedbackOutput(4, /*ordinal=*/0);
    fOff = lumAt(out, offX, cy);
    fCenterN1 = lumAt(out, cx, cy);
  }
  // GOOD expectation (asserted ALWAYS — `want` is FIXED): frame N center lit; frame N+1 a previously-
  // black off-neighbour lit (blur spread of the carried trail) AND the center persisted but dimmer
  // than frame N (decay). Under -bug the suppressed toggle flip breaks the cross-frame routing (frame
  // N+1 reads/writes the same half it wrote at frame N, folding it with the all-black input), so the
  // off-neighbour stays dark and the center collapses -> this SAME assert FAILS -> the tooth bites.
  bool centerLitN = fCenterN > 40;
  bool spread = fOff > 8;                                  // glow bled to a black neighbour (blur)
  bool decayBand = fCenterN1 > 8 && fCenterN1 < fCenterN;  // trail persisted, dimmer than frame N
  bool flatGood = centerLitN && spread && decayBand;
  if (!flatGood) {
    std::printf("[selftest-afterglow] flat %s centerN=%d off=%d centerN1=%d "
                "(want centerN>40, off>8 spread, centerN1 in (8,centerN))\n",
                injectBug ? "diverged under -bug (expected, tooth bites)" : "FAIL",
                fCenterN, fOff, fCenterN1);
    ok = false;
  }

  // ---------------- RESIDENT (production) cook, 2 frames (R-2 production proof) ---------------------
  // Same block scenario through the canonical resident path (libFromGraph -> buildEvalGraph) on a
  // FRESH PointGraph. Good run only (the -bug tooth already bit on the shared leaf cook above).
  if (!injectBug) {
    PointGraph rpg(dev, lib, q, 64, 64);
    Graph g;
    buildBlockGraph(g, RS, /*agId=*/4);

    EvaluationContext rctx{}; rctx.time = 0.0f; rctx.deltaTime = 1.0f / 60.0f;
    rctx.frameIndex = 0;
    {
      SymbolLibrary slib = libFromGraph(g);
      ResidentEvalGraph reg = buildEvalGraph(slib, "Root");
      rpg.cookResident(reg, rctx, nullptr, /*targetPath=*/"4");
    }
    int rCenterN = lumAt(rpg.debugCookedFeedbackOutput(4, 0, /*resident=*/true), cx, cy);

    g.node(1)->params["Count"] = 0.0f;
    rctx.frameIndex = 1;
    int rOff = -1, rCenterN1 = -1;
    {
      SymbolLibrary slib = libFromGraph(g);
      ResidentEvalGraph reg = buildEvalGraph(slib, "Root");
      rpg.cookResident(reg, rctx, nullptr, /*targetPath=*/"4");
      MTL::Texture* out = rpg.debugCookedFeedbackOutput(4, 0, /*resident=*/true);
      rOff = lumAt(out, offX, cy);
      rCenterN1 = lumAt(out, cx, cy);
    }
    bool rGood = rCenterN > 40 && rOff > 8 && rCenterN1 > 8 && rCenterN1 < rCenterN;
    if (!rGood) {
      std::printf("[selftest-afterglow] resident FAIL centerN=%d off=%d centerN1=%d "
                  "(want centerN>40, off>8 spread, centerN1 in (8,centerN))\n",
                  rCenterN, rOff, rCenterN1);
      ok = false;
    } else {
      std::printf("[selftest-afterglow] resident: centerN=%d off=%d(spread) centerN1=%d(decay) PASS\n",
                  rCenterN, rOff, rCenterN1);
    }
  }

  // ---------------- COLOR param-coverage scenario (柏為 gate edf73a9) -------------------------------
  // The scenarios above leave Color at its ~0.59 grey DEFAULT, so the declared Color.x/.y/.z params are
  // never DRIVEN through the param-cook path -> golden-UNCOVERED. Close it: drive Color via params (like
  // DecayRate/GlowImpact/BlurAmount) to a RED tint and assert the added glow is red-dominant. Color
  // drives the additive Layer2d glow tint (AfterGlow.t3:374-378): composite = prev*(1-DecayRate) +
  // Color.rgb * blur(current). At frame N the trail pair is FRESH (prev == black), so the lit center is
  // ~= Color.rgb * blur(block) -> with Color=(1,0,0) the R channel dominates G and B.
  // Load-bearing BY CONSTRUCTION (want is FIXED, R-dominant): a wrong Color routing fails it —
  //   * Color ignored / left grey  -> R ~= G ~= B   (R>>G and R>>B both fail)
  //   * Color.y/.z mis-wired to .x  -> G or B lit too (the >> margin fails)
  // so this asserts the per-channel tint route, which the grey scenarios' lum-sum cannot see.
  if (!injectBug) {
    PointGraph cpg(dev, lib, q, 64, 64);
    Graph g;
    buildBlockGraph(g, RS, /*agId=*/4);
    g.node(4)->params["Color.x"] = 1.0f;  // RED tint via the param-cook path (cookParam "Color.x"...)
    g.node(4)->params["Color.y"] = 0.0f;
    g.node(4)->params["Color.z"] = 0.0f;

    EvaluationContext cctx{}; cctx.time = 0.0f; cctx.deltaTime = 1.0f / 60.0f;
    cctx.frameIndex = 0;
    cpg.cook(g, cctx, nullptr, /*targetNodeId=*/4);
    MTL::Texture* out = cpg.debugCookedFeedbackOutput(4, /*ordinal=*/0);
    int r = chanAt(out, cx, cy, 0), gC = chanAt(out, cx, cy, 1), b = chanAt(out, cx, cy, 2);
    // FIXED want: the lit center is RED-tinted -> R lit AND R dominates G and B by a wide margin.
    // Threshold is a SINGLE channel (R), not lumAt's RGB sum, so it sits well below the ~54 sum the
    // grey scenarios read; R~30 is unambiguously lit here while G/B sit at 0.
    bool redLit = r > 16;
    bool rDomG = r > gC * 4 + 8;  // generous margin; grey (r~=g) or G-mis-route both fail this
    bool rDomB = r > b * 4 + 8;
    bool colorGood = redLit && rDomG && rDomB;
    if (!colorGood) {
      std::printf("[selftest-afterglow] color-param FAIL R=%d G=%d B=%d "
                  "(want R>16 red-lit, R>>G, R>>B for Color=(1,0,0))\n", r, gC, b);
      ok = false;
    } else {
      std::printf("[selftest-afterglow] color-param: R=%d G=%d B=%d red-tinted glow (Color.x/.y/.z "
                  "param-driven) PASS\n", r, gC, b);
    }
  }

  afterGlowInjectBug() = false;
  if (!injectBug && ok)
    std::printf("[selftest-afterglow] flat+resident 2-frame feedback×blur: trail spreads & decays "
                "(flat centerN=%d off=%d centerN1=%d)\n", fCenterN, fOff, fCenterN1);

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-afterglow] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
