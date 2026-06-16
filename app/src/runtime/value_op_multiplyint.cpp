// MultiplyInt value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/int/basic/MultiplyInt.cs (+ MultiplyInt.t3 defaults).
//
//   MultiplyInt.cs Update():
//     Result.Value = A.GetValue(context) * B.GetValue(context);   // int * int -> int
//
//   MultiplyInt.t3 DefaultValues: A=1, B=1. (Default eval: 1 * 1 = 1.)
//
// 2 int inputs (A/B) -> 1 int output (Result). Pure stateless value op: behaviour is entirely the
// evaluate fn, registered via the ValueOp seam (no GPU cook). Sibling of value_op_addints.cpp —
// identical int-on-Float-port shape, only the operator changes (+ -> *).
//
// FORKS (named):
//   - fork-multiplyint-int-on-float-port: TiXL A/B/Result are `int`. This runtime has only Float
//     value ports, so each input is read as a Float and truncated to int via (int) BEFORE the
//     multiply, then the int product is emitted as a Float. The (int) cast = C# truncation toward
//     zero (NOT floor) — identical to the convention already shipped for Floor.cs (value_eval_ops.cpp
//     evalFloor: `(float)(int)in[0]`) and for AddInts (value_op_addints.cpp). For whole-number
//     inputs (the only ones an int slider produces in TiXL) this is byte-identical: 3 * 4 = 12,
//     -2 * 5 = -10, etc. Fractional Float inputs (which TiXL's int slot cannot hold) truncate toward
//     zero before multiplying, matching the C# (int) cast TiXL would apply if it ever saw one.
//   - fork-multiplyint-no-overflow-guard: TiXL has NO overflow guard (plain `int * int`, which
//     wraps on overflow in unchecked C#). We add none either = faithful. Within the int slider's
//     practical range it never bites; products that overflow 32-bit int are outside any value an
//     int slider produces, and the Float spine would lose precision past 2^24 regardless (see
//     fork-multiplyint-float-mantissa).
//   - fork-multiplyint-float-mantissa: a Float exactly represents integers only up to 2^24; beyond
//     that the int product can lose low bits the C# `int` would keep. This is an inherent limitation
//     of the runtime's single Float value spine, not a behavioural choice — every existing int-typed
//     value op shares it. In the int slider's practical range it never bites.
//   NOTE: MultiplyInt has NO bool input (both ports are int), so the bool-on-Float-port convention
//   does not apply here.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runMultiplyIntSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: A, B.
// Result = (int)A * (int)B, emitted as Float  (TiXL MultiplyInt.cs verbatim, int semantics).
float evalMultiplyIntOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int a = (int)in[0];  // fork-multiplyint-int-on-float-port (truncate toward zero, C# (int) cast)
  const int b = (int)in[1];
  return (float)(a * b);  // fork-multiplyint-no-overflow-guard (plain int *, no guard, like TiXL)
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_multiplyint{
    // MultiplyInt (TiXL Lib.numbers.int.basic.MultiplyInt): (int)A * (int)B.
    // Port order MUST match evalMultiplyIntOp's in[] read: A, B, then out.
    // Defaults from MultiplyInt.t3: A=1, B=1.
    {"MultiplyInt", "MultiplyInt",
     {{"A", "A", "Float", true, 1.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"B", "B", "Float", true, 1.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalMultiplyIntOp},
    "multiplyint", runMultiplyIntSelfTest};

// --- MultiplyInt MATH golden -------------------------------------------------------------------
// Builds a 1-node MultiplyInt graph, sets params, pulls "out" via evalFloat (math_ops_selftest
// style), and compares to the hand-derived TiXL formula Result = (int)A * (int)B. injectBug adds
// instead of multiplies in the typical-case expectation so the assertion flips RED, proving the
// tooth bites.
int runMultiplyIntSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate MultiplyInt with explicit params.
  auto evalMultiplyInt = [&](float a, float b) -> float {
    const NodeSpec* spec = findSpec("MultiplyInt");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "MultiplyInt";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["A"] = a;
    g.node(nid)->params["B"] = b;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: 3 * 4 = 12. injectBug expectation: pretend it adds -> 3 + 4 = 7, not 12.
  {
    float r = evalMultiplyInt(3.0f, 4.0f);
    float want = injectBug ? 7.0f : 12.0f;  // bug: add instead of multiply
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyint] MultiplyInt(3,4)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: 1 * 1 = 1 (MultiplyInt.t3 defaults A=1, B=1).
  {
    float r = evalMultiplyInt(1.0f, 1.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyint] MultiplyInt(1,1)=%.6f want=1.000000 (defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // ZERO factor: 7 * 0 = 0 (multiplicative annihilator — distinct from AddInts where 0 is identity).
  {
    float r = evalMultiplyInt(7.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyint] MultiplyInt(7,0)=%.6f want=0.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE / mixed sign: -2 * 5 = -10.
  {
    float r = evalMultiplyInt(-2.0f, 5.0f);
    bool pass = std::fabs(r - (-10.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyint] MultiplyInt(-2,5)=%.6f want=-10.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOTH NEGATIVE: -7 * -8 = 56 (sign cancels).
  {
    float r = evalMultiplyInt(-7.0f, -8.0f);
    bool pass = std::fabs(r - 56.0f) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyint] MultiplyInt(-7,-8)=%.6f want=56.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-multiplyint-int-on-float-port: fractional inputs truncate toward zero BEFORE
  //   multiplying. (int)3.9 * (int)4.9 = 3 * 4 = 12 (NOT round-to-20, NOT 19.11). Negative
  //   truncates toward zero: (int)-2.9 = -2. So 3.9 * (-2.9) -> 3 * (-2) = -6.
  {
    float r = evalMultiplyInt(3.9f, 4.9f);
    bool pass = std::fabs(r - 12.0f) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyint] MultiplyInt(3.9,4.9)=%.6f want=12.000000 (trunc-toward-zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalMultiplyInt(3.9f, -2.9f);
    bool pass = std::fabs(r - (-6.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyint] MultiplyInt(3.9,-2.9)=%.6f want=-6.000000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
