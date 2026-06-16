// FloatToInt value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/int/process/FloatToInt.cs.
//
//   FloatToInt.cs Update():
//     Integer.Value = (int)FloatValue.GetValue(context);   // C# (int) cast: truncate toward zero
//
//   Ports: FloatValue = InputSlot<float> (default 0); Output: Integer = Slot<int>.
//   (No .t3 default override needed — a fresh InputSlot<float> defaults to 0; eval: (int)0 = 0.)
//
// 1 Float input (FloatValue) -> 1 int output (Integer). Pure stateless value op: behaviour is
// entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// FORKS (named):
//   - fork-floattoint-trunc-toward-zero: C# `(int)x` truncates toward zero (NOT floor toward
//     -inf, NOT round to nearest). C++ `static_cast<int>` has the identical truncate-toward-zero
//     semantics, so this is byte-identical, not a behavioural fork — but it IS load-bearing on
//     negative inputs and must be asserted: (int)-0.9 = 0 (not -1), (int)-1.0 = -1, (int)2.7 = 2.
//     This matches the convention already shipped for Floor.cs (value_eval_ops.cpp:79 evalFloor:
//     `(float)(int)in[0]`, documented there as "C# (int) cast = truncf, NOT floor-toward-neg-inf").
//   - fork-floattoint-int-on-float-port: TiXL Integer output is `int`. This runtime has only Float
//     value ports, so the truncated int is emitted as a Float ((float)(int)x). For every value
//     representable as an exact int within the Float mantissa (2^24) this is byte-identical to the
//     C# int; this is the same single-Float-spine limitation every existing int-typed value op
//     shares (cf. AddInts fork-addints-float-mantissa), not a behavioural choice. In the practical
//     range it never bites.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runFloatToIntSelfTest(bool injectBug);

namespace {

// in[] order = the single Float input port: FloatValue.
// Result = (int)FloatValue, emitted as Float  (TiXL FloatToInt.cs verbatim, C# (int) truncate).
float evalFloatToIntOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;
  // fork-floattoint-trunc-toward-zero: static_cast<int> == C# (int), truncate toward zero.
  return (float)(int)in[0];  // fork-floattoint-int-on-float-port (int emitted as Float)
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_floattoint{
    // FloatToInt (TiXL Lib.numbers.int.process.FloatToInt): (int)FloatValue, truncate toward zero.
    // Port order MUST match evalFloatToIntOp's in[] read: FloatValue, then out.
    // Default from FloatToInt.cs: FloatValue=0 (fresh InputSlot<float>).
    {"FloatToInt", "FloatToInt",
     {{"FloatValue", "FloatValue", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalFloatToIntOp},
    "floattoint", runFloatToIntSelfTest};

// --- FloatToInt MATH golden --------------------------------------------------------------------
// Builds a 1-node FloatToInt graph, sets the param, pulls "out" via evalFloat (math_ops_selftest
// style), and compares to the hand-derived TiXL formula Result = (int)FloatValue. injectBug rounds
// to nearest instead of truncating in the typical-case expectation so the assertion flips RED,
// proving the tooth bites.
int runFloatToIntSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate FloatToInt with an explicit FloatValue.
  auto evalF2I = [&](float fv) -> float {
    const NodeSpec* spec = findSpec("FloatToInt");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "FloatToInt";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["FloatValue"] = fv;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: (int)2.7 = 2 (truncate toward zero, NOT round-to-3). injectBug expectation: pretend it
  // rounds to nearest -> 3, not 2.
  {
    float r = evalF2I(2.7f);
    float want = injectBug ? 3.0f : 2.0f;  // bug: round-to-nearest instead of truncate
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-floattoint] FloatToInt(2.7)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: (int)0 = 0 (FloatToInt.cs default FloatValue=0).
  {
    float r = evalF2I(0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-floattoint] FloatToInt(0)=%.6f want=0.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-floattoint-trunc-toward-zero (degenerate negatives): the case that distinguishes
  // truncate-toward-zero from floor-toward-neg-inf.
  //   (int)-0.9 = 0   (truncate; floor would give -1)
  //   (int)-1.0 = -1  (exact integer, unchanged)
  //   (int)-2.9 = -2  (truncate toward zero; floor would give -3)
  {
    float r = evalF2I(-0.9f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-floattoint] FloatToInt(-0.9)=%.6f want=0.000000 (trunc->0, not floor->-1) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalF2I(-1.0f);
    bool pass = std::fabs(r - (-1.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-floattoint] FloatToInt(-1.0)=%.6f want=-1.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalF2I(-2.9f);
    bool pass = std::fabs(r - (-2.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-floattoint] FloatToInt(-2.9)=%.6f want=-2.000000 (trunc->-2, not floor->-3) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // POSITIVE boundary: (int)1.999 = 1 (truncate, never rounds up below the next int).
  {
    float r = evalF2I(1.999f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-floattoint] FloatToInt(1.999)=%.6f want=1.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
