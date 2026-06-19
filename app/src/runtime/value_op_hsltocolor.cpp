// HSLToColor value op (value-op self-registration seam leaf — MULTI-OUTPUT).
// TiXL authority: Operators/Lib/numbers/color/HSLToColor.cs (lines 14-70).
//
//   HSLToColor.cs Update() — ported VERBATIM (line refs to the .cs):
//     L16: var hue = (Hue.GetValue(context) % 1) * 360f;   // NOTE: % 1, NOT % 360
//     L18: var sat        = Saturation.GetValue(context);
//     L19: var brightness = Lightness.GetValue(context);
//     L21-41: 3-band fSat ramp keyed on `hue` (degrees):
//        hue < 120 : fSatR=(120-hue)/60, fSatG=hue/60,        fSatB=0
//        hue < 240 : fSatR=0,            fSatG=(240-hue)/60,   fSatB=(hue-120)/60
//        else      : fSatR=(hue-240)/60, fSatG=0,             fSatB=(360-hue)/60
//     L43-45: per-channel clamp: fSatC = (fSatC < 1) ? fSatC : 1   (cap at 1, NO lower clamp)
//     L47-49: fTmpC = 2*sat*fSatC + (1 - sat)
//     L52-63: lightness branch:
//        brightness < 0.5 : fC = brightness * fTmpC
//        else             : fC = (1 - brightness)*fTmpC + 2*brightness - 1
//     L68: Color.Value = new Vector4(fR, fG, fB, Alpha.GetValue(context));   // NO output clamp
//
//   Ports (HSLToColor.cs L72-82): Hue, Saturation, Lightness (InputSlot<float>, default 0),
//   Alpha = InputSlot<float> with default 1f (L82). Output: Color = Slot<Vector4>.
//
// EVAL-SIDE LAYOUT (multi-output convention, mirrors Vector4Components / DotVec4): this runtime
// has no native Vector4/Color port type — a Slot<Vector4> output decomposes into FOUR Float output
// ports (Color.x/.y/.z/.w = R/G/B/A) grouped under one Widget::Vec head (vecArity=4). The FOUR
// scalar inputs (Hue/Saturation/Lightness/Alpha) are distinct Float params (Widget::Float), NOT a
// vec head, and occupy in[0..3]. The four output ports come AFTER the inputs at spec index outIdx
// in [n, n+3] (n = 4 input ports), so the output channel is k = outIdx - n ∈ [0,3]:
//   k=0 → fR, k=1 → fG, k=2 → fB, k=3 → alpha (Alpha input passed straight through).
// All four channels share the SAME fSat/fTmp/lightness computation; only the channel selection at
// the end differs. evalFloat pulls each output pin independently (flat path — HSLToColor is NOT a
// multiInput op, one wire per port), recomputing the closed-form HSL each pull (pure, no state).
//
// FORKS (named, each with a TiXL line ref — all are host-data-model seams, NOT math forks):
//   - fork-vec4-decompose-arity: TiXL's Color is a single Slot<Vector4> (HSLToColor.cs L7). sw has
//     only scalar Float ports, so the vec4 output is exposed as four Float ports (Color.x/.y/.z/.w)
//     under one Widget::Vec head (vecArity=4) — same convention shipped for Vector4Components. The
//     R/G/B/A values are byte-identical to TiXL's Vector4 components; only the host type differs.
//   - hsl-hue-mod1: hue = (Hue % 1) * 360 (HSLToColor.cs L16) — the modulus is % 1 (wrap to the
//     unit interval THEN scale to degrees), NOT % 360. Hue values in [0,1) are the normal range;
//     Hue >= 1 wraps to the same [0,360) band as its fractional part. Ported verbatim; the injectBug
//     below flips this to % 360 and a Hue=2 golden goes RED to prove the tooth bites.
//   - hsl-fsat-clamp: fSatR/G/B are clamped to <= 1 (HSLToColor.cs L43-45: `(x < 1) ? x : 1`) — an
//     UPPER cap only; there is NO lower clamp, so fSat can stay negative in the else-band
//     (e.g. fSatB=(360-hue)/60 < 0 for hue>360 — only reachable under the % 360 bug). Ported verbatim.
//   - hsl-lightness-branch: brightness < 0.5 vs >= 0.5 produces different fR/fG/fB formulas
//     (HSLToColor.cs L52-63). The 0.5 boundary uses the else (>= 0.5) branch. Ported verbatim.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runHslToColorSelfTest(bool injectBug);

namespace {

// in[] = [Hue, Saturation, Lightness, Alpha] (the 4 scalar Float input ports, n=4).
// Output ports Color.x/.y/.z/.w (R/G/B/A) are at spec index outIdx in [n, n+3] → channel k = outIdx-n.
// Ports HSLToColor.cs L16-68, ported verbatim (see file header for line-by-line mapping).
float evalHslToColor(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  int k = outIdx - n;  // output channel: 0=R, 1=G, 2=B, 3=A
  if (k < 0 || k > 3) return 0.0f;

  const float hueIn      = in[0];
  const float sat        = in[1];
  const float brightness = in[2];
  const float alpha      = in[3];

  // L68: Alpha passes straight through (no math). Compute it before the color work.
  if (k == 3) return alpha;

  // L16: hue = (Hue % 1) * 360   (hsl-hue-mod1: % 1, NOT % 360).
  const float hue = std::fmod(hueIn, 1.0f) * 360.0f;

  // L21-41: 3-band fSat ramp (degrees).
  float fSatR = 1.0f, fSatG = 1.0f, fSatB = 1.0f;
  if (hue < 120.0f) {
    fSatR = (120.0f - hue) / 60.0f;
    fSatG = hue / 60.0f;
    fSatB = 0.0f;
  } else if (hue < 240.0f) {
    fSatR = 0.0f;
    fSatG = (240.0f - hue) / 60.0f;
    fSatB = (hue - 120.0f) / 60.0f;
  } else {
    fSatR = (hue - 240.0f) / 60.0f;
    fSatG = 0.0f;
    fSatB = (360.0f - hue) / 60.0f;
  }

  // L43-45: per-channel UPPER clamp at 1 (hsl-fsat-clamp; no lower clamp).
  fSatR = (fSatR < 1.0f) ? fSatR : 1.0f;
  fSatG = (fSatG < 1.0f) ? fSatG : 1.0f;
  fSatB = (fSatB < 1.0f) ? fSatB : 1.0f;

  // L47-49: fTmpC = 2*sat*fSatC + (1 - sat).
  const float fTmpR = 2.0f * sat * fSatR + (1.0f - sat);
  const float fTmpG = 2.0f * sat * fSatG + (1.0f - sat);
  const float fTmpB = 2.0f * sat * fSatB + (1.0f - sat);

  // L52-63: lightness branch (hsl-lightness-branch; 0.5 boundary → else branch).
  float fR, fG, fB;
  if (brightness < 0.5f) {
    fR = brightness * fTmpR;
    fG = brightness * fTmpG;
    fB = brightness * fTmpB;
  } else {
    fR = (1.0f - brightness) * fTmpR + 2.0f * brightness - 1.0f;
    fG = (1.0f - brightness) * fTmpG + 2.0f * brightness - 1.0f;
    fB = (1.0f - brightness) * fTmpB + 2.0f * brightness - 1.0f;
  }

  // L68: NO output clamp — return the raw channel value.
  return (k == 0) ? fR : (k == 1) ? fG : fB;
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_hsltocolor{
    // HSLToColor (TiXL Lib.numbers.color.HSLToColor): HSL+Alpha → RGBA color.
    // Port order MUST match evalHslToColor's in[] read: 4 scalar inputs (Hue/Saturation/Lightness/
    // Alpha) first, then the 4 decomposed Color outputs (R/G/B/A) under one Widget::Vec head.
    // Defaults from HSLToColor.cs L72-82: Hue/Saturation/Lightness = 0, Alpha = 1 (L82).
    // PortSpec field order (graph.h:27):
    //   id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity, multiInput.
    {"HSLToColor", "HSLToColor",
     {{"Hue",        "Hue",        "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      {"Saturation", "Saturation", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      {"Lightness",  "Lightness",  "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      {"Alpha",      "Alpha",      "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"Color.x", "Color",   "Float", false, 0.0f, 0.0f, 0.0f, Widget::Vec, {}, false, 4},
      {"Color.y", "Color.y", "Float", false, 0.0f, 0.0f, 0.0f, Widget::Vec, {}, false, 1},
      {"Color.z", "Color.z", "Float", false, 0.0f, 0.0f, 0.0f, Widget::Vec, {}, false, 1},
      {"Color.w", "Color.w", "Float", false, 0.0f, 0.0f, 0.0f, Widget::Vec, {}, false, 1}},
     evalHslToColor},
    "hsltocolor", runHslToColorSelfTest};

// --- HSLToColor MATH golden (flat path — pulls each of the 4 output pins independently) ----------
// Builds a 1-node HSLToColor graph, sets the 4 scalar inputs, pulls each output pin (Color.x/.y/.z/.w
// = R/G/B/A) via evalFloat, and compares to hand-computed closed-form HSL values. Goldens are picked
// at saturated/boundary points (no fwidth/smoothstep drift). injectBug flips hue = (Hue%1)*360 to
// (Hue%360)*360 — at Hue=2 the faithful result is RED (red), the bug spirals to hue=720° → wildly
// different R/B, so the Hue=2 R-channel assertion flips RED, proving the % 1 tooth bites.
int runHslToColorSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate HSLToColor with explicit inputs, pulling a named output port (Color.x/.y/.z/.w).
  auto evalHsl = [&](float hue, float sat, float light, float alpha, const char* outName) -> float {
    const NodeSpec* spec = findSpec("HSLToColor");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "HSLToColor";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Hue"]        = hue;
    g.node(nid)->params["Saturation"] = sat;
    g.node(nid)->params["Lightness"]  = light;
    g.node(nid)->params["Alpha"]      = alpha;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // Convenience: assert one channel.
  auto check = [&](const char* tag, float got, float want) {
    bool pass = std::fabs(got - want) < eps;
    ok = ok && pass;
    printf("[selftest-hsltocolor] %s got=%.5f want=%.5f -> %s\n",
           tag, got, want, pass ? "PASS" : "FAIL");
  };

  // GOLDEN 1 (spec): Sat=0, L=0, Hue=0, A=1 → (0,0,0,1).
  //   hue=0 → fSatR=2→clamp1, fSatG=0, fSatB=0. fTmpC = 2*0*fSatC + 1 = 1 (all channels).
  //   brightness=0 < 0.5 → fC = 0*fTmpC = 0. Alpha passthrough = 1. → (0,0,0,1).
  check("G1 R(H0,S0,L0,A1)", evalHsl(0.0f, 0.0f, 0.0f, 1.0f, "Color.x"), 0.0f);
  check("G1 G",              evalHsl(0.0f, 0.0f, 0.0f, 1.0f, "Color.y"), 0.0f);
  check("G1 B",              evalHsl(0.0f, 0.0f, 0.0f, 1.0f, "Color.z"), 0.0f);
  check("G1 A",              evalHsl(0.0f, 0.0f, 0.0f, 1.0f, "Color.w"), 1.0f);

  // GOLDEN 2 (spec): Sat=1, L=0.5, Hue=0, A=1 → (1,0,0,1)  (pure red).
  //   hue=0 → fSatR=1, fSatG=0, fSatB=0. fTmpR = 2*1*1 + 0 = 2; fTmpG = 0; fTmpB = 0.
  //   brightness=0.5 → ELSE branch (>=0.5): fR = (1-0.5)*2 + 2*0.5 - 1 = 1+0 = 1; fG=0; fB=0.
  check("G2 R(H0,S1,L0.5,A1)", evalHsl(0.0f, 1.0f, 0.5f, 1.0f, "Color.x"), 1.0f);
  check("G2 G",                evalHsl(0.0f, 1.0f, 0.5f, 1.0f, "Color.y"), 0.0f);
  check("G2 B",                evalHsl(0.0f, 1.0f, 0.5f, 1.0f, "Color.z"), 0.0f);

  // GOLDEN 3 (hsl-fsat-clamp + 3-band): Hue=0.5 (→180°), Sat=1, L=0.5, A=1.
  //   hue = (0.5%1)*360 = 180. Band hue<240: fSatR=0, fSatG=(240-180)/60=1, fSatB=(180-120)/60=1.
  //   clamp: all <=1 already. fTmpR=2*1*0+0=0; fTmpG=2*1*1+0=2; fTmpB=2*1*1+0=2.
  //   L=0.5 ELSE: fG=(0.5)*2+0=1; fB=1; fR=0. → cyan (0,1,1). Proves G & B bands AND the <=1 clamp
  //   (fSatG/fSatB exactly 1, fSatR pinned to 0 not clamped).
  check("G3 R(H0.5,S1,L0.5)", evalHsl(0.5f, 1.0f, 0.5f, 1.0f, "Color.x"), 0.0f);
  check("G3 G",               evalHsl(0.5f, 1.0f, 0.5f, 1.0f, "Color.y"), 1.0f);
  check("G3 B",               evalHsl(0.5f, 1.0f, 0.5f, 1.0f, "Color.z"), 1.0f);

  // GOLDEN 4 (hsl-lightness-branch, brightness<0.5 path): Hue=0, Sat=1, L=0.25, A=1.
  //   fSatR=1,fSatG=0,fSatB=0. fTmpR=2; fTmpG=0; fTmpB=0.
  //   brightness=0.25 < 0.5 → fR = 0.25*2 = 0.5; fG=0; fB=0. Proves the < 0.5 branch (NOT the else).
  check("G4 R(H0,S1,L0.25)", evalHsl(0.0f, 1.0f, 0.25f, 1.0f, "Color.x"), 0.5f);
  check("G4 G",              evalHsl(0.0f, 1.0f, 0.25f, 1.0f, "Color.y"), 0.0f);

  // GOLDEN 5 (Alpha passthrough): Alpha=0.3 → Color.w = 0.3 (straight through, HSLToColor.cs L68).
  check("G5 A(A0.3)", evalHsl(0.0f, 0.0f, 0.0f, 0.3f, "Color.w"), 0.3f);

  // BOUNDARY (HSLToColor defaults: Hue/Sat/Light=0, Alpha=1): → (0,0,0,1) (== Golden 1).
  check("DEFAULT A", evalHsl(0.0f, 0.0f, 0.0f, 1.0f, "Color.w"), 1.0f);

  // injectBug GOLDEN (hsl-hue-mod1): Hue=2, Sat=1, L=0.5, A=1.
  //   FAITHFUL (% 1): hue = (2%1)*360 = 0 → same as Golden 2 → R=1 (pure red).
  //   BUG (% 360):    hue = (2%360)*360 = 720° → else-band (hue>=240): fSatR=(720-240)/60=8→clamp1,
  //                   fSatB=(360-720)/60=-6 (NO lower clamp). fTmpR = 2*1*1+0 = 2; fR (L>=0.5 else) =
  //                   (1-0.5)*2 + 0 = 1 ... R alone happens to still be 1, but fSatB=-6 →
  //                   fTmpB=2*1*(-6)+0=-12 → fB = 0.5*(-12)+0 = -6. So assert the BLUE channel: a
  //                   faithful Hue=2 reds to (1,0,0) → B=0; the % 360 bug drives B to -6 → RED.
  {
    float wantB = injectBug ? -6.0f : 0.0f;  // bug: hue=720° drives B to -6 (no lower fSat clamp)
    check("BUG B(H2,S1,L0.5)", evalHsl(2.0f, 1.0f, 0.5f, 1.0f, "Color.z"), wantB);
  }

  return ok ? 0 : 1;
}

}  // namespace sw
