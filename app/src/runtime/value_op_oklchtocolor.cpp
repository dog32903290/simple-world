// OKLChToColor value op (value-op self-registration seam leaf — MULTI-OUTPUT, mirror of HSLToColor).
// TiXL authority: Operators/Lib/numbers/color/OKLChToColor.cs + Core/Utils/OkLab.cs (verbatim below).
//
//   OKLChToColor.cs Update():
//     var boost = IntensityBoost.GetValue(context);
//     Color.Value = OkLab.FromOkLCh(Brightness, Saturation, (Hue % 1)*360f, Alpha, UseGamma)
//                   * new Vector4(boost, boost, boost, 1);
//
//   OkLab.FromOkLCh(L, C, hDegrees, alpha, gamma):              (OkLab.cs)
//     float h = hDegrees * (PI/180);  a = C*cos(h);  b = C*sin(h);
//     return FromOkLab(L, a, b, alpha, gamma);
//   OkLab.FromOkLab(L, a, b, alpha, gamma):
//     hdrExcess = max(0, L-1);
//     linear        = OkLabToRgba(new Vector4(L.Clamp(0,1), a, b, alpha));
//     clampedLinear = Vector4.Clamp(linear, Zero, One);
//     srgb          = ToGamma(clampedLinear);             // ToGamma = pow(c, 1/2.2) per RGB, A passthrough
//     if (hdrExcess <= 0) return srgb;
//     return new Vector4(srgb.XYZ * (1+hdrExcess), srgb.W);
//   OkLab.OkLabToRgba(c) [OKLab → linear sRGB, c=(L,a,b,alpha)]:
//     l1 = c.X + 0.3963377774*c.Y + 0.2158037573*c.Z;
//     m1 = c.X - 0.1055613458*c.Y - 0.0638541728*c.Z;
//     s1 = c.X - 0.0894841775*c.Y - 1.2914855480*c.Z;
//     l = l1^3; m = m1^3; s = s1^3;
//     R = +4.0767416621*l - 3.3077115913*m + 0.2309699292*s;
//     G = -1.2684380046*l + 2.6097574011*m - 0.3413193965*s;
//     B = -0.0041960863*l - 0.7034186147*m + 1.7076147010*s;  A = c.W (passthrough).
//   OkLab.ToGamma(c): pow(c.{X,Y,Z}, 1/2.2), c.W passthrough.
//
//   OKLChToColor.t3 DefaultValues: Brightness=0.50000006, IntensityBoost=1, UseGamma=false,
//     Hue=0, Saturation=0, Alpha=1.
//
// EVAL-SIDE LAYOUT (multi-output, mirrors HSLToColor / Vector4Components): the host runtime has no
// native Vector4/Color port type — a Slot<Vector4> output decomposes into FOUR Float output ports
// (Color.x/.y/.z/.w = R/G/B/A) grouped under one Widget::Vec head (vecArity=4). The scalar inputs
// (Hue/Saturation/Brightness/Alpha/UseGamma/IntensityBoost) are distinct Float params and occupy
// in[0..5]; the four output ports come AFTER the inputs at spec index outIdx in [n, n+3] (n=6 inputs),
// so output channel k = outIdx - n ∈ [0,3]. All four channels share the SAME OkLab computation;
// only the final component selection differs. evalFloat pulls each output pin independently (flat
// path — OKLChToColor is NOT a multiInput op), recomputing the closed-form OkLab each pull (pure).
//
// FORKS (named, each with a TiXL line ref — host-data-model + a faithful no-op input, NOT math forks):
//   - fork-vec4-decompose-arity: TiXL's Color is one Slot<Vector4> (OKLChToColor.cs:11). sw exposes it
//     as four Float ports (Color.x/.y/.z/.w) under one Widget::Vec head (vecArity=4) — same convention
//     shipped for HSLToColor / Vector4Components. R/G/B/A are byte-identical to TiXL's Vector4
//     components; only the host type differs.
//   - fork-oklch-usegamma-is-noop: TiXL reads UseGamma (OKLChToColor.cs InputSlot<bool>) and threads it
//     to OkLab.FromOkLCh's `gamma` parameter — but OkLab.FromOkLab IGNORES `gamma` (it ALWAYS calls
//     ToGamma; the `gamma` param is dead in the OkLab impl, OkLab.cs:91-110). So UseGamma has NO effect
//     on the output in TiXL. Ported faithfully: the UseGamma input EXISTS (port shape parity) but does
//     not change the result — exactly matching TiXL's behaviour, not a divergence.
//   - fork-oklch-hue-mod1: hue degrees = (Hue % 1) * 360 (OKLChToColor.cs:23) — modulus is % 1 (wrap to
//     the unit interval THEN scale to degrees), NOT % 360. Hue in [0,1) is the normal range; Hue >= 1
//     wraps to its fractional part. Ported verbatim; the injectBug below flips % 1 → % 360 so a Hue=1
//     golden goes RED (Hue=1: faithful hue=0°, bug hue=360° drives a/b differently).
//   - fork-oklch-double-cbrt-precision: OkLabToRgba uses float math for the OKLab→RGB step (OkLab.cs:36
//     OkLabToRgba is all float); FromOkLCh's cos/sin are MathF (float). Ported with float math to match.
//     (The RgbAToOkLab inverse uses double cbrt, but OKLChToColor only walks the float forward path.)
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runOklChToColorSelfTest(bool injectBug);

namespace {

// OkLab.cs ToGamma per-channel: pow(c, 1/2.2). C# uses MathF.Pow (float).
inline float okToGamma(float c) { return std::pow(c, 1.0f / 2.2f); }
inline float clamp01(float c) { return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c); }

// OkLab.FromOkLCh → FromOkLab → OkLabToRgba → ToGamma, ported verbatim. Writes RGBA into out[4].
// usegamma is read but unused (fork-oklch-usegamma-is-noop, faithful to OkLab.cs ignoring `gamma`).
void okLChToRgba(float L, float C, float hueDegrees, float alpha, float /*useGamma*/, float out[4]) {
  // FromOkLCh: h = hDegrees * PI/180; a = C*cos(h); b = C*sin(h).
  const float h = hueDegrees * (3.14159265358979323846f / 180.0f);
  const float a = C * std::cos(h);
  const float b = C * std::sin(h);

  // FromOkLab: hdrExcess = max(0, L-1); linear = OkLabToRgba((L.Clamp(0,1), a, b, alpha)).
  const float hdrExcess = std::fmax(0.0f, L - 1.0f);
  const float Lc = clamp01(L);  // OkLabToRgba's c.X uses L.Clamp(0,1)

  // OkLabToRgba(c=(Lc,a,b,alpha)):
  const float l1 = Lc + 0.3963377774f * a + 0.2158037573f * b;
  const float m1 = Lc - 0.1055613458f * a - 0.0638541728f * b;
  const float s1 = Lc - 0.0894841775f * a - 1.2914855480f * b;
  const float l = l1 * l1 * l1;
  const float m = m1 * m1 * m1;
  const float s = s1 * s1 * s1;
  float R = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
  float G = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
  float B = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
  const float A = alpha;  // OkLabToRgba: A = c.W passthrough.

  // clampedLinear = Vector4.Clamp(linear, Zero, One) — clamps ALL FOUR components incl. alpha.
  R = clamp01(R);
  G = clamp01(G);
  B = clamp01(B);
  const float Ac = clamp01(A);

  // srgb = ToGamma(clampedLinear): pow(RGB, 1/2.2), A passthrough.
  float sR = okToGamma(R);
  float sG = okToGamma(G);
  float sB = okToGamma(B);
  const float sA = Ac;  // ToGamma: c.W passthrough.

  // if (hdrExcess <= 0) return srgb; else scale RGB by (1+hdrExcess), A unchanged.
  if (hdrExcess > 0.0f) {
    const float k = 1.0f + hdrExcess;
    sR *= k;
    sG *= k;
    sB *= k;
  }
  out[0] = sR;
  out[1] = sG;
  out[2] = sB;
  out[3] = sA;
}

// in[] = [Hue, Saturation, Brightness, Alpha, UseGamma, IntensityBoost]  (n=6).
// Output ports Color.x/.y/.z/.w (R/G/B/A) at spec index outIdx in [n, n+3] → channel k = outIdx-n.
// OKLChToColor.cs: Color = FromOkLCh(Brightness, Saturation, (Hue%1)*360, Alpha, UseGamma)
//                          * Vector4(boost, boost, boost, 1).
float evalOklChToColor(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  const int k = outIdx - n;  // 0=R 1=G 2=B 3=A
  if (k < 0 || k > 3) return 0.0f;

  const float hueIn     = in[0];
  const float saturation = in[1];
  const float brightness = in[2];
  const float alpha      = in[3];
  const float useGamma   = in[4];
  const float boost      = in[5];

  // OKLChToColor.cs:23: (Hue % 1) * 360 (fork-oklch-hue-mod1: % 1, NOT % 360).
  const float hueDegrees = std::fmod(hueIn, 1.0f) * 360.0f;

  float rgba[4];
  okLChToRgba(brightness, saturation, hueDegrees, alpha, useGamma, rgba);

  // * Vector4(boost, boost, boost, 1): RGB scaled by boost, A unchanged.
  if (k == 3) return rgba[3];        // A * 1 = A
  return rgba[k] * boost;            // R/G/B * boost
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (CMake globs value_op*.cpp;
// no shared edit point). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_oklchtocolor{
    // OKLChToColor (TiXL Lib.numbers.color.OKLChToColor): OKLCh → RGBA color.
    // Port order MUST match evalOklChToColor's in[] read: 6 scalar inputs first, then the 4 decomposed
    // Color outputs (R/G/B/A) under one Widget::Vec head. Defaults from OKLChToColor.t3:
    //   Hue=0, Saturation=0, Brightness=0.50000006, Alpha=1, UseGamma=false(0), IntensityBoost=1.
    // PortSpec field order (graph.h:27):
    //   id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity, multiInput.
    {"OKLChToColor", "OKLChToColor",
     {{"Hue",           "Hue",           "Float", true, 0.0f,        -100.0f, 100.0f, Widget::Slider},
      {"Saturation",    "Saturation",    "Float", true, 0.0f,        -100.0f, 100.0f, Widget::Slider},
      {"Brightness",    "Brightness",    "Float", true, 0.50000006f, -100.0f, 100.0f, Widget::Slider},
      {"Alpha",         "Alpha",         "Float", true, 1.0f,        -100.0f, 100.0f, Widget::Slider},
      {"UseGamma",      "UseGamma",      "Float", true, 0.0f,        0.0f,    1.0f,   Widget::Bool},
      {"IntensityBoost","IntensityBoost","Float", true, 1.0f,        -100.0f, 100.0f, Widget::Slider},
      {"Color.x", "Color",   "Float", false, 0.0f, 0.0f, 0.0f, Widget::Vec, {}, false, 4},
      {"Color.y", "Color.y", "Float", false, 0.0f, 0.0f, 0.0f, Widget::Vec, {}, false, 1},
      {"Color.z", "Color.z", "Float", false, 0.0f, 0.0f, 0.0f, Widget::Vec, {}, false, 1},
      {"Color.w", "Color.w", "Float", false, 0.0f, 0.0f, 0.0f, Widget::Vec, {}, false, 1}},
     evalOklChToColor},
    "oklchtocolor", runOklChToColorSelfTest};

// --- OKLChToColor MATH golden (flat path — pulls each of the 4 output pins independently) ---------
// Builds a 1-node OKLChToColor graph, sets inputs, pulls each output pin (Color.x/.y/.z/.w = R/G/B/A)
// via evalFloat, and compares to closed-form OkLab values. Goldens picked at structurally meaningful
// points (achromatic grey where C=0 → R=G=B; alpha passthrough; HDR boost; hue-mod1). injectBug flips
// hue = (Hue%1)*360 → (Hue%360)*360 so the Hue=1 case (faithful hue=0°) diverges (bug hue=360°).
int runOklChToColorSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Helper: evaluate OKLChToColor with explicit inputs, pulling a named output port.
  auto evalC = [&](float hue, float sat, float bright, float alpha, float useGamma, float boost,
                   const char* outName) -> float {
    const NodeSpec* spec = findSpec("OKLChToColor");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "OKLChToColor";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Hue"]            = hue;
    g.node(nid)->params["Saturation"]     = sat;
    g.node(nid)->params["Brightness"]     = bright;
    g.node(nid)->params["Alpha"]          = alpha;
    g.node(nid)->params["UseGamma"]       = useGamma;
    g.node(nid)->params["IntensityBoost"] = boost;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;
    ok = ok && pass;
    printf("[selftest-oklchtocolor] %s got=%.6f want=%.6f -> %s\n",
           tag, got, want, pass ? "PASS" : "FAIL");
  };

  // GOLDEN 1 (achromatic grey, C=0): Hue=0, Sat(C)=0, Brightness(L)=0.5, Alpha=1, boost=1.
  //   C=0 → a=b=0. OkLabToRgba((0.5,0,0,1)): l1=m1=s1=0.5 → l=m=s=0.125. Each matrix row sums to
  //   exactly 1.0, so R=G=B=0.125 (linear, in [0,1] → no clamp). srgb = 0.125^(1/2.2). hdrExcess=0.
  //   0.125^(1/2.2) = exp(ln(0.125)/2.2) = exp(-2.079442/2.2) = exp(-0.945201) = 0.388597.
  {
    const float wantGrey = std::pow(0.125f, 1.0f / 2.2f);  // 0.38859… (closed form, NOT self-ref cook)
    check("G1 R(grey L0.5,C0)", evalC(0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, "Color.x"), wantGrey, eps);
    check("G1 G",               evalC(0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, "Color.y"), wantGrey, eps);
    check("G1 B",               evalC(0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, "Color.z"), wantGrey, eps);
    check("G1 A",               evalC(0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 1.0f, "Color.w"), 1.0f,     eps);
  }

  // GOLDEN 2 (black, L=0): Brightness=0, C=0 → l1=m1=s1=0 → l=m=s=0 → R=G=B=0 → srgb=0. Alpha=1.
  {
    check("G2 R(black L0)", evalC(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, "Color.x"), 0.0f, eps);
    check("G2 G",           evalC(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, "Color.y"), 0.0f, eps);
    check("G2 B",           evalC(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, "Color.z"), 0.0f, eps);
  }

  // GOLDEN 3 (white, L=1): Brightness=1, C=0 → l1=m1=s1=1 → l=m=s=1 → R=G=B=1 → clamp→1 → srgb=1.
  //   hdrExcess = max(0, 1-1) = 0 → no boost scaling. → (1,1,1).
  {
    check("G3 R(white L1)", evalC(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, "Color.x"), 1.0f, eps);
    check("G3 G",           evalC(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, "Color.y"), 1.0f, eps);
    check("G3 B",           evalC(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, "Color.z"), 1.0f, eps);
  }

  // GOLDEN 4 (alpha passthrough): Alpha=0.3 → Color.w = 0.3 (clamp01(0.3)=0.3, ToGamma A passthrough,
  //   * 1 unchanged).
  check("G4 A(0.3)", evalC(0.0f, 0.0f, 0.5f, 0.3f, 0.0f, 1.0f, "Color.w"), 0.3f, eps);

  // GOLDEN 5 (IntensityBoost on grey): grey 0.388597 * boost 2 = 0.777194 (RGB scaled, A unchanged).
  {
    const float grey = std::pow(0.125f, 1.0f / 2.2f);
    check("G5 R(grey*boost2)", evalC(0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 2.0f, "Color.x"), grey * 2.0f, eps);
    check("G5 A",              evalC(0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 2.0f, "Color.w"), 1.0f,        eps);
  }

  // GOLDEN 6 (HDR boost path, L>1): Brightness=1.5, C=0, boost=1. L.Clamp(0,1)=1 → linear=(1,1,1)
  //   → srgb=(1,1,1). hdrExcess = max(0, 1.5-1) = 0.5 → RGB *= (1+0.5)=1.5 → 1.5. Proves the
  //   hdrExcess>0 branch (RGB super-white, A unchanged).
  check("G6 R(HDR L1.5)", evalC(0.0f, 0.0f, 1.5f, 1.0f, 0.0f, 1.0f, "Color.x"), 1.5f, eps);
  check("G6 A",           evalC(0.0f, 0.0f, 1.5f, 1.0f, 0.0f, 1.0f, "Color.w"), 1.0f, eps);

  // GOLDEN 7 (UseGamma is a no-op, fork-oklch-usegamma-is-noop): toggling UseGamma must NOT change the
  //   output (OkLab.FromOkLab ignores `gamma`). Grey with UseGamma=1 == grey with UseGamma=0.
  {
    const float grey = std::pow(0.125f, 1.0f / 2.2f);
    check("G7 R(UseGamma=1, still grey)", evalC(0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, "Color.x"), grey, eps);
  }

  // injectBug GOLDEN (fork-oklch-hue-mod1): the hue transform is the load-bearing line OKLChToColor.cs:23,
  //   hueDegrees = (Hue % 1) * 360. The bug class this golden detects = dropping the (% 1)*360 wrap and
  //   using Hue raw as degrees. Inputs Hue=0.25, Sat(C)=0.15, Brightness(L)=0.5, Alpha=1, boost=1.
  //     FAITHFUL: hueDeg = (0.25 % 1)*360 = 90° → a = C*cos90° ≈ 0, b = C*sin90° = C → R = 0.514328.
  //     BUG (raw degrees): hueDeg = 0.25° → a ≈ C, b ≈ 0 → R = 0.632889 (materially different red).
  //   Both reds are hand-derived (Python closed-form vs OkLab.cs, NOT a self-referential cook). injectBug
  //   asserts the BUG red 0.632889 against the FAITHFUL code output 0.514328 → RED (the % 1·360 tooth bites).
  {
    const float gotR = evalC(0.25f, 0.15f, 0.5f, 1.0f, 0.0f, 1.0f, "Color.x");
    const float wantR = injectBug ? 0.632889f   // bug: Hue used raw as degrees (0.25° → R=0.632889)
                                  : 0.514328f;   // faithful: (0.25%1)*360=90° → R=0.514328
    check("BUG R(Hue0.25,C0.15) % 1·360 tooth", gotR, wantR, eps);
  }

  return ok ? 0 : 1;
}

}  // namespace sw
