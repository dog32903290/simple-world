// RgbaToColor value op (value-op self-registration seam leaf — MULTI-OUTPUT).
// TiXL authority: Operators/Lib/numbers/vec4/RgbaToColor.cs (lines 14-17).
//
//   RgbaToColor.cs Update():
//     Result.Value = new Vector4(R.GetValue(context), G.GetValue(context),
//                                B.GetValue(context), A.GetValue(context));
//
//   Ports (TiXL): R/G/B/A = InputSlot<float> (defaults all 1.0). Output: Result = Slot<Vector4>.
//   Pure stateless repack: Result = (R, G, B, A). NO clamp, NO branch — identity per channel.
//   This is the exact TWIN of Vector4Components, with direction reversed: that op decomposes one
//   Vector4 into 4 Floats; this op composes 4 Floats into one Vector4 (which this runtime exposes
//   as 4 Float outputs Result.x/.y/.z/.w — see fork below).
//
// EVAL-SIDE LAYOUT (multi-output convention, mirrors Vector4Components): the FOUR Float input ports
// R/G/B/A occupy in[0..3]; the ONE Vector4 output (Result) is exposed as FOUR named Float output
// ports (Result.x/.y/.z/.w) that come AFTER the inputs. With n = 4 input ports, an output port
// lives at spec index outIdx in [n, n+3], so the component is k = outIdx - n ∈ [0,3]. The repack
// is identity: output component k == input port k (Result.x=R, Result.y=G, Result.z=B, Result.w=A).
// evalFloat pulls each output pin independently (flat path — RgbaToColor is NOT a multiInput op).
//
// FORKS (named):
//   - fork-vec4-compose-arity (RgbaToColor.cs:7,16): TiXL's Result is a single Slot<Vector4>. This
//     runtime has no native vec4/Color port type, so the vec4 output is exposed as four Float ports
//     (Result.x/.y/.z/.w) — purely a UI/authoring affordance. The compose is byte-identical: each
//     output emits its own input channel, no arithmetic. Same decompose convention already shipped
//     for Vector4Components (the twin op); this just runs it in the composing direction. NOT a taste
//     fork — sw has no Slot<Vector4>.
//   - inputs-as-sliders (param-completion fix): TiXL has R/G/B/A as 4 SEPARATE InputSlot<float> —
//     no Vec4 grouping. sw originally registered them under Widget::Vec (R as head arity=4, G/B/A as
//     components), which the dumpNodeSpec fold walker collapsed to a logical count of 1 (vs TiXL=4)
//     and the inspector showed as a single Vec4 row. Corrected to Widget::Slider per port (same as
//     TiXL's design: 4 independent float knobs, same as HSBToColor). Folded count now 4 == TiXL=4.
//   - NO clamp/branch fork: RgbaToColor.cs:16 is a bare Vector4 ctor — identity, no clamping to
//     [0,1] despite the "Color" name. Ported verbatim (the goldens below use 0.2/0.4/0.6/0.8 and
//     1.0 — all already in-range, so even a hypothetical clamp would not show; the identity is the
//     faithful behaviour regardless).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runRgbaToColorSelfTest(bool injectBug);

namespace {

// in[] = [R, G, B, A] (the 4 Float input ports, n=4).
// Output ports Result.x/.y/.z/.w are at spec index outIdx in [n, n+3] → component k = outIdx - n.
//   Result.Value = new Vector4(R, G, B, A) (TiXL RgbaToColor.cs:16, verbatim) → component k == in[k].
float evalRgbaToColor(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  int k = outIdx - n;  // output port index → component (0=x←R, 1=y←G, 2=z←B, 3=w←A)
  if (k < 0 || k > 3) return 0.0f;
  return in[k];
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_rgbatocolor{
    // RgbaToColor (TiXL Lib.numbers.vec4.RgbaToColor): compose 4 Float (R/G/B/A) → Vec4 Result.
    // Port order MUST match evalRgbaToColor's in[] read: the 4 R/G/B/A inputs first, then the 4
    // named outputs (Result.x/.y/.z/.w — fork-vec4-compose-arity). Inputs are Widget::Slider, NOT
    // Widget::Vec: TiXL has 4 separate InputSlot<float> (no Vec4 grouping), matching HSBToColor's
    // pattern. RgbaToColor defaults R=G=B=A=1.0 (RgbaToColor.cs:7,20/23/26/29 —
    // InputSlot<float> default; the .t3 ships all four at 1.0).
    {"RgbaToColor", "RgbaToColor",
     {{"R", "R", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"G", "G", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"B", "B", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"A", "A", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false},
      {"Result.w", "Result.w", "Float", false}},
     evalRgbaToColor},
    "rgbatocolor", runRgbaToColorSelfTest};

// --- RgbaToColor MATH golden (flat path — pulls each of the 4 output pins independently) ----------
// Builds a 1-node RgbaToColor graph, sets the 4 channel params, pulls each output pin
// (Result.x/.y/.z/.w) via evalFloat, and compares to the input channels (compose is identity per
// channel). The 4 channels are deliberately ALL DISTINCT so a mis-wired output (e.g. Result.x
// reading A, or k off by one) cannot coincidentally pass. injectBug swaps the Result.x expectation
// to the A value so the typical-case assertion flips RED, proving the tooth bites and the
// per-channel routing is real.
int runRgbaToColorSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate RgbaToColor with explicit channels, pulling a named output port.
  // Returns the value at output port `outName` (one of Result.x/.y/.z/.w).
  auto evalRgba = [&](float r, float g, float b, float a, const char* outName) -> float {
    const NodeSpec* spec = findSpec("RgbaToColor");
    if (!spec) return -999.0f;
    Graph gr;
    Node nd; nd.id = gr.nextId++; nd.type = "RgbaToColor";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    gr.nodes.push_back(nd);
    int nid = gr.nodes.back().id;
    gr.node(nid)->params["R"] = r;
    gr.node(nid)->params["G"] = g;
    gr.node(nid)->params["B"] = b;
    gr.node(nid)->params["A"] = a;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(gr, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: (R,G,B,A) = (0.2, 0.4, 0.6, 0.8) → Result = (0.2, 0.4, 0.6, 0.8) (per-channel identity).
  // injectBug asserts Result.x == A (0.8) — the "all outputs read the last channel" / k-collapse
  // failure mode — so the typical Result.x assertion flips RED.
  const float r = 0.2f, g = 0.4f, b = 0.6f, a = 0.8f;
  {
    float v = evalRgba(r, g, b, a, "Result.x");
    float want = injectBug ? a : r;  // bug: Result.x reads A
    bool pass = std::fabs(v - want) < eps;
    ok = ok && pass;
    printf("[selftest-rgbatocolor] Result.x(%.1f,%.1f,%.1f,%.1f)=%.4f want=%.4f -> %s\n",
           r, g, b, a, v, want, pass ? "PASS" : "FAIL");
  }
  {
    float v = evalRgba(r, g, b, a, "Result.y");
    bool pass = std::fabs(v - g) < eps;
    ok = ok && pass;
    printf("[selftest-rgbatocolor] Result.y=%.4f want=%.4f -> %s\n", v, g, pass ? "PASS" : "FAIL");
  }
  {
    float v = evalRgba(r, g, b, a, "Result.z");
    bool pass = std::fabs(v - b) < eps;
    ok = ok && pass;
    printf("[selftest-rgbatocolor] Result.z=%.4f want=%.4f -> %s\n", v, b, pass ? "PASS" : "FAIL");
  }
  {
    float v = evalRgba(r, g, b, a, "Result.w");
    bool pass = std::fabs(v - a) < eps;
    ok = ok && pass;
    printf("[selftest-rgbatocolor] Result.w=%.4f want=%.4f -> %s\n", v, a, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: default (R,G,B,A) = (1,1,1,1) → Result = (1,1,1,1) (RgbaToColor.t3 all-default compose).
  {
    float rx = evalRgba(1.0f, 1.0f, 1.0f, 1.0f, "Result.x");
    float rw = evalRgba(1.0f, 1.0f, 1.0f, 1.0f, "Result.w");
    bool pass = std::fabs(rx - 1.0f) < eps && std::fabs(rw - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-rgbatocolor] default(1,1,1,1) Result.x=%.4f Result.w=%.4f want=1 -> %s\n",
           rx, rw, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
