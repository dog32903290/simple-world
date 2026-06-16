// IntToFloat value op (value-op self-registration seam leaf — numbers/int family).
// TiXL authority: Operators/Lib/numbers/int/basic/IntToFloat.cs (verbatim):
//
//   [Output] public readonly Slot<float> Result = new();
//   private void Update(EvaluationContext context) {
//       Result.Value = IntValue.GetValue(context);          // int → float (widening, exact)
//   }
//   [Input] public readonly InputSlot<int> IntValue = new();  // default 0
//
// 1 input (IntValue) → 1 output (Result). Pure stateless value op: behaviour is entirely the
// evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// NAMED FORK — fork-inttofloat-int-slot-truncates:
//   TiXL's IntValue is an InputSlot<int>: whatever value reaches that slot is already an int,
//   so any non-integer source is truncated toward zero by C# `(int)` semantics on assignment
//   BEFORE the int→float widening in Update(). This runtime has only Float ports, so to be
//   byte-faithful we replicate the int slot by truncating the incoming Float toward zero,
//   `(float)(int)in[0]`, then widening back. For every whole-number input (the only kind that
//   makes sense for an int slot, and TiXL's default IntValue=0) this is byte-identical to TiXL.
//   This is the SAME `(int)`-cast = truncate-toward-zero convention already shipped in Floor
//   (value_eval_ops.cpp:79, "TiXL Floor.cs (int) cast = truncf"), not floor-toward-neg-infinity:
//     IntToFloat(2.7) → 2.0 ;  IntToFloat(-2.7) → -2.0   (toward zero, like C# (int)).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runIntToFloatSelfTest(bool injectBug);

namespace {

// in[] = [IntValue]. Result = (float)(int)IntValue  — int-slot truncate-toward-zero then widen.
// (TiXL IntToFloat.cs: Result.Value = IntValue.GetValue(context), where IntValue is an int slot.)
float evalIntToFloat(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;
  return (float)(int)in[0];  // fork-inttofloat-int-slot-truncates (see header)
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_inttofloat{
    // IntToFloat (TiXL Lib.numbers.int.basic.IntToFloat): widen an int input to float.
    // Single input IntValue (default 0, an int slot → truncate-toward-zero) → out.
    // Port order MUST match evalIntToFloat's in[] read: IntValue, then out.
    {"IntToFloat", "IntToFloat",
     {{"IntValue", "IntValue", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalIntToFloat},
    "inttofloat", runIntToFloatSelfTest};

// --- IntToFloat MATH golden -------------------------------------------------------------------
// Builds a 1-node IntToFloat graph, sets IntValue, pulls "out" via evalFloat (math_ops_selftest
// style), and compares to the hand-derived TiXL result. injectBug flips the truncation-fork
// expectation (asserts round-to-nearest instead of toward-zero) so an assertion goes RED, proving
// the tooth bites on the load-bearing fork.
int runIntToFloatSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate IntToFloat with an explicit IntValue.
  auto evalI2F = [&](float intValue) -> float {
    const NodeSpec* spec = findSpec("IntToFloat");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "IntToFloat";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["IntValue"] = intValue;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL whole number: IntValue=5 → 5.0 (exact int→float widening, byte-identical to TiXL).
  {
    float r = evalI2F(5.0f);
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-inttofloat] IntToFloat(5)=%.6f want=5.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: IntValue=0 (TiXL default) → 0.0.
  {
    float r = evalI2F(0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-inttofloat] IntToFloat(0)=%.6f want=0.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE whole number: IntValue=-7 → -7.0.
  {
    float r = evalI2F(-7.0f);
    bool pass = std::fabs(r - (-7.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-inttofloat] IntToFloat(-7)=%.6f want=-7.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK (positive non-integer): IntValue=2.7 → int slot truncates toward zero → 2 → 2.0.
  // injectBug asserts round-to-nearest (3.0) instead of toward-zero (2.0) → flips RED, proving the
  // truncation-toward-zero fork is the one this tooth guards (round-up would mis-port the int slot).
  {
    float r = evalI2F(2.7f);
    float want = injectBug ? 3.0f : 2.0f;  // bug: round-to-nearest, not (int)-truncate
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-inttofloat] IntToFloat(2.7)=%.6f want=%.6f (fork:trunc-toward-zero) -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // FORK (negative non-integer): IntValue=-2.7 → truncate TOWARD ZERO → -2 → -2.0
  // (NOT floor-toward-neg-infinity which would give -3; this distinguishes (int)-cast from floor).
  {
    float r = evalI2F(-2.7f);
    bool pass = std::fabs(r - (-2.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-inttofloat] IntToFloat(-2.7)=%.6f want=-2.000000 (toward-zero not floor) "
           "-> %s\n", r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
