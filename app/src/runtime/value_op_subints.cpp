// SubInts value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/int/basic/SubInts.cs (+ SubInts.t3 defaults).
//
//   SubInts.cs Update():
//     Result.Value = Input1.GetValue(context) - Input2.GetValue(context);   // int - int -> int
//
//   SubInts.t3 DefaultValues: Input1=0, Input2=0. (Default eval: 0 - 0 = 0.)
//
// 2 int inputs (Input1/Input2) -> 1 int output (Result). Pure stateless value op: behaviour is
// entirely the evaluate fn, registered via the ValueOp seam (no GPU cook). Exact mirror of AddInts
// (value_op_addints.cpp) with subtraction in place of addition.
//
// FORKS (named):
//   - fork-subints-int-on-float-port: TiXL Input1/Input2/Result are `int`. This runtime has only
//     Float value ports, so each input is read as a Float and truncated to int via (int) BEFORE
//     the subtract, then the int difference is emitted as a Float. The (int) cast = C# truncation
//     toward zero (NOT floor) — identical to the convention already shipped for AddInts.cs
//     (value_op_addints.cpp) and Floor.cs (value_eval_ops.cpp evalFloor: `(float)(int)in[0]`). For
//     whole-number inputs (the only ones an int slider produces in TiXL) this is byte-identical:
//     7 - 4 = 3, 5 - (-2) = 7, etc. Fractional Float inputs (which TiXL's int slot cannot hold)
//     truncate toward zero before subtracting, matching the C# (int) cast TiXL would apply if it
//     ever saw one.
//   - fork-subints-float-mantissa: a Float exactly represents integers only up to 2^24; beyond
//     that the int difference can lose low bits the C# `int` would keep. This is an inherent
//     limitation of the runtime's single Float value spine, not a behavioural choice — every
//     existing int-typed value op shares it. In the int slider's practical range it never bites.
//   NOTE: SubInts has NO bool input (both ports are int), so the bool-on-Float-port convention does
//   not apply here.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runSubIntsSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: Input1, Input2.
// Result = (int)Input1 - (int)Input2, emitted as Float  (TiXL SubInts.cs verbatim, int semantics).
float evalSubIntsOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int a = (int)in[0];  // fork-subints-int-on-float-port (truncate toward zero, C# (int) cast)
  const int b = (int)in[1];
  return (float)(a - b);
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_subints{
    // SubInts (TiXL Lib.numbers.int.basic.SubInts): (int)Input1 - (int)Input2.
    // Port order MUST match evalSubIntsOp's in[] read: Input1, Input2, then out.
    // Defaults from SubInts.t3: Input1=0, Input2=0.
    {"SubInts", "SubInts",
     {{"Input1", "Input1", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"Input2", "Input2", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalSubIntsOp},
    "subints", runSubIntsSelfTest};

// --- SubInts MATH golden -----------------------------------------------------------------------
// Builds a 1-node SubInts graph, sets params, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL formula Result = (int)Input1 - (int)Input2. injectBug adds
// instead of subtracts in the typical-case expectation so the assertion flips RED, proving the
// tooth bites.
int runSubIntsSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate SubInts with explicit params.
  auto evalSubInts = [&](float in1, float in2) -> float {
    const NodeSpec* spec = findSpec("SubInts");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "SubInts";
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

  // TYPICAL: 7 - 4 = 3. injectBug expectation: pretend it adds -> 7 + 4 = 11, not 3.
  {
    float r = evalSubInts(7.0f, 4.0f);
    float want = injectBug ? 11.0f : 3.0f;  // bug: add instead of subtract
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-subints] SubInts(7,4)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: 0 - 0 = 0 (SubInts.t3 defaults).
  {
    float r = evalSubInts(0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-subints] SubInts(0,0)=%.6f want=0.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // ORDER SENSITIVITY (non-commutative): 4 - 7 = -3 (NOT 3) — proves arg order is honoured.
  {
    float r = evalSubInts(4.0f, 7.0f);
    bool pass = std::fabs(r - (-3.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-subints] SubInts(4,7)=%.6f want=-3.000000 (order) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // SUBTRACT NEGATIVE: 5 - (-2) = 7.
  {
    float r = evalSubInts(5.0f, -2.0f);
    bool pass = std::fabs(r - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-subints] SubInts(5,-2)=%.6f want=7.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOTH NEGATIVE: -7 - (-8) = 1.
  {
    float r = evalSubInts(-7.0f, -8.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-subints] SubInts(-7,-8)=%.6f want=1.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-subints-int-on-float-port: fractional inputs truncate toward zero BEFORE subtracting.
  //   (int)7.9 - (int)4.9 = 7 - 4 = 3 (NOT round, NOT 3.0 from 7.9-4.9=3.0 by luck — the values
  //   below distinguish trunc from raw float). Negative truncates toward zero: (int)-2.9 = -2.
  //   So (int)7.9 - (int)-2.9 = 7 - (-2) = 9.  (Raw float 7.9 - (-2.9) = 10.8 would fail.)
  {
    float r = evalSubInts(7.9f, 4.9f);
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-subints] SubInts(7.9,4.9)=%.6f want=3.000000 (trunc-toward-zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalSubInts(7.9f, -2.9f);
    bool pass = std::fabs(r - 9.0f) < eps;
    ok = ok && pass;
    printf("[selftest-subints] SubInts(7.9,-2.9)=%.6f want=9.000000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
