// IntAdd value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/int/basic/IntAdd.cs (+ IntAdd.t3 defaults).
//
//   IntAdd.cs Update():
//     var a = Value1.GetValue(context);   // int
//     var b = Value2.GetValue(context);   // int
//     Result.Value = a + b;               // int + int -> int
//
//   IntAdd.t3 DefaultValues: Value1=0, Value2=0. (Default eval: 0 + 0 = 0.)
//   IntAdd.t3ui Description: "Adds two integers\n\nSame as [AddInts]".
//
// 2 int inputs (Value1/Value2) -> 1 int output (Result). Pure stateless value op: behaviour is
// entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// FORKS (named):
//   - fork-intadd-twin-of-addints: TiXL ships IntAdd AND AddInts as TWO distinct ops with the SAME
//     fixed 2-arg int-add semantics (the IntAdd.t3ui description literally reads "Same as
//     [AddInts]"). They differ ONLY in port names: IntAdd has Value1/Value2, AddInts has
//     Input1/Input2 (see AddInts.cs:24,27). NEITHER is a MultiInput op — both Update() bodies are a
//     plain 2-operand `a + b`. This leaf faithfully reproduces IntAdd's Value1/Value2 port names so
//     .swproj wires keyed by port id stay byte-distinct from the already-shipped AddInts leaf
//     (value_op_addints.cpp). No behavioural divergence between the two — that is TiXL's own design.
//   - fork-intadd-int-on-float-port: TiXL Value1/Value2/Result are `int`. This runtime has only
//     Float value ports, so each input is read as a Float and truncated to int via (int) BEFORE the
//     add, then the int sum is emitted as a Float. The (int) cast = C# truncation toward zero (NOT
//     floor) — identical to the convention already shipped for Floor.cs (value_eval_ops.cpp
//     evalFloor: `(float)(int)in[0]`) and for AddInts (value_op_addints.cpp). For whole-number
//     inputs (the only ones an int slider produces in TiXL) this is byte-identical: 3 + 4 = 7,
//     -2 + 5 = 3, etc. Fractional Float inputs (which TiXL's int slot cannot hold) truncate toward
//     zero before adding, matching the C# (int) cast TiXL would apply if it ever saw one.
//   - fork-intadd-float-mantissa: a Float exactly represents integers only up to 2^24; beyond that
//     the int sum can lose low bits the C# `int` would keep. This is an inherent limitation of the
//     runtime's single Float value spine, not a behavioural choice — every existing int-typed value
//     op shares it. In the int slider's practical range it never bites.
//   NOTE: IntAdd has NO bool input (both ports are int), so the bool-on-Float-port convention does
//   not apply here.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runIntAddSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: Value1, Value2.
// Result = (int)Value1 + (int)Value2, emitted as Float  (TiXL IntAdd.cs verbatim, int semantics).
float evalIntAddOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int a = (int)in[0];  // fork-intadd-int-on-float-port (truncate toward zero, C# (int) cast)
  const int b = (int)in[1];
  return (float)(a + b);
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_intadd{
    // IntAdd (TiXL Lib.numbers.int.basic.IntAdd): (int)Value1 + (int)Value2.
    // Port order MUST match evalIntAddOp's in[] read: Value1, Value2, then out.
    // Defaults from IntAdd.t3: Value1=0, Value2=0. Port names = TiXL's (NOT AddInts' Input1/Input2).
    {"IntAdd", "IntAdd",
     {{"Value1", "Value1", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"Value2", "Value2", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalIntAddOp},
    "intadd", runIntAddSelfTest};

// --- IntAdd MATH golden ------------------------------------------------------------------------
// Builds a 1-node IntAdd graph, sets params, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL formula Result = (int)Value1 + (int)Value2. injectBug
// subtracts instead of adds in the typical-case expectation so the assertion flips RED, proving
// the tooth bites.
int runIntAddSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate IntAdd with explicit params.
  auto evalIntAdd = [&](float v1, float v2) -> float {
    const NodeSpec* spec = findSpec("IntAdd");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "IntAdd";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value1"] = v1;
    g.node(nid)->params["Value2"] = v2;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: 3 + 4 = 7. injectBug expectation: pretend it subtracts -> 3 - 4 = -1, not 7.
  {
    float r = evalIntAdd(3.0f, 4.0f);
    float want = injectBug ? -1.0f : 7.0f;  // bug: subtract instead of add
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-intadd] IntAdd(3,4)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: 0 + 0 = 0 (IntAdd.t3 defaults).
  {
    float r = evalIntAdd(0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-intadd] IntAdd(0,0)=%.6f want=0.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE / mixed sign: -2 + 5 = 3.
  {
    float r = evalIntAdd(-2.0f, 5.0f);
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-intadd] IntAdd(-2,5)=%.6f want=3.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // BOTH NEGATIVE: -7 + -8 = -15.
  {
    float r = evalIntAdd(-7.0f, -8.0f);
    bool pass = std::fabs(r - (-15.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-intadd] IntAdd(-7,-8)=%.6f want=-15.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-intadd-int-on-float-port: fractional inputs truncate toward zero BEFORE adding.
  //   (int)3.9 + (int)4.9 = 3 + 4 = 7 (NOT round-to-8, NOT 8.8). Negative truncates toward zero:
  //   (int)-2.9 = -2. So 3.9 + (-2.9) -> 3 + (-2) = 1.
  {
    float r = evalIntAdd(3.9f, 4.9f);
    bool pass = std::fabs(r - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-intadd] IntAdd(3.9,4.9)=%.6f want=7.000000 (trunc-toward-zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalIntAdd(3.9f, -2.9f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-intadd] IntAdd(3.9,-2.9)=%.6f want=1.000000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
