// afterglow2_golden — --selftest-afterglow2. The CROSS-FRAME × MULTI-PASS golden for the two-color
// feedback×blur COMPOSITION (AfterGlow2). Single-frame cannot catch feedback bugs (the trail only
// exists across frames), so this is a 2-FRAME scenario, run on BOTH the flat cook AND the resident
// (PRODUCTION) cook (cookResident) — R-2 iron rule (only-flat = a production black hole).
//
// AfterGlow2 vs AfterGlow delta proven here: TWO color pins. Color tints the glow ACCUMULATED into the
// persistent trail; OrgColor tints the crisp ORIGINAL image SCREEN-composited on top each frame (the
// terminal Blend pass AfterGlow lacks). The color scenario drives BOTH via the param path and asserts
// each routes to a distinct channel.
//
// Scenario (the load-bearing cross-frame × spread proof). A small CENTERED point cluster (a bright
// "block") is rendered into a RenderTarget that feeds AfterGlow2's Image. We cook TWICE on the SAME
// PointGraph (so the node's persistent trail pair + toggle survive between cooks — the whole point):
//     frame N   : cluster PRESENT -> AfterGlow2 Output: the block region is LIT (trail glow + the
//                 OrgColor-tinted original screen-composited on top).
//     frame N+1 : cluster GONE (Count 0 -> all-black input) -> AfterGlow2 Output: (a) a previously-
//                 black neighbour OUTSIDE the block is now LIT (the DECAYED+BLURRED trail persisted &
//                 spread), AND (b) the block center is dimmer than frame N (decay<1; AND the OrgColor
//                 original-image contribution is gone since the input is now black).
//   Frame N+1's "neighbour lit although the current input is all-black" is the cross-frame tooth: it
//   can only pass if the toggle flipped, the right half was read, and the trail fold carried over.
//
// injectBug (afterGlow2InjectBug): SUPPRESSES the toggle flip. Frame N+1 reads/writes the SAME half it
// wrote at frame N, folding it with the all-black input -> the carried trail is not addressed -> the
// frame-N+1 lit-from-black asserts collapse -> exit 1. `want` stays FIXED (the bug removes the trail,
// it does NOT flip the expected).
#include <cstdio>
#include <cstdlib>  // std::abs(int)
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

bool& afterGlow2InjectBug();  // point_ops_afterglow2.cpp

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
// null/empty/out-of-bounds. Used by the two-color scenario to prove each color routes per-channel.
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
// (Custom RS) -> AfterGlow2 (id = agId). Params bump DecayRate + BlurAmount so the decay step and the
// blur spread are unmistakable in 8-bit readback (the default 0.0157 decay is below the 2-LSB
// tolerance; larger values exercise the SAME composite path — a parameter, not a fork).
void buildBlockGraph(Graph& g, uint32_t RS, int agId) {
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 96.0f; gen.params["Radius"] = 0.22f;  // small centered cluster (a block)
  g.nodes.push_back(gen);
  // DrawPoints now draws faithful PointSize-sized quad sprites (TiXL DrawPoints.hlsl); the .t3 default
  // 0.1 is ~1px. Bump PointSize so the 96-pt cluster has a visible footprint for the AfterGlow2 probe
  // (subject under test is the glow/decay, not the sprite size — DrawPoints parity has its own gate).
  Node drw; drw.id = 2; drw.type = "DrawPoints"; drw.params["PointSize"] = 1.5f; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RS; rt.params["CustomH"] = (float)RS;
  g.nodes.push_back(rt);
  Node ag; ag.id = agId; ag.type = "AfterGlow2";
  ag.params["DecayRate"] = 0.25f;        // survival 0.75 -> a clear, readable decay step
  ag.params["GlowImpact"] = 2.5f;        // strong glow so the trail survives the static -0.04 Blur.Offset
  ag.params["BlurAmount"] = 35.0f;       // wide blur so the block spreads to off-block neighbours
  g.nodes.push_back(ag);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  g.connections.push_back({103, pinId(3, 1), pinId(agId, 0)});
}

}  // namespace

int runAfterGlow2SelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-afterglow2] FAIL: no metallib\n");
    dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  MTL::CommandQueue* q = dev->newCommandQueue();

  const uint32_t RS = 96;
  const uint32_t cx = RS / 2, cy = RS / 2;
  const uint32_t offX = cx + 9;  // OUTSIDE the small cluster, WITHIN the blur reach (BlurAmount 35)

  bool ok = true;
  afterGlow2InjectBug() = injectBug;

  // ---------------- FLAT cook, 2 frames (the cross-frame × spread tooth — runs in BOTH modes) ------
  // Frame N: cluster present. Frame N+1: Count=0 (black input). Read AfterGlow2 Output (ordinal 0).
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
  // than frame N (decay + the original-image contribution removed). Under -bug the suppressed toggle
  // flip breaks the cross-frame routing -> the off-neighbour stays dark and the center collapses ->
  // this SAME assert FAILS -> the tooth bites.
  bool centerLitN = fCenterN > 40;
  bool spread = fOff > 8;                                  // glow bled to a black neighbour (blur)
  bool decayBand = fCenterN1 > 8 && fCenterN1 < fCenterN;  // trail persisted, dimmer than frame N
  bool flatGood = centerLitN && spread && decayBand;
  if (!flatGood) {
    std::printf("[selftest-afterglow2] flat %s centerN=%d off=%d centerN1=%d "
                "(want centerN>40, off>8 spread, centerN1 in (8,centerN))\n",
                injectBug ? "diverged under -bug (expected, tooth bites)" : "FAIL",
                fCenterN, fOff, fCenterN1);
    ok = false;
  }

  // ---------------- DECAY-ISOLATION tooth (the decay MULTIPLIER must bite its target) --------------
  // The flat decayBand assert above (centerN1 < centerN) passes even with NO decay, because frame N+1's
  // black input ALSO removes the terminal OrgColor*orig contribution that lit centerN — so centerN1 drops
  // regardless of the (1-DecayRate) multiplier. To make a WRONG decay FAIL, isolate the multiplier:
  // run the SAME 2-frame scenario at two survival rates and compare frame N+1 directly.
  //   At frame N the trail pair is FRESH (prev == black), so trailN_center = blur(block)*Color is the SAME
  //   for both runs (independent of Decay). At frame N+1 the input is black, so the terminal pass adds 0
  //   and Output_center = trailN_center * Decay. Therefore:
  //       centerN1(survival s) = trailN_center * s   (a clean linear probe of the multiplier).
  //   Run A survival 0.75 (DecayRate 0.25), Run B survival 1.0 (DecayRate 0). Expected ratio = 0.75.
  //   A wrong decay (e.g. Decay forced to 1.0 -> survival 1.0 for run A too) makes ratio == 1.0 -> the
  //   |ratio-0.75| band FAILS. `want` (ratio 0.75) is FIXED — it is the (1-DecayRate) the params declare.
  if (!injectBug) {
    auto centerN1ForDecay = [&](float decayRate) -> int {
      PointGraph pg(dev, lib, q, 64, 64);
      Graph g;
      buildBlockGraph(g, RS, /*agId=*/4);
      g.node(4)->params["DecayRate"] = decayRate;
      EvaluationContext ctx{}; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      ctx.frameIndex = 0;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/4);
      g.node(1)->params["Count"] = 0.0f;  // frame N+1: black input -> output = trailN * survival
      ctx.frameIndex = 1;
      pg.cook(g, ctx, nullptr, /*targetNodeId=*/4);
      return lumAt(pg.debugCookedFeedbackOutput(4, /*ordinal=*/0), cx, cy);
    };
    int dA = centerN1ForDecay(0.25f);  // survival 0.75
    int dB = centerN1ForDecay(0.0f);   // survival 1.0 (no decay) -> the upper anchor
    // dB = trailN_center; dA = trailN_center * 0.75. Predicted dA, with an 8-bit + sampling tolerance.
    int predictedA = (int)(dB * 0.75f + 0.5f);
    bool anchorLit = dB > 32;                       // the no-decay anchor is bright enough to resolve 0.75
    bool decaysBelowAnchor = dA + 6 < dB;           // 0.75 multiplier visibly pulls A below the anchor B
    bool hitsBand = std::abs(dA - predictedA) <= 6; // A lands on trailN*0.75 (NOT on trailN -> rejects decay=1)
    bool decayIso = anchorLit && decaysBelowAnchor && hitsBand;
    if (!decayIso) {
      std::printf("[selftest-afterglow2] decay-isolation FAIL survival0.75=%d survival1.0=%d "
                  "predicted0.75=%d (want |A-pred|<=6 AND A+6<B AND B>32)\n",
                  dA, dB, predictedA);
      ok = false;
    } else {
      std::printf("[selftest-afterglow2] decay-isolation: survival0.75=%d survival1.0=%d predicted=%d "
                  "(ratio %.3f ~ 0.75; (1-DecayRate) multiplier bites) PASS\n",
                  dA, dB, predictedA, dB > 0 ? (float)dA / (float)dB : 0.0f);
    }
  }

  // ---------------- RESIDENT (production) cook, 2 frames (R-2 production proof) ---------------------
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
      std::printf("[selftest-afterglow2] resident FAIL centerN=%d off=%d centerN1=%d "
                  "(want centerN>40, off>8 spread, centerN1 in (8,centerN))\n",
                  rCenterN, rOff, rCenterN1);
      ok = false;
    } else {
      std::printf("[selftest-afterglow2] resident: centerN=%d off=%d(spread) centerN1=%d(decay) PASS\n",
                  rCenterN, rOff, rCenterN1);
    }
  }

  // ---------------- TWO-COLOR param-coverage scenario (the AfterGlow2 delta) ------------------------
  // Drive BOTH color pins via the param path to PROVE the two-color routing:
  //   Color    = (1,0,0) RED   -> tints the glow accumulated into the TRAIL.
  //   OrgColor = (0,0,1) BLUE  -> tints the ORIGINAL image screen-composited on top.
  // At frame N the trail pair is FRESH (prev == black). The lit center =
  //     Color.rgb * blur(block)            (trail glow, RED)
  //   + (orig.rgb * OrgColor.rgb) * orig.a (terminal screen, BLUE)
  // so BOTH R (from Color) AND B (from OrgColor) are lit, while G stays dark — proving each color
  // routes to its own channel. Load-bearing BY CONSTRUCTION (want is FIXED, R-lit AND B-lit, G-dark):
  //   * Color ignored / left white  -> G also lit (the G-dark assert fails).
  //   * OrgColor ignored / dropped  -> B dark (the B-lit assert fails -> proves the terminal pass).
  //   * the two colors swapped/mis-wired to the same channel -> one of R/B goes dark.
  if (!injectBug) {
    PointGraph cpg(dev, lib, q, 64, 64);
    Graph g;
    buildBlockGraph(g, RS, /*agId=*/4);
    g.node(4)->params["Color.x"] = 1.0f;     // RED glow tint (trail) via the param-cook path
    g.node(4)->params["Color.y"] = 0.0f;
    g.node(4)->params["Color.z"] = 0.0f;
    g.node(4)->params["OrgColor.x"] = 0.0f;  // BLUE original tint (terminal screen) via param-cook
    g.node(4)->params["OrgColor.y"] = 0.0f;
    g.node(4)->params["OrgColor.z"] = 1.0f;

    EvaluationContext cctx{}; cctx.time = 0.0f; cctx.deltaTime = 1.0f / 60.0f;
    cctx.frameIndex = 0;
    cpg.cook(g, cctx, nullptr, /*targetNodeId=*/4);
    MTL::Texture* out = cpg.debugCookedFeedbackOutput(4, /*ordinal=*/0);
    int r = chanAt(out, cx, cy, 0), gC = chanAt(out, cx, cy, 1), b = chanAt(out, cx, cy, 2);
    // FIXED want: R lit (Color glow routed), B lit (OrgColor terminal routed), G dark (neither uses G).
    bool rLit = r > 16;                  // Color.x -> R glow into trail
    bool bLit = b > 16;                  // OrgColor.z -> B terminal screen (proves the delta pass)
    bool gDark = gC * 4 + 8 < r && gC * 4 + 8 < b;  // G unlit; both R and B dominate it by a margin
    bool colorGood = rLit && bLit && gDark;
    if (!colorGood) {
      std::printf("[selftest-afterglow2] two-color FAIL R=%d G=%d B=%d "
                  "(want R>16 Color-glow, B>16 OrgColor-screen, G dark)\n", r, gC, b);
      ok = false;
    } else {
      std::printf("[selftest-afterglow2] two-color: R=%d G=%d B=%d (Color->R trail glow, OrgColor->B "
                  "terminal screen; both param-driven) PASS\n", r, gC, b);
    }

    // ---- OrgColor.w LIVE proof (Fix 1): the terminal Blend scales by ImageBColor.a (Blend.hlsl:54
    // tB.a = clamp(orig.a)*clamp(OrgColor.a)). Re-cook the SAME RED-glow/BLUE-org scenario but with
    // OrgColor.w=0. The terminal BLUE contribution must VANISH (B collapses to ~0), while the RED trail
    // glow — which does NOT pass through OrgColor at all — stays lit. If OrgColor.w were dropped (the
    // pre-fix bug), B would stay at its w=1 level and this assert FAILS. `want` FIXED: B vanishes, R holds.
    {
      PointGraph wpg(dev, lib, q, 64, 64);
      Graph wg;
      buildBlockGraph(wg, RS, /*agId=*/4);
      wg.node(4)->params["Color.x"] = 1.0f; wg.node(4)->params["Color.y"] = 0.0f;
      wg.node(4)->params["Color.z"] = 0.0f;
      wg.node(4)->params["OrgColor.x"] = 0.0f; wg.node(4)->params["OrgColor.y"] = 0.0f;
      wg.node(4)->params["OrgColor.z"] = 1.0f;
      wg.node(4)->params["OrgColor.w"] = 0.0f;  // gate the terminal screen-add OFF
      EvaluationContext wctx{}; wctx.time = 0.0f; wctx.deltaTime = 1.0f / 60.0f;
      wctx.frameIndex = 0;
      wpg.cook(wg, wctx, nullptr, /*targetNodeId=*/4);
      MTL::Texture* wout = wpg.debugCookedFeedbackOutput(4, /*ordinal=*/0);
      int rw = chanAt(wout, cx, cy, 0), bw = chanAt(wout, cx, cy, 2);
      bool bVanished = bw <= 6;            // terminal BLUE gated off by OrgColor.w=0
      bool rHeld = rw > 16 && rw + 6 >= r; // RED trail glow unaffected (bypasses OrgColor)
      bool orgWGood = bVanished && rHeld;
      if (!orgWGood) {
        std::printf("[selftest-afterglow2] OrgColor.w FAIL w=0 -> R=%d B=%d (was R=%d B=%d at w=1; "
                    "want B<=6 vanished, R held)\n", rw, bw, r, b);
        ok = false;
      } else {
        std::printf("[selftest-afterglow2] OrgColor.w live: w=1 B=%d -> w=0 B=%d (terminal screen "
                    "gated off); R %d->%d held PASS\n", b, bw, r, rw);
      }
    }
  }

  afterGlow2InjectBug() = false;
  if (!injectBug && ok)
    std::printf("[selftest-afterglow2] flat+resident 2-frame feedback×blur two-color: trail spreads "
                "& decays (flat centerN=%d off=%d centerN1=%d)\n", fCenterN, fOff, fCenterN1);

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-afterglow2] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
