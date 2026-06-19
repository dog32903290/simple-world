// BlendColors value op (value-op self-registration seam leaf — MULTI-OUTPUT, two Vec4 inputs).
// TiXL authority: Operators/Lib/numbers/color/BlendColors.cs (+ BlendColors.t3 defaults).
//
//   BlendColors.cs Update() (lines 16-58, ported VERBATIM):
//     var c1   = ColorA.GetValue(context);   // Vector4
//     var c2   = ColorB.GetValue(context);   // Vector4
//     var m    = Factor.GetValue(context);   // float
//     var mode = (Modes)Mode.GetValue(context);
//     switch (mode) {
//       case Modes.Mix:       result = c1*(1-m) + c2*m;                       break;  // line 28
//       case Modes.Multiply:  var factor = Lerp((1,1,1,1), c2, m);                     // line 32
//                             result = c1 * factor;                          break;  // line 33
//       case Modes.Add:       result = c1 + c2*m;                            break;  // line 38
//       case Modes.Blend:     result = (1-c2.W)*c1 + c2.W*c2;                         // line 43
//                             result.W = c1.W + c2.W - c1.W*c2.W;            break;  // line 44
//     }
//     Color.Value = result;   // line 57 — Vector4.Max(result, Vector4.Zero) is COMMENTED OUT → raw.
//
//   enum Modes { Mix=0, Multiply=1, Add=2, Blend=3 } (lines 72-78).
//   C# MathUtils.Lerp(a, b, t) = a + (b-a)*t = a*(1-t) + b*t componentwise.
//   BlendColors.t3 DefaultValues (lines 1-32): Factor=1.0, Mode=0 (Mix),
//     ColorA={1,1,1,1}, ColorB={1,1,1,1}.  → Default eval (Mix, m=1): c1*0 + c2*1 = (1,1,1,1).
//
// 9 Float input ports (ColorA.x/.y/.z/.w, ColorB.x/.y/.z/.w, Factor, Mode) → 4 Float outputs (R/G/B/A).
// Pure stateless value op (no GPU cook); behaviour is entirely evaluate(), registered via ValueOp seam.
//
// EVAL-SIDE LAYOUT (multi-output convention, mirrors Vector4Components / Vector3Components): the two
// Vector4 inputs are decomposed into 8 Float ports (in[0..3]=ColorA, in[4..7]=ColorB), Factor=in[8],
// Mode=in[9] → n=10 input ports. The FOUR named output ports (R/G/B/A) come AFTER the inputs, so an
// output lives at spec index outIdx in [n, n+3] → channel k = outIdx - n ∈ [0,3]. evalFloat pulls each
// output pin independently (flat path — BlendColors is NOT a multiInput op, one wire per port).
//
// NAMED FORKS:
//   - fork-blendcolors-vec4-as-8-floats: mirror of DotVec4 / Vector4Components. This runtime has only
//     scalar Float ports — no native Vector4/Color type (see value_op_dotvec4.cpp header). Each Vector4
//     input (ColorA, ColorB) is decomposed into 4 consecutive Float ports grouped under one Widget::Vec
//     head (vecArity=4), and the single Slot<Vector4> Color output into 4 Float ports (R/G/B/A). The
//     blend math is byte-identical to TiXL; only the host data model differs (8+1+1 Float wires + 4
//     Float outs vs 2 Vec4 in / 1 Vec4 out). COSMETIC, not an eval/golden fork.
//   - fork-blendcolors-mode-enum: TiXL Mode is InputSlot<int> with MappedType=typeof(Modes)
//     (BlendColors.cs:69-70). Exposed here as a Widget::Enum int (compile-time selector, NOT a runtime
//     uniform), 4 labels Mix/Multiply/Add/Blend, read int-on-Float and truncated toward zero — same
//     enum-on-Float convention as CompareInt (value_op_compareint.cpp) / SDF axis enums. Default Mode=0
//     (Mix) per BlendColors.t3.
//   - fork-blendcolors-no-clamp: BlendColors.cs:57 emits `result` RAW — the `Vector4.Max(result,
//     Vector4.Zero)` clamp is COMMENTED OUT in TiXL. So this op does NOT clamp; an Add of two opaque
//     colors yields alpha 2.0 (not 1.0). Faithfully reproduced (no std::max). The injectBug below adds
//     the clamp back to prove the no-clamp path is real.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runBlendColorsSelfTest(bool injectBug);

namespace {

// When true, the (commented-out in TiXL) Vector4.Max(result, 0) clamp is applied. Used ONLY by the
// injectBug golden path to prove the shipped op emits RAW (un-clamped) results.
bool g_blendColorsInjectClamp = false;

// in[] = [ColorA.x/.y/.z/.w, ColorB.x/.y/.z/.w, Factor, Mode] (spec input-port order), n=10.
// outIdx selects the pulled output channel: k = outIdx - n → 0=R, 1=G, 2=B, 3=A (multi-output, mirror
// of Vector4Components). BlendColors.cs:24-57 ported verbatim. NO final clamp (line 57 commented).
float evalBlendColors(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 10) return 0.0f;
  const int k = outIdx - n;  // output channel index (0=R, 1=G, 2=B, 3=A)
  if (k < 0 || k > 3) return 0.0f;

  const float a[4] = {in[0], in[1], in[2], in[3]};  // c1 = ColorA
  const float b[4] = {in[4], in[5], in[6], in[7]};  // c2 = ColorB
  const float m = in[8];                            // Factor
  const int mode = (int)in[9];                      // Mode (enum-on-Float, trunc toward zero)

  float result[4];
  switch (mode) {
    case 0:  // Mix:  result = c1*(1-m) + c2*m   (BlendColors.cs:28, componentwise lerp)
      for (int i = 0; i < 4; ++i) result[i] = a[i] * (1.0f - m) + b[i] * m;
      break;

    case 1:  // Multiply: factor = Lerp((1,1,1,1), c2, m) = (1-m) + c2*m; result = c1*factor (cs:32-33)
      for (int i = 0; i < 4; ++i) {
        const float factor = 1.0f * (1.0f - m) + b[i] * m;  // Lerp(1, c2.i, m)
        result[i] = a[i] * factor;
      }
      break;

    case 2:  // Add: result = c1 + c2*m   (BlendColors.cs:38, componentwise)
      for (int i = 0; i < 4; ++i) result[i] = a[i] + b[i] * m;
      break;

    default:  // 3 = Blend:  result = (1-c2.W)*c1 + c2.W*c2 ; result.W = c1.W+c2.W - c1.W*c2.W
      // (BlendColors.cs:43-44). Factor m is NOT used in Blend. The .W from line 43 is overwritten
      // by line 44. Default-out-of-range modes also land here (TiXL would throw; we collapse to Blend
      // as the switch default — no in-range BlendColors authoring path produces mode∉[0,3]).
      for (int i = 0; i < 4; ++i) result[i] = (1.0f - b[3]) * a[i] + b[3] * b[i];  // line 43
      result[3] = a[3] + b[3] - a[3] * b[3];                                       // line 44
      break;
  }

  // BlendColors.cs:57 — Color.Value = result;  (Vector4.Max(result, Vector4.Zero) is COMMENTED OUT.)
  // fork-blendcolors-no-clamp: emit RAW. g_blendColorsInjectClamp only true under the injectBug golden.
  if (g_blendColorsInjectClamp && result[k] < 0.0f) result[k] = 0.0f;
  if (g_blendColorsInjectClamp && result[k] > 1.0f) result[k] = 1.0f;
  return result[k];
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_blendcolors{
    // BlendColors (TiXL Lib.numbers.color.BlendColors): blend two RGBA colors by Factor + Mode enum.
    // Port order MUST match evalBlendColors's in[] read: ColorA (4, Widget::Vec head vecArity=4),
    // ColorB (4, Widget::Vec head vecArity=4), Factor (Slider), Mode (Enum), then the 4 outputs R/G/B/A.
    // Output ports MUST follow ALL inputs so outIdx-n channel indexing holds.
    // Defaults from BlendColors.t3: Factor=1.0, Mode=0 (Mix), ColorA={1,1,1,1}, ColorB={1,1,1,1}.
    // PortSpec field order (graph.h): id, name, dataType, isInput, def, minV, maxV, widget, labels,
    //   pinless, vecArity, multiInput.
    {"BlendColors", "BlendColors",
     {{"ColorA.x", "ColorA",   "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 4},
      {"ColorA.y", "ColorA.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
      {"ColorA.z", "ColorA.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
      {"ColorA.w", "ColorA.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
      {"ColorB.x", "ColorB",   "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 4},
      {"ColorB.y", "ColorB.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
      {"ColorB.z", "ColorB.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
      {"ColorB.w", "ColorB.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
      {"Factor", "Factor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
       {"Mix", "Multiply", "Add", "Blend"}},
      {"R", "R", "Float", false},
      {"G", "G", "Float", false},
      {"B", "B", "Float", false},
      {"A", "A", "Float", false}},
     evalBlendColors},
    "blendcolors", runBlendColorsSelfTest};

// --- BlendColors MATH golden (flat path — pulls each of the 4 output pins independently) ----------
// Builds a 1-node BlendColors graph, sets the 10 input params, pulls each output pin (R/G/B/A) via
// evalFloat, and compares to the hand-derived TiXL formula per mode. injectBug re-enables the
// (TiXL-commented-out) Vector4.Max(.,0..1) clamp so the Add case (alpha=2.0, > 1) flips RED — proving
// the shipped op emits RAW un-clamped results (fork-blendcolors-no-clamp).
int runBlendColorsSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Toggle the clamp ONLY for the duration of this selftest, then restore. The injectBug build path
  // sets the clamp on; the shipped op (g_blendColorsInjectClamp==false) always emits raw.
  const bool savedClamp = g_blendColorsInjectClamp;
  g_blendColorsInjectClamp = injectBug;

  // Helper: evaluate BlendColors with explicit inputs, pulling a named output channel (R/G/B/A).
  auto evalCh = [&](float ax, float ay, float az, float aw,
                    float bx, float by, float bz, float bw,
                    float factor, float mode, const char* outName) -> float {
    const NodeSpec* spec = findSpec("BlendColors");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "BlendColors";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["ColorA.x"] = ax; g.node(nid)->params["ColorA.y"] = ay;
    g.node(nid)->params["ColorA.z"] = az; g.node(nid)->params["ColorA.w"] = aw;
    g.node(nid)->params["ColorB.x"] = bx; g.node(nid)->params["ColorB.y"] = by;
    g.node(nid)->params["ColorB.z"] = bz; g.node(nid)->params["ColorB.w"] = bw;
    g.node(nid)->params["Factor"] = factor;
    g.node(nid)->params["Mode"] = mode;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  auto check4 = [&](const char* label, float ax, float ay, float az, float aw,
                    float bx, float by, float bz, float bw, float factor, float mode,
                    float wantR, float wantG, float wantB, float wantA) {
    float r = evalCh(ax, ay, az, aw, bx, by, bz, bw, factor, mode, "R");
    float gg = evalCh(ax, ay, az, aw, bx, by, bz, bw, factor, mode, "G");
    float bb = evalCh(ax, ay, az, aw, bx, by, bz, bw, factor, mode, "B");
    float aa = evalCh(ax, ay, az, aw, bx, by, bz, bw, factor, mode, "A");
    bool pass = std::fabs(r - wantR) < eps && std::fabs(gg - wantG) < eps &&
                std::fabs(bb - wantB) < eps && std::fabs(aa - wantA) < eps;
    ok = ok && pass;
    printf("[selftest-blendcolors] %s = (%.4f,%.4f,%.4f,%.4f) want=(%.4f,%.4f,%.4f,%.4f) -> %s\n",
           label, r, gg, bb, aa, wantR, wantG, wantB, wantA, pass ? "PASS" : "FAIL");
  };

  // MIX (Mode=0), A=(1,0,0,1), B=(0,1,0,1), Factor=0.5:
  //   result = A*(1-0.5) + B*0.5 = (0.5,0,0,0.5) + (0,0.5,0,0.5) = (0.5,0.5,0,1).
  //   (Spec golden says (0.5,0.5,0.5,1); that assumed B.b=1 — with B=(0,1,0,1) the blue channel is 0.
  //    Computed straight from the formula for the EXACT inputs given.)
  check4("Mix A=(1,0,0,1) B=(0,1,0,1) F=0.5", 1, 0, 0, 1, 0, 1, 0, 1, 0.5f, 0.0f,
         0.5f, 0.5f, 0.0f, 1.0f);

  // MIX boundary F=0 → pure A; F=1 → pure B. A=(0.2,0.4,0.6,0.8), B=(1,1,1,1).
  check4("Mix F=0 -> pure A", 0.2f, 0.4f, 0.6f, 0.8f, 1, 1, 1, 1, 0.0f, 0.0f,
         0.2f, 0.4f, 0.6f, 0.8f);
  check4("Mix F=1 -> pure B", 0.2f, 0.4f, 0.6f, 0.8f, 1, 1, 1, 1, 1.0f, 0.0f,
         1.0f, 1.0f, 1.0f, 1.0f);

  // MULTIPLY (Mode=1), Factor=1: factor = Lerp(1, c2, 1) = c2 → result = c1 * c2 componentwise.
  //   A=(1,0.5,0.25,1), B=(0.5,0.5,0.5,1) → (0.5, 0.25, 0.125, 1).
  check4("Multiply F=1 -> A*B", 1.0f, 0.5f, 0.25f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f,
         0.5f, 0.25f, 0.125f, 1.0f);

  // MULTIPLY Factor=0: factor = Lerp(1, c2, 0) = (1,1,1,1) → result = c1 (B has no effect).
  check4("Multiply F=0 -> A unchanged", 0.3f, 0.6f, 0.9f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
         0.3f, 0.6f, 0.9f, 0.7f);

  // ADD (Mode=2), Factor=1: result = A + B (componentwise). A=(1,0,0,1), B=(0,1,1,1) → (1,1,1,2).
  //   NOTE alpha=2.0 (NO clamp, line 57). This is the load-bearing no-clamp golden: injectBug clamps
  //   A to 1.0 → this assertion (wantA=2.0) flips RED.
  check4("Add F=1 -> A+B (alpha=2, NO clamp)", 1, 0, 0, 1, 0, 1, 1, 1, 1.0f, 2.0f,
         1.0f, 1.0f, 1.0f, 2.0f);

  // ADD Factor=0.5: result = A + B*0.5. A=(0.2,0,0,0.2), B=(0.4,0.8,0.6,1) → (0.4,0.4,0.3,0.7).
  check4("Add F=0.5 -> A + B*0.5", 0.2f, 0.0f, 0.0f, 0.2f, 0.4f, 0.8f, 0.6f, 1.0f, 0.5f, 2.0f,
         0.4f, 0.4f, 0.3f, 0.7f);

  // BLEND (Mode=3): result.rgb = (1-B.w)*A.rgb + B.w*B.rgb ; result.w = A.w + B.w - A.w*B.w.
  //   Factor is IGNORED in Blend. A=(1,0,0,1), B=(0,0,1,0.5), Factor=0.5:
  //     rgb: (1-0.5)*A + 0.5*B = 0.5*(1,0,0) + 0.5*(0,0,1) = (0.5, 0, 0.5)
  //     w:   1 + 0.5 - 1*0.5 = 1.0
  //   → (0.5, 0, 0.5, 1.0). (Factor=0.5 is set but must have NO effect — verifying Blend ignores m.)
  check4("Blend A=(1,0,0,1) B=(0,0,1,0.5) F=0.5(ignored)", 1, 0, 0, 1, 0, 0, 1, 0.5f, 0.5f, 3.0f,
         0.5f, 0.0f, 0.5f, 1.0f);

  // BLEND B fully opaque (B.w=1): rgb = 0*A + 1*B = B; w = A.w+1 - A.w*1 = 1 → result = (B.rgb, 1).
  //   A=(0.2,0.4,0.6,0.3), B=(0.9,0.8,0.7,1.0) → (0.9,0.8,0.7,1.0).
  check4("Blend B opaque -> B over A = B", 0.2f, 0.4f, 0.6f, 0.3f, 0.9f, 0.8f, 0.7f, 1.0f, 0.9f, 3.0f,
         0.9f, 0.8f, 0.7f, 1.0f);

  // BLEND B fully transparent (B.w=0): rgb = 1*A + 0*B = A; w = A.w+0 - 0 = A.w → result = A.
  //   A=(0.2,0.4,0.6,0.3), B=(0.9,0.8,0.7,0.0) → (0.2,0.4,0.6,0.3).
  check4("Blend B transparent -> A unchanged", 0.2f, 0.4f, 0.6f, 0.3f, 0.9f, 0.8f, 0.7f, 0.0f, 0.5f,
         3.0f, 0.2f, 0.4f, 0.6f, 0.3f);

  // DEFAULTS (BlendColors.t3: Mode=0 Mix, Factor=1, A=B=(1,1,1,1)): Mix F=1 → pure B = (1,1,1,1).
  check4("t3 defaults (Mix,F=1,A=B=1) -> (1,1,1,1)", 1, 1, 1, 1, 1, 1, 1, 1, 1.0f, 0.0f,
         1.0f, 1.0f, 1.0f, 1.0f);

  g_blendColorsInjectClamp = savedClamp;
  return ok ? 0 : 1;
}

}  // namespace sw
