// IntDiv value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/int/basic/IntDiv.cs (+ IntDiv.t3 defaults).
//
//   IntDiv.cs Update():
//     var n = Numerator.GetValue(context);
//     var d = Denominator.GetValue(context);
//     Result.Value = (d == 0) ? 1 : n / d;     // int / int -> int (C# integer division), d==0 -> 1
//
//   IntDiv.t3 DefaultValues: Numerator=0, Denominator=1. (Default eval: 0 / 1 = 0.)
//
// 2 int inputs (Numerator/Denominator) -> 1 int output (Result). Pure stateless value op:
// behaviour is entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// FORKS (named):
//   - fork-intdiv-divzero: TiXL has an EXPLICIT guard `(d == 0) ? 1 : n / d` — when the
//     Denominator is zero the op returns 1 (NOT 0, NOT NaN, NOT a C# DivideByZeroException).
//     This is verbatim TiXL behaviour, not a faithfulness invention: integer `n / 0` in C#
//     throws, and TiXL pre-empts that with a literal `1`. We reproduce the `1` exactly. The
//     golden asserts d=0 -> 1.
//   - fork-intdiv-int-on-float-port: TiXL Numerator/Denominator/Result are `int`. This runtime
//     has only Float value ports, so each input is read as a Float and truncated to int via (int)
//     BEFORE the division, and the int quotient is emitted as a Float. The (int) cast = C#
//     truncation toward zero (NOT floor) — identical to the convention already shipped for
//     Floor.cs / AddInts (value_op_addints.cpp). C# integer division ALSO truncates toward zero
//     (e.g. -7 / 2 == -3, not -4), so `(int)n / (int)d` is byte-identical to TiXL's `int / int`
//     for every whole-number input an int slider can produce, and matches the (int) cast TiXL
//     would apply if it ever saw a fractional Float on an int slot.
//   - fork-intdiv-float-mantissa: a Float exactly represents integers only up to 2^24; beyond
//     that quotient/operands can lose low bits the C# `int` would keep. Inherent limitation of the
//     runtime's single Float value spine (every int-typed value op shares it); never bites in the
//     int slider's practical range.
//   NOTE: IntDiv has NO bool input (both ports are int), so the bool-on-Float-port convention
//   does not apply here.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runIntDivSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: Numerator, Denominator.
// Result = (d == 0) ? 1 : (int)Numerator / (int)Denominator, emitted as Float (TiXL IntDiv.cs).
float evalIntDivOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int num = (int)in[0];  // fork-intdiv-int-on-float-port (truncate toward zero, C# (int))
  const int den = (int)in[1];
  if (den == 0) return 1.0f;   // fork-intdiv-divzero (TiXL: (d == 0) ? 1 : n / d) — verbatim
  return (float)(num / den);   // C# integer division truncates toward zero, like (int) cast
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_intdiv{
    // IntDiv (TiXL Lib.numbers.int.basic.IntDiv): (d==0) ? 1 : (int)Numerator / (int)Denominator.
    // Port order MUST match evalIntDivOp's in[] read: Numerator, Denominator, then out.
    // Defaults from IntDiv.t3: Numerator=0, Denominator=1.
    {"IntDiv", "IntDiv",
     {{"Numerator", "Numerator", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"Denominator", "Denominator", "Float", true, 1.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalIntDivOp},
    "intdiv", runIntDivSelfTest};

// --- IntDiv MATH golden ------------------------------------------------------------------------
// Builds a 1-node IntDiv graph, sets params, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL formula Result = (d==0) ? 1 : (int)Num / (int)Den.
// injectBug flips the typical-case expectation (pretend it multiplies) so the assertion goes RED,
// proving the tooth bites.
int runIntDivSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate IntDiv with explicit params.
  auto evalIntDiv = [&](float num, float den) -> float {
    const NodeSpec* spec = findSpec("IntDiv");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "IntDiv";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Numerator"]   = num;
    g.node(nid)->params["Denominator"] = den;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: 7 / 2 = 3 (C# int division truncates toward zero, NOT 3.5, NOT 4).
  // injectBug expectation: pretend it multiplies -> 7 * 2 = 14, not 3.
  {
    float r = evalIntDiv(7.0f, 2.0f);
    float want = injectBug ? 14.0f : 3.0f;  // bug: multiply instead of divide
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-intdiv] IntDiv(7,2)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: 0 / 1 = 0 (IntDiv.t3 defaults Numerator=0, Denominator=1).
  {
    float r = evalIntDiv(0.0f, 1.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-intdiv] IntDiv(0,1)=%.6f want=0.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // EXACT: 8 / 2 = 4 (no remainder).
  {
    float r = evalIntDiv(8.0f, 2.0f);
    bool pass = std::fabs(r - 4.0f) < eps;
    ok = ok && pass;
    printf("[selftest-intdiv] IntDiv(8,2)=%.6f want=4.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE NUMERATOR: -7 / 2 = -3 (C# truncates toward zero, NOT floor's -4).
  {
    float r = evalIntDiv(-7.0f, 2.0f);
    bool pass = std::fabs(r - (-3.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-intdiv] IntDiv(-7,2)=%.6f want=-3.000000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE DENOMINATOR: 7 / -2 = -3 (truncate toward zero).
  {
    float r = evalIntDiv(7.0f, -2.0f);
    bool pass = std::fabs(r - (-3.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-intdiv] IntDiv(7,-2)=%.6f want=-3.000000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY fork-intdiv-divzero: Denominator=0 -> 1 (TiXL explicit guard, NOT 0/NaN/exception).
  //   Numerator value is irrelevant when den==0: 42 / 0 -> 1.
  {
    float r = evalIntDiv(42.0f, 0.0f);
    bool pass = std::fabs(r - 1.0f) < eps && std::isfinite(r);
    ok = ok && pass;
    printf("[selftest-intdiv] IntDiv(42,0)=%.6f want=1.000000 (fork:divzero->1) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-intdiv-int-on-float-port: fractional inputs truncate toward zero BEFORE dividing.
  //   (int)7.9 / (int)2.9 = 7 / 2 = 3 (NOT 7.9/2.9 ~= 2.72, NOT round-up). Negative truncates
  //   toward zero: (int)-7.9 = -7, so -7.9 / 2.9 -> -7 / 2 = -3.
  {
    float r = evalIntDiv(7.9f, 2.9f);
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-intdiv] IntDiv(7.9,2.9)=%.6f want=3.000000 (trunc before div) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
