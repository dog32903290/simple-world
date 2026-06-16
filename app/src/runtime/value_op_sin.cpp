// Sin value op (value-op self-registration seam leaf — first consumer).
// TiXL authority: Operators/Lib/numbers/float/trigonometry/Sin.cs (+ Sin.t3 defaults).
//
//   Sin.cs Update():
//     Result.Value = (float)Math.Sin(Input.GetValue(context) / Period.GetValue(context)
//                                     + Phase.GetValue(context))
//                    * Amplitude.GetValue(context)
//                    + Offset.GetValue(context);
//
//   Sin.t3 DefaultValues: Input=0, Period=1.0, Phase=0, Amplitude=1.0, Offset=0.
//   (Default eval: sin(0/1 + 0) * 1 + 0 = 0.)
//
// 5 inputs (Input/Period/Phase/Amplitude/Offset) → 1 output (Result). Pure stateless value op:
// behaviour is entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// NAMED FORK — fork-sin-period-zero:
//   TiXL has NO explicit Period==0 guard: in C#, `Input / 0f` is float division → ±Infinity
//   (or NaN for 0/0), and Math.Sin(Infinity) returns NaN. So a TiXL Sin with Period=0 emits NaN,
//   which would propagate through this runtime's float spine / inspector / Metal cbuffers as a
//   poison value. We guard Period==0 → return Offset (the degenerate "no oscillation" value:
//   sin(...)*Amplitude collapses, Offset is the DC term). This is a faithfulness fork on a
//   pathological input only; the default Period=1.0 and every non-zero Period is byte-identical
//   to TiXL. (Same spirit as the Div B==0 → 0 fork already shipped — avoid NaN propagation.)
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"        // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runSinSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: Input, Period, Phase, Amplitude, Offset.
// Result = sin(Input/Period + Phase) * Amplitude + Offset  (TiXL Sin.cs verbatim).
float evalSinOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  const float input = in[0], period = in[1], phase = in[2], amplitude = in[3], offset = in[4];
  if (period == 0.0f) return offset;  // fork-sin-period-zero (see header) — avoid NaN
  return std::sin(input / period + phase) * amplitude + offset;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_sin{
    // Sin (TiXL Lib.numbers.float.trigonometry.Sin): sin(Input/Period + Phase)*Amplitude + Offset.
    // Port order MUST match evalSinOp's in[] read: Input, Period, Phase, Amplitude, Offset, then out.
    // Defaults from Sin.t3: Period=1, Amplitude=1, the rest 0.
    {"Sin", "Sin",
     {{"Input", "Input", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      {"Period", "Period", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"Phase", "Phase", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      {"Amplitude", "Amplitude", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"Offset", "Offset", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalSinOp},
    "sin", runSinSelfTest};

// --- Sin MATH golden ---------------------------------------------------------------------------
// Builds a 1-node Sin graph, sets params, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL formula. injectBug swaps Period and Phase in the formula
// expectation so the typical-case assertion flips RED, proving the tooth bites.
int runSinSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate Sin with explicit params.
  auto evalSin = [&](float input, float period, float phase, float amplitude,
                     float offset) -> float {
    const NodeSpec* spec = findSpec("Sin");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Sin";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input"]     = input;
    g.node(nid)->params["Period"]    = period;
    g.node(nid)->params["Phase"]     = phase;
    g.node(nid)->params["Amplitude"] = amplitude;
    g.node(nid)->params["Offset"]    = offset;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Input=π/2, Period=1, Phase=0, Amplitude=2, Offset=1.
  //   sin(π/2 / 1 + 0) * 2 + 1 = sin(π/2)*2 + 1 = 1*2 + 1 = 3.
  // injectBug expectation: pretend the formula divided by Phase or added Period — assert the
  // WRONG value sin(π/2 + 1)*2 + 1 (Phase=1 instead of 0) → ≈ sin(2.5708)*2+1 ≈ 2.0806, not 3.
  {
    const float pi = 3.14159265358979f;
    float r = evalSin(pi / 2.0f, 1.0f, 0.0f, 2.0f, 1.0f);
    float want = injectBug ? (std::sin(pi / 2.0f + 1.0f) * 2.0f + 1.0f) : 3.0f;  // bug: Phase=1
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-sin] Sin(in=pi/2,T=1,ph=0,amp=2,off=1)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // TYPICAL: defaults except Input. Sin(0,1,0,1,0) = sin(0)*1+0 = 0.
  {
    float r = evalSin(0.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-sin] Sin(0,1,0,1,0)=%.6f want=0.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // PERIOD effect: Input=π, Period=2 → sin(π/2)=1, *1+0 = 1.
  {
    const float pi = 3.14159265358979f;
    float r = evalSin(pi, 2.0f, 0.0f, 1.0f, 0.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-sin] Sin(pi,T=2,...)=%.6f want=1.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY fork-sin-period-zero: Period=0 → return Offset (not NaN). Offset=7 → 7.
  {
    float r = evalSin(1.0f, 0.0f, 0.0f, 5.0f, 7.0f);
    bool pass = std::fabs(r - 7.0f) < eps && std::isfinite(r);
    ok = ok && pass;
    printf("[selftest-sin] Sin(1,T=0,...,off=7)=%.6f want=7.000000 (fork:period0->offset) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
