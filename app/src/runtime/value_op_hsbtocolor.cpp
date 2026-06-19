// HSBToColor value op (value-op self-registration seam leaf — MULTI-OUTPUT).
// TiXL authority: Operators/Lib/numbers/color/HSBToColor.cs (Guid 13b08a56-…), lines 14-78.
//
//   HSBToColor.cs Update():
//     var hue        = (Hue.GetValue(context) % 360f);            // .cs:16
//     var saturation = Saturation.GetValue(context);             // .cs:17
//     var brightness = Brightness.GetValue(context);             // .cs:18
//     float r = 0, g = 0, b = 0;                                 // .cs:21
//     if (saturation == 0) { r = g = b = brightness; }           // .cs:23-27  (grayscale)
//     else {
//       hue %= 360f; if (hue < 0) hue += 360f;                   // .cs:31-32  (normalize to [0,360))
//       int   sector     = (int)(hue / 60f);                     // .cs:35     (truncate-toward-zero)
//       float fractional = (hue / 60f) - sector;                 // .cs:36
//       float p = brightness * (1 - saturation);                 // .cs:38
//       float q = brightness * (1 - saturation * fractional);    // .cs:39
//       float t = brightness * (1 - saturation * (1 - fractional)); // .cs:40
//       switch (sector) {                                        // .cs:42-74  (6-sector verbatim below)
//         0: r=brightness; g=t;          b=p;
//         1: r=q;          g=brightness; b=p;
//         2: r=p;          g=brightness; b=t;
//         3: r=p;          g=q;          b=brightness;
//         4: r=t;          g=p;          b=brightness;
//         5: r=brightness; g=p;          b=q;
//       }
//     }
//     Color.Value = new Vector4(r, g, b, Alpha.GetValue(context)); // .cs:77
//
//   Ports: Hue/Saturation/Brightness/Alpha = 4 InputSlot<float> (.cs:80-90). Output: Color =
//   Slot<Vector4> (.cs:6-7).
//
// EVAL-SIDE LAYOUT (multi-output convention, mirrors Vector4Components):
//   The FOUR scalar Float inputs occupy in[0..3] = [Hue, Saturation, Brightness, Alpha] (these are
//   genuinely four independent named Float slots in TiXL — NOT a packed vec, so they ride the plain
//   Widget::Slider, one canvas pin each). The single Vector4 output Color is decomposed into FOUR
//   named Float output ports (R/G/B/A) grouped under one Widget::Vec head (vecArity=4 — see fork
//   below). With n = 4 input ports, an output port lives at spec index outIdx in [n, n+3], so the
//   channel is k = outIdx - n ∈ [0,3] (0=R, 1=G, 2=B, 3=A). evalFloat pulls each output pin
//   independently (flat path — HSBToColor is NOT a multiInput op); the 6-sector switch is recomputed
//   per pulled channel from the same 4 inputs, so every channel is byte-consistent.
//
// FORKS (named):
//   - fork-hsb-sector-switch: the 6-sector HSB→RGB switch is ported byte-for-byte from HSBToColor.cs
//     lines 23-74 — same `saturation == 0` grayscale short-circuit, same `(int)(hue/60f)` truncating
//     sector, same p/q/t terms, same per-sector r/g/b assignments. No reordering, no clamping, no
//     fast-path substitution.
//   - fork-hsb-no-output-clamp: TiXL emits `new Vector4(r,g,b,Alpha)` raw (.cs:77) — there is NO
//     Vector4.Max / saturate / clamp on the result. With Brightness>1 or Brightness<0 the channels
//     run past [0,1] verbatim. This leaf likewise emits raw (no clamp), matching TiXL exactly.
//   - fork-vec4-decompose-arity: TiXL's Color is a single Vector4 slot; this runtime has only scalar
//     Float ports, so the vec4 output is exposed as four Float ports (R/G/B/A) grouped under one
//     Widget::Vec head (vecArity=4) — purely a UI/authoring affordance, byte-identical per channel.
//     Same convention shipped for Vector4Components.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runHSBToColorSelfTest(bool injectBug);

namespace {

// in[] = [Hue, Saturation, Brightness, Alpha] (the 4 scalar Float input ports, n=4).
// Output ports R/G/B/A are at spec index outIdx in [n, n+3] → channel k = outIdx - n.
// The full HSBToColor.cs switch is recomputed here and the requested channel is returned.
float evalHSBToColor(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  int k = outIdx - n;  // output port index → channel (0=R, 1=G, 2=B, 3=A)
  if (k < 0 || k > 3) return 0.0f;

  float hue = std::fmod(in[0], 360.0f);  // .cs:16  Hue % 360f
  float saturation = in[1];              // .cs:17
  float brightness = in[2];              // .cs:18
  float alpha = in[3];                   // .cs:77  (Alpha.GetValue)

  if (k == 3) return alpha;  // A channel is the raw Alpha input regardless of the HSB branch.

  float r = 0.0f, g = 0.0f, b = 0.0f;  // .cs:21

  if (saturation == 0.0f) {
    // Grayscale (.cs:23-27): r = g = b = brightness.
    r = g = b = brightness;
  } else {
    // Normalize hue to [0, 360) (.cs:31-32).
    hue = std::fmod(hue, 360.0f);
    if (hue < 0.0f) hue += 360.0f;

    int sector = (int)(hue / 60.0f);                  // .cs:35  (int) truncates toward zero
    float fractional = (hue / 60.0f) - (float)sector;  // .cs:36

    float p = brightness * (1.0f - saturation);                       // .cs:38
    float q = brightness * (1.0f - saturation * fractional);          // .cs:39
    float t = brightness * (1.0f - saturation * (1.0f - fractional)); // .cs:40

    switch (sector) {       // .cs:42-74 (verbatim — fork-hsb-sector-switch)
      case 0:               // 0° - 60°
        r = brightness; g = t; b = p;
        break;
      case 1:               // 60° - 120°
        r = q; g = brightness; b = p;
        break;
      case 2:               // 120° - 180°
        r = p; g = brightness; b = t;
        break;
      case 3:               // 180° - 240°
        r = p; g = q; b = brightness;
        break;
      case 4:               // 240° - 300°
        r = t; g = p; b = brightness;
        break;
      case 5:               // 300° - 360°
        r = brightness; g = p; b = q;
        break;
    }
  }

  // .cs:77 — Color = (r, g, b, Alpha), raw, NO clamp (fork-hsb-no-output-clamp).
  return k == 0 ? r : (k == 1 ? g : b);
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_hsbtocolor{
    // HSBToColor (TiXL Lib.numbers.color.HSBToColor): 4 Float inputs (Hue/Saturation/Brightness/
    // Alpha) → vec4 Color decomposed into R/G/B/A Float outputs. Port order MUST match
    // evalHSBToColor's in[] read: the 4 scalar inputs first, then the 4 named outputs (grouped under
    // one Widget::Vec head, vecArity=4 — fork-vec4-decompose-arity).
    // TiXL defaults are the InputSlot<float> defaults (0). min/max are inspector affordances only
    // (no clamp on eval): Hue spans the hue wheel, Sat/Bri/Alpha span [0,1].
    {"HSBToColor", "HSBToColor",
     {{"Hue", "Hue", "Float", true, 0.0f, 0.0f, 360.0f, Widget::Slider, {}, false, 1},
      {"Saturation", "Saturation", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1},
      {"Brightness", "Brightness", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1},
      {"Alpha", "Alpha", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1},
      {"R", "R", "Float", false},
      {"G", "G", "Float", false},
      {"B", "B", "Float", false},
      {"A", "A", "Float", false}},
     evalHSBToColor},
    "hsbtocolor", runHSBToColorSelfTest};

// --- HSBToColor MATH golden (flat path — pulls each of the 4 output pins independently) ----------
// Builds a 1-node HSBToColor graph, sets the 4 input params, pulls each output pin (R/G/B/A) via
// evalFloat, and compares to closed-form hand-computed RGBA. Goldens chosen to exercise BOTH HSB
// branches and three distinct sectors so a corrupted sector case cannot coincidentally pass.
//   G1 grayscale:  Sat=0, Bri=0.5, A=1                  → (0.5, 0.5, 0.5, 1.0)   [.cs:23-27 branch]
//   G2 sector0:    Hue=0,   Sat=1, Bri=1, A=1           → (1, 0, 0, 1)
//                  sector=(int)(0/60)=0, frac=0; p=0, q=1, t=0 → r=Bri=1, g=t=0, b=p=0
//   G3 sector2:    Hue=120, Sat=1, Bri=1, A=1           → (0, 1, 0, 1)
//                  sector=(int)(120/60)=2, frac=0; p=0, t=0 → r=p=0, g=Bri=1, b=t=0
// injectBug corrupts the sector-2 expectation (asserts G3 green channel == 0 instead of 1 — the
// "case 2 dropped g=brightness" failure mode), flipping the G3 assertion RED so the per-sector
// routing tooth is proven to bite.
int runHSBToColorSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate HSBToColor with explicit inputs, pulling a named output port (R/G/B/A).
  auto evalChan = [&](float hue, float sat, float bri, float alpha, const char* outName) -> float {
    const NodeSpec* spec = findSpec("HSBToColor");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "HSBToColor";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Hue"] = hue;
    g.node(nid)->params["Saturation"] = sat;
    g.node(nid)->params["Brightness"] = bri;
    g.node(nid)->params["Alpha"] = alpha;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // G1 grayscale: Sat=0, Bri=0.5, A=1 → (0.5, 0.5, 0.5, 1.0).
  {
    float r = evalChan(0.0f, 0.0f, 0.5f, 1.0f, "R");
    float gg = evalChan(0.0f, 0.0f, 0.5f, 1.0f, "G");
    float b = evalChan(0.0f, 0.0f, 0.5f, 1.0f, "B");
    float a = evalChan(0.0f, 0.0f, 0.5f, 1.0f, "A");
    bool pass = std::fabs(r - 0.5f) < eps && std::fabs(gg - 0.5f) < eps &&
                std::fabs(b - 0.5f) < eps && std::fabs(a - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-hsbtocolor] G1 grayscale Sat0 Bri.5 -> (%.4f,%.4f,%.4f,%.4f) want(.5,.5,.5,1) -> %s\n",
           r, gg, b, a, pass ? "PASS" : "FAIL");
  }

  // G2 sector0: Hue=0, Sat=1, Bri=1, A=1 → (1, 0, 0, 1).
  {
    float r = evalChan(0.0f, 1.0f, 1.0f, 1.0f, "R");
    float gg = evalChan(0.0f, 1.0f, 1.0f, 1.0f, "G");
    float b = evalChan(0.0f, 1.0f, 1.0f, 1.0f, "B");
    float a = evalChan(0.0f, 1.0f, 1.0f, 1.0f, "A");
    bool pass = std::fabs(r - 1.0f) < eps && std::fabs(gg - 0.0f) < eps &&
                std::fabs(b - 0.0f) < eps && std::fabs(a - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-hsbtocolor] G2 sector0 Hue0 -> (%.4f,%.4f,%.4f,%.4f) want(1,0,0,1) -> %s\n",
           r, gg, b, a, pass ? "PASS" : "FAIL");
  }

  // G3 sector2: Hue=120, Sat=1, Bri=1, A=1 → (0, 1, 0, 1). injectBug corrupts the green expectation.
  {
    float r = evalChan(120.0f, 1.0f, 1.0f, 1.0f, "R");
    float gg = evalChan(120.0f, 1.0f, 1.0f, 1.0f, "G");
    float b = evalChan(120.0f, 1.0f, 1.0f, 1.0f, "B");
    float a = evalChan(120.0f, 1.0f, 1.0f, 1.0f, "A");
    float wantG = injectBug ? 0.0f : 1.0f;  // bug: case 2 drops g=brightness
    bool pass = std::fabs(r - 0.0f) < eps && std::fabs(gg - wantG) < eps &&
                std::fabs(b - 0.0f) < eps && std::fabs(a - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-hsbtocolor] G3 sector2 Hue120 -> (%.4f,%.4f,%.4f,%.4f) wantG=%.1f -> %s\n",
           r, gg, b, a, wantG, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
