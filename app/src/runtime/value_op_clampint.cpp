// ClampInt value op (value-op self-registration seam leaf — parallel-weave consumer; independent
// .cpp, no shared edit point vs value_op_sin.cpp / value_op_pickfloat.cpp).
// TiXL authority: Operators/Lib/numbers/int/process/ClampInt.cs (+ Core/Utils/MathUtils.Clamp<T>).
//
//   ClampInt.cs Update():
//     var v   = Value.GetValue(context);   // int
//     var min = Min.GetValue(context);     // int
//     var max = Max.GetValue(context);     // int
//     Result.Value = MathUtils.Clamp(v, min, max);
//
//   MathUtils.Clamp<T>(v, min, max)  (MathUtils.cs:252):
//     => T.Min(T.Max(v, min), max);        // Max first, THEN Min — order is load-bearing
//
//   ClampInt.t3 DefaultValues: Value=0, Min=0, Max=0. (Default eval: Clamp(0,0,0) = 0.)
//
// 3 int inputs (Value/Min/Max) → 1 int output (Result). Pure stateless value op: behaviour is
// entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// FORKS (named):
//   - fork-clampint-float-port: TiXL Value/Min/Max/Result are int; this runtime has only Float
//     ports, so each input is read as Float and truncated to int via the C# `(int)` cast =
//     truncate-toward-zero (truncf), the exact convention the rest of this value spine uses for
//     int semantics (value_eval_ops.cpp Floor: "(float)(int)in[0]", PickFloat index). The output
//     is the int result re-widened to Float (every whole int representable exactly in float over
//     the operative range). This is faithful to C# int math for whole-number inputs — the only
//     inputs that make sense for an integer clamp.
//   - fork-clampint-min-gt-max (ASYMMETRIC, documented not changed): when Min > Max, TiXL's
//     Min(Max(v,min),max) makes `max` always win — the result is exactly `max` for every v
//     (since Max(v,min) >= min > max, then Min(...,max) = max). We reproduce this verbatim. NB:
//     this runtime's existing Float `Clamp` op (value_eval_ops.cpp evalClamp) does NOT — its
//     branch form `if(v<lo) return lo; ...` returns `lo` when v<lo<hi-inverted, diverging from
//     TiXL when min>max. ClampInt here is faithful to TiXL via the literal Min/Max composition.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <algorithm>  // std::min / std::max
#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runClampIntSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: Value, Min, Max.
// Result = MathUtils.Clamp<int>(Value, Min, Max) = Min(Max(Value, Min), Max), int-truncated.
// Max applied first then Min — order matters for the Min>Max asymmetric case (see header fork).
float evalClampInt(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const int v   = (int)in[0];  // fork-clampint-float-port: C# (int) cast = truncate toward zero
  const int min = (int)in[1];
  const int max = (int)in[2];
  return (float)std::min(std::max(v, min), max);  // T.Min(T.Max(v,min),max) verbatim
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched; distinct .cpp).
static const ValueOp _reg_clampint{
    // ClampInt (TiXL Lib.numbers.int.process.ClampInt): Min(Max(Value,Min),Max), int-truncated.
    // Port order MUST match evalClampInt's in[] read: Value, Min, Max, then out.
    // Defaults from ClampInt.t3: all 0. Slider widget (this runtime has no dedicated Int affordance;
    // int semantics live in evalClampInt's (int) cast — same as every other int-bearing Float port).
    {"ClampInt", "ClampInt",
     {{"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider},
      {"Min", "Min", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider},
      {"Max", "Max", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalClampInt},
    "clampint", runClampIntSelfTest};

// --- ClampInt MATH golden --------------------------------------------------------------------
// Builds a 1-node ClampInt graph, sets params, pulls "out" via evalFloat (math_ops_selftest /
// value_op_sin style), compares to the hand-derived TiXL formula Min(Max(v,min),max) over ints.
// injectBug swaps the in-range typical to the WRONG (low-clamped) expectation so the tooth bites.
int runClampIntSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate ClampInt with explicit params.
  auto evalCI = [&](float value, float min, float max) -> float {
    const NodeSpec* spec = findSpec("ClampInt");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "ClampInt";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value"] = value;
    g.node(nid)->params["Min"]   = min;
    g.node(nid)->params["Max"]   = max;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL in-range: Clamp(5, 0, 10) = Min(Max(5,0),10) = Min(5,10) = 5 (passes through).
  // injectBug expectation: pretend it low-clamped to Min (=0) — assert 0, which is the WRONG
  // value for an in-range input → flips the typical assertion RED, proving the tooth bites.
  {
    float r = evalCI(5.0f, 0.0f, 10.0f);
    float want = injectBug ? 0.0f : 5.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-clampint] ClampInt(5,0,10)=%.1f want=%.1f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // CLAMP-LOW boundary: Clamp(-3, 0, 10) = Min(Max(-3,0),10) = Min(0,10) = 0.
  {
    float r = evalCI(-3.0f, 0.0f, 10.0f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-clampint] ClampInt(-3,0,10)=%.1f want=0.0 (clamp-low) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // CLAMP-HIGH boundary: Clamp(42, 0, 10) = Min(Max(42,0),10) = Min(42,10) = 10.
  {
    float r = evalCI(42.0f, 0.0f, 10.0f);
    bool pass = std::fabs(r - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-clampint] ClampInt(42,0,10)=%.1f want=10.0 (clamp-high) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // INT TRUNCATION (fork-clampint-float-port): Clamp(7.9, 0, 10): (int)7.9 = 7 (toward zero),
  // Min(Max(7,0),10) = 7. Proves Float inputs are truncated to int, not rounded (round → 8).
  {
    float r = evalCI(7.9f, 0.0f, 10.0f);
    bool pass = std::fabs(r - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-clampint] ClampInt(7.9,0,10)=%.1f want=7.0 (trunc-toward-zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE TRUNCATION (toward zero, NOT floor): (int)(-2.7) = -2, clamp into [-5,5] → -2.
  {
    float r = evalCI(-2.7f, -5.0f, 5.0f);
    bool pass = std::fabs(r - (-2.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-clampint] ClampInt(-2.7,-5,5)=%.1f want=-2.0 (trunc-toward-zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // ASYMMETRIC fork-clampint-min-gt-max: Min=10 > Max=2. TiXL Min(Max(v,10),2): for ANY v the
  // inner Max >= 10 > 2, so the outer Min collapses to 2. Clamp(5,10,2) = Min(Max(5,10),2) =
  // Min(10,2) = 2. (A naive low-first clamp `if(v<min)return min` would wrongly return 10.)
  {
    float r = evalCI(5.0f, 10.0f, 2.0f);
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-clampint] ClampInt(5,10,2)=%.1f want=2.0 (min>max -> max wins) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // DEFAULTS: Clamp(0,0,0) = Min(Max(0,0),0) = 0 (the .t3 default eval).
  {
    float r = evalCI(0.0f, 0.0f, 0.0f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-clampint] ClampInt(0,0,0)=%.1f want=0.0 (defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
