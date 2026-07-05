// colorlisttoints_golden — --selftest-colorlisttoints. CHAIN-THROUGH-evalFloat golden for the
// COLORLIST→FLOATLIST BRIDGE consumer ColorListToInts (numbers/floats/process): a ColorList producer
// (ColorsToList) feeds ColorListToInts, whose FloatList/IntList output is read by a DOWNSTREAM host-scalar
// consumer (PickFloatFromList for an indexed ELEMENT, FloatListLength for the COUNT) via evalFloat (the
// pure value recursion). This proves the vec4-list currency CROSSES the rail into the int-list currency
// AND that the crossed list flows node→node and is CONSUMED, not merely transported.
//
// Each expected value is hand-derived from ColorListToInts.cs (external/tixl/Operators/Lib/numbers/floats/
// process/ColorListToInts.cs):
//   AppendAsInt(f) (cs:71-74): element = (int)( (f*255).Clamp(0,255) ) — C# precedence CLAMPS the FLOAT
//     (f*255) into [0,255] THEN casts (int) (truncate toward zero). So:
//       f=1.0 → 255 ; f=0.0 → 0 ; f=0.5 → 127.5 → (int)127 (TRUNCATE, ★ not 128 round) ;
//       f=0.25 → 63.75 → 63 ; f=2.0 → clampf(510→255) → 255 ; f=-0.1 → clampf(-25.5→0) → 0.
//   AppendChannelValues (cs:41-69) per OutputMode: RGBA→X,Y,Z,W ; ARGB→W,X,Y,Z ; RGB→X,Y,Z ; R→X ; A→W.
//   Modes enum (cs:86-93): RGBA=0 ARGB=1 RGB=2 R=3 A=4 ; DEFAULT RGB (cs:76).
//
// The LOAD-BEARING legs (each would fail if the op body were wrong in a specific way):
//   • L_TRUNC: 0.5 → 127, NOT 128 — distinguishes truncate-toward-zero from round-to-nearest.
//   • L_CLAMP_HI / L_CLAMP_LO: 2.0 → 255 and -0.1 → 0 — distinguishes the clamp from an unclamped cast
//     (2.0*255=510 would be 510 without the clamp; -0.1*255=-25 without it).
//   • L_ARGB: mode 1 reorders to W,X,Y,Z — distinguishes the mode switch from a fixed RGBA order.
//   • L_COUNT: RGBA of 2 colors = 8 ints — distinguishes the per-mode channel count (RGBA=4/color).
//
// injectBug routes through floatListInjectBug() (drops ColorListToInts's last output element in the REAL
// cook) so the DOWNSTREAM evalFloat reads a wrong count/element → RED on the actual cook path, NOT by
// flipping the expected value. --selftest-colorlisttoints-bug must exit NON-zero.
#include <cmath>
#include <cstdio>
#include <vector>

#include <simd/simd.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/floatlist_op_registry.h"  // floatListInjectBug
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId + evalFloat
#include "runtime/point_graph.h"             // PointGraph::cook

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Build ColorsToList(id 1) with one Const per (color,channel) wired into the matching component port
// (Colors.x=0/.y=1/.z=2/.w=3), the SAME wiring colorlist_golden.cpp uses. Returns the graph; ColorsToList
// out port is index 4 (ColorList).
void addColorsToList(Graph& g, int ctlId, const std::vector<simd::float4>& colors, int& nextNode,
                     int& connId) {
  Node ctl; ctl.id = ctlId; ctl.type = "ColorsToList"; g.nodes.push_back(ctl);
  const int chanPin[4] = {pinId(ctlId, 0), pinId(ctlId, 1), pinId(ctlId, 2), pinId(ctlId, 3)};
  for (size_t i = 0; i < colors.size(); ++i) {
    const float comp[4] = {colors[i].x, colors[i].y, colors[i].z, colors[i].w};
    for (int k = 0; k < 4; ++k) {
      Node c; c.id = nextNode++; c.type = "Const"; c.params["value"] = comp[k];
      g.nodes.push_back(c);
      g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), chanPin[k]});
    }
  }
}

// ColorsToList(colors) → ColorListToInts(mode) → PickFloatFromList(index). Returns the PickFloatFromList
// terminal id (read via evalFloat port 0). ColorListToInts: ColorLists port 0 (ColorList MultiInput),
// OutputMode port 1, out port 2 (FloatList). PickFloatFromList: Selected port 0, Input port 1, Index port 2.
int chainPick(Graph& g, const std::vector<simd::float4>& colors, int mode, float index) {
  int nextNode = 10, connId = 100;
  const int ctl = 1, cli = 2, pk = 3;
  addColorsToList(g, ctl, colors, nextNode, connId);
  Node c; c.id = cli; c.type = "ColorListToInts"; c.params["OutputMode"] = (float)mode; g.nodes.push_back(c);
  g.connections.push_back({connId++, pinId(ctl, /*out*/ 4), pinId(cli, /*ColorLists*/ 0)});
  Node p; p.id = pk; p.type = "PickFloatFromList"; p.params["Index"] = index; g.nodes.push_back(p);
  g.connections.push_back({connId++, pinId(cli, /*out*/ 2), pinId(pk, /*Input*/ 1)});
  return pk;
}

// ColorsToList(colors) → ColorListToInts(mode) → FloatListLength (count).
int chainLen(Graph& g, const std::vector<simd::float4>& colors, int mode) {
  int nextNode = 10, connId = 100;
  const int ctl = 1, cli = 2, len = 3;
  addColorsToList(g, ctl, colors, nextNode, connId);
  Node c; c.id = cli; c.type = "ColorListToInts"; c.params["OutputMode"] = (float)mode; g.nodes.push_back(c);
  g.connections.push_back({connId++, pinId(ctl, /*out*/ 4), pinId(cli, /*ColorLists*/ 0)});
  Node l; l.id = len; l.type = "FloatListLength"; g.nodes.push_back(l);
  g.connections.push_back({connId++, pinId(cli, /*out*/ 2), pinId(len, /*Input*/ 1)});
  return len;
}

float cookEval(PointGraph& pg, Graph& g, int terminalId) {
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/terminalId);
  return evalFloat(g, pinId(terminalId, /*out port 0*/ 0), ctx);
}

}  // namespace

int runColorListToIntsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  bool ok = true;

  auto run = [&](const char* tag, Graph& g, int terminal, float want) {
    floatListInjectBug() = injectBug;
    float got = cookEval(pg, g, terminal);
    floatListInjectBug() = false;
    bool pass = nearf(got, want);
    ok = ok && pass;
    std::printf("[selftest-colorlisttoints] %s = %.2f want=%.2f -> %s\n", tag, got, want,
                pass ? "PASS" : "FAIL");
  };

  // Two colors used across the mode legs. c0 exercises the load-bearing channel values; c1 checks reorder.
  //   c0 = (1.0, 0.5, 0.25, 2.0)  → 255, 127(TRUNC), 63, 255(CLAMP HI)
  //   c1 = (-0.1, 0.0, 1.0, 0.0)  → 0(CLAMP LO), 0, 255, 0
  const std::vector<simd::float4> two = {
      simd::make_float4(1.0f, 0.5f, 0.25f, 2.0f),
      simd::make_float4(-0.1f, 0.0f, 1.0f, 0.0f)};

  // === RGBA mode (0): each color emits X,Y,Z,W. Flat index into the 8-element output. ===
  // c0 RGBA = [255,127,63,255]; c1 RGBA = [0,0,255,0]. Combined = [255,127,63,255, 0,0,255,0].
  { Graph g; int t = chainPick(g, two, /*RGBA*/ 0, /*idx*/ 0);
    run("RGBA[0]=255(f=1.0)", g, t, 255.0f); }
  // ★L_TRUNC: index 1 = c0.G = (int)127.5 = 127 (truncate toward zero, NOT 128).
  { Graph g; int t = chainPick(g, two, /*RGBA*/ 0, /*idx*/ 1);
    run("RGBA[1]=127(0.5 TRUNCATE not 128)", g, t, 127.0f); }
  // index 2 = c0.B = (int)63.75 = 63.
  { Graph g; int t = chainPick(g, two, /*RGBA*/ 0, /*idx*/ 2);
    run("RGBA[2]=63(f=0.25)", g, t, 63.0f); }
  // ★L_CLAMP_HI: index 3 = c0.A = clampf(2.0*255→255) = 255 (NOT 510).
  { Graph g; int t = chainPick(g, two, /*RGBA*/ 0, /*idx*/ 3);
    run("RGBA[3]=255(f=2.0 CLAMP HI)", g, t, 255.0f); }
  // ★L_CLAMP_LO: index 4 = c1.R = clampf(-0.1*255→0) = 0 (NOT -25).
  { Graph g; int t = chainPick(g, two, /*RGBA*/ 0, /*idx*/ 4);
    run("RGBA[4]=0(f=-0.1 CLAMP LO)", g, t, 0.0f); }
  // index 6 = c1.B = 255 (f=1.0).
  { Graph g; int t = chainPick(g, two, /*RGBA*/ 0, /*idx*/ 6);
    run("RGBA[6]=255(c1.B f=1.0)", g, t, 255.0f); }
  // ★L_COUNT: RGBA of 2 colors = 8 ints.
  { Graph g; int t = chainLen(g, two, /*RGBA*/ 0);
    run("RGBA.Count=8(2 colors x4)", g, t, 8.0f); }

  // === ARGB mode (1): each color emits W,X,Y,Z (alpha first). ★L_ARGB reorder. ===
  // c0 ARGB = [A=255, R=255, G=127, B=63] (c0.W=2.0→255). index 0 = c0.A = 255; index 1 = c0.R = 255;
  // index 2 = c0.G = 127. The DISTINGUISHING index is 0: under RGBA it would be R (also 255 here) — so use
  // c1's alpha to disambiguate: c1 ARGB index 4 = c1.A = c1.W = 0.0 → 0, vs RGBA index 4 = c1.R = 0 too.
  // Pick an index where ARGB≠RGBA: c0 ARGB[2] = c0.G = 127; RGBA[2] would be c0.B = 63. So [2] disambiguates.
  { Graph g; int t = chainPick(g, two, /*ARGB*/ 1, /*idx*/ 2);
    run("ARGB[2]=127(=G; RGBA would be 63=B)", g, t, 127.0f); }
  // ARGB index 3 = c0.B = 63 (=Z); RGBA[3] would be A=255. Second disambiguator.
  { Graph g; int t = chainPick(g, two, /*ARGB*/ 1, /*idx*/ 3);
    run("ARGB[3]=63(=B; RGBA would be 255=A)", g, t, 63.0f); }

  // === RGB mode (2, default): each color emits X,Y,Z (no alpha). Count = 3/color. ===
  // c0 RGB = [255,127,63]; index 2 = 63. RGB.Count of 2 colors = 6.
  { Graph g; int t = chainPick(g, two, /*RGB*/ 2, /*idx*/ 2);
    run("RGB[2]=63(no alpha)", g, t, 63.0f); }
  { Graph g; int t = chainLen(g, two, /*RGB*/ 2);
    run("RGB.Count=6(2 colors x3)", g, t, 6.0f); }

  // === R mode (3): emit X only. Count = 1/color. c1.R clamps to 0. ===
  { Graph g; int t = chainPick(g, two, /*R*/ 3, /*idx*/ 1);
    run("R[1]=0(c1.R=-0.1 CLAMP LO)", g, t, 0.0f); }
  { Graph g; int t = chainLen(g, two, /*R*/ 3);
    run("R.Count=2(2 colors x1)", g, t, 2.0f); }

  // === A mode (4): emit W only. c0.A = 2.0 → 255; count = 1/color. ===
  { Graph g; int t = chainPick(g, two, /*A*/ 4, /*idx*/ 0);
    run("A[0]=255(c0.A=2.0 CLAMP HI)", g, t, 255.0f); }

  q->release();
  dev->release();
  pool->release();

  // Harness convention: -bug variant must exit NON-zero. injectBug drops ColorListToInts's last output
  // element → a downstream Length/Pick reads wrong → ok false → return 1 (teeth bite). No inversion.
  std::printf("[selftest-colorlisttoints] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
