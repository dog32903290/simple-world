// AddInts value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/int/basic/AddInts.cs (+ AddInts.t3 defaults).
//
//   AddInts.cs Update():
//     Result.Value = Input1.GetValue(context) + Input2.GetValue(context);   // int + int -> int
//
//   AddInts.t3 DefaultValues: Input1=0, Input2=0. (Default eval: 0 + 0 = 0.)
//
// 2 int inputs (Input1/Input2) -> 1 int output (Result). Pure stateless value op: behaviour is
// entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// FORKS (named):
//   - fork-addints-int-on-float-port: TiXL Input1/Input2/Result are `int`. This runtime has only
//     Float value ports, so each input is read as a Float and truncated to int via (int) BEFORE
//     the add, then the int sum is emitted as a Float. The (int) cast = C# truncation toward zero
//     (NOT floor) — identical to the convention already shipped for Floor.cs (value_eval_ops.cpp
//     evalFloor: `(float)(int)in[0]`). For whole-number inputs (the only ones an int slider
//     produces in TiXL) this is byte-identical: 3 + 4 = 7, -2 + 5 = 3, etc. Fractional Float
//     inputs (which TiXL's int slot cannot hold) truncate toward zero before adding, matching the
//     C# (int) cast TiXL would apply if it ever saw one.
//   - fork-addints-float-mantissa: a Float exactly represents integers only up to 2^24; beyond
//     that the int sum can lose low bits the C# `int` would keep. This is an inherent limitation of
//     the runtime's single Float value spine, not a behavioural choice — every existing int-typed
//     value op shares it. In the int slider's practical range it never bites.
//   NOTE: AddInts has NO bool input (both ports are int), so the bool-on-Float-port convention does
//   not apply here.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runAddIntsSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: Input1, Input2.
// Result = (int)Input1 + (int)Input2, emitted as Float  (TiXL AddInts.cs verbatim, int semantics).
float evalAddIntsOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int a = (int)in[0];  // fork-addints-int-on-float-port (truncate toward zero, C# (int) cast)
  const int b = (int)in[1];
  return (float)(a + b);
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_addints{
    // AddInts (TiXL Lib.numbers.int.basic.AddInts): (int)Input1 + (int)Input2.
    // Port order MUST match evalAddIntsOp's in[] read: Input1, Input2, then out.
    // Defaults from AddInts.t3: Input1=0, Input2=0.
    {"AddInts", "AddInts",
     {{"Input1", "Input1", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"Input2", "Input2", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalAddIntsOp},
    "addints", runAddIntsSelfTest};

// --- AddInts MATH golden -----------------------------------------------------------------------
// Builds a 1-node AddInts graph, sets params, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL formula Result = (int)Input1 + (int)Input2. injectBug
// subtracts instead of adds in the typical-case expectation so the assertion flips RED, proving
// the tooth bites.
int runAddIntsSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate AddInts with explicit params.
  auto evalAddInts = [&](float in1, float in2) -> float {
    const NodeSpec* spec = findSpec("AddInts");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AddInts";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input1"] = in1;
    g.node(nid)->params["Input2"] = in2;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: 3 + 4 = 7. injectBug expectation: pretend it subtracts -> 3 - 4 = -1, not 7.
  {
    float r = evalAddInts(3.0f, 4.0f);
    float want = injectBug ? -1.0f : 7.0f;  // bug: subtract instead of add
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-addints] AddInts(3,4)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: 0 + 0 = 0 (AddInts.t3 defaults).
  {
    float r = evalAddInts(0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-addints] AddInts(0,0)=%.6f want=0.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE / mixed sign: -2 + 5 = 3.
  {
    float r = evalAddInts(-2.0f, 5.0f);
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addints] AddInts(-2,5)=%.6f want=3.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // BOTH NEGATIVE: -7 + -8 = -15.
  {
    float r = evalAddInts(-7.0f, -8.0f);
    bool pass = std::fabs(r - (-15.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-addints] AddInts(-7,-8)=%.6f want=-15.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-addints-int-on-float-port: fractional inputs truncate toward zero BEFORE adding.
  //   (int)3.9 + (int)4.9 = 3 + 4 = 7 (NOT round-to-8, NOT 8.8). Negative truncates toward zero:
  //   (int)-2.9 = -2. So 3.9 + (-2.9) -> 3 + (-2) = 1.
  {
    float r = evalAddInts(3.9f, 4.9f);
    bool pass = std::fabs(r - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addints] AddInts(3.9,4.9)=%.6f want=7.000000 (trunc-toward-zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalAddInts(3.9f, -2.9f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addints] AddInts(3.9,-2.9)=%.6f want=1.000000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
