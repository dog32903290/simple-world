// ModInt value op (value-op self-registration seam leaf — parallel-weave, no shared edit point).
// TiXL authority: Operators/Lib/numbers/int/basic/ModInt.cs (+ ModInt.t3 defaults).
//
//   ModInt.cs Update():
//     var v = Value.GetValue(context);    // int
//     var mod = Mod.GetValue(context);    // int
//     if (mod == 0)
//         return;                         // (stateful) leave Result.Value untouched
//     Result.Value = v % mod;             // C# INTEGER %  → TRUNCATED modulo (sign of dividend)
//
//   ModInt.t3 DefaultValues: Value=0, Mod=1.  (Default eval: 0 % 1 = 0.)
//
//   Ports: Value = InputSlot<int>, Mod = InputSlot<int>.  Output: Result (Slot<int>).
//   2 int in → 1 int out. Pure stateless value op: behaviour is entirely the evaluate fn,
//   registered via the ValueOp seam (no GPU cook).
//
// NAMED FORKS:
//   - fork-modint-mod-zero (the one the task flagged): TiXL has Mod==0 → `return`, which leaves
//     the prior Result.Value in place — i.e. it is STATEFUL on that pathological input (keep last
//     written value). This runtime's value spine is stateless-per-frame (no prior value to keep),
//     so we return 0 (the same neutral the rest of the value spine uses for degenerate inputs, e.g.
//     Div B==0 → 0, Modulo m==0 → 0). The default Mod=1 and every non-zero Mod is byte-identical
//     to TiXL, so the fork only fires on the explicit Mod==0 case.
//   - fork-modint-int-via-float: this runtime has only Float ports, so the two int inputs arrive as
//     Float and are truncated to int via (int) before the modulo — matching C# `(int)` cast =
//     truncate-toward-zero, the same convention Floor.cs already follows (value_eval_ops.cpp:75-79).
//     The result is widened back to float for the value spine. For whole-number inputs (the only
//     ones meaningful to an int op) this is exact.
//   - fork-modint-truncated-not-floormod (semantic, easy to get wrong): C# integer `%` is TRUNCATED
//     modulo — the result takes the sign of the DIVIDEND. This is DIFFERENT from the float Modulo.cs
//     op (floor-mod: v - mod*floor(v/mod), sign of divisor) already shipped. We deliberately use
//     C++ `%` on the truncated ints (which is ALSO truncated modulo, identical to C#), NOT floor-mod.
//     e.g. (-7) % 3 = -1 here (TiXL ModInt), whereas float Modulo(-7,3) = 2.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"            // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"   // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runModIntSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: Value, Mod.
// Result = (int)Value % (int)Mod  (TiXL ModInt.cs verbatim; C# integer % = truncated modulo).
float evalModInt(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int v = (int)in[0];    // fork-modint-int-via-float (truncate toward zero, C# (int) cast)
  const int mod = (int)in[1];
  if (mod == 0) return 0.0f;    // fork-modint-mod-zero (see header) — stateless → 0, not prior value
  return (float)(v % mod);      // C++ % on ints = truncated modulo = C# % (sign of dividend)
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_modint{
    // ModInt (TiXL Lib.numbers.int.basic.ModInt): (int)Value % (int)Mod, truncated modulo.
    // Port order MUST match evalModInt's in[] read: Value, Mod, then out.
    // Defaults from ModInt.t3: Value=0, Mod=1 (so the default node eval = 0 % 1 = 0, no fork).
    {"ModInt", "ModInt",
     {{"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider},
      {"Mod", "Mod", "Float", true, 1.0f, -1000.0f, 1000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalModInt},
    "modint", runModIntSelfTest};

// --- ModInt MATH golden --------------------------------------------------------------------------
// Builds a 1-node ModInt graph, sets params, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL formula. injectBug swaps the truncated-modulo expectation
// for a floor-mod one on the negative case so the assertion flips RED (proves the tooth bites AND
// guards specifically against the easy floor-mod-vs-truncated-mod confusion).
int runModIntSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate ModInt with explicit params.
  auto evalMod = [&](float value, float mod) -> float {
    const NodeSpec* spec = findSpec("ModInt");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "ModInt";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value"] = value;
    g.node(nid)->params["Mod"]   = mod;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Value=10, Mod=3 → 10 % 3 = 1.
  {
    float r = evalMod(10.0f, 3.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-modint] ModInt(10,3)=%.6f want=1.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // TYPICAL: Value=17, Mod=5 → 17 % 5 = 2.
  {
    float r = evalMod(17.0f, 5.0f);
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-modint] ModInt(17,5)=%.6f want=2.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE (truncated-modulo, sign of dividend): Value=-7, Mod=3 → -7 % 3 = -1 (C#/C++ truncated).
  //   This is the DISCRIMINATING case vs float Modulo (floor-mod would give 2). injectBug asserts
  //   the floor-mod value (2) instead → flips RED, catching a floor-mod mis-port.
  {
    float r = evalMod(-7.0f, 3.0f);
    float want = injectBug ? 2.0f : -1.0f;  // bug: pretend floor-mod (sign of divisor)
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-modint] ModInt(-7,3)=%.6f want=%.6f (truncated-mod) -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // INT-VIA-FLOAT (fork-modint-int-via-float): Value=10.9, Mod=3.9 → (int)10.9=10, (int)3.9=3 →
  //   10 % 3 = 1 (truncate toward zero before modulo; fractional part dropped, NOT rounded).
  {
    float r = evalMod(10.9f, 3.9f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-modint] ModInt(10.9,3.9)=%.6f want=1.000000 (int trunc) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY fork-modint-mod-zero: Mod=0 → 0 (stateless), and finite (not NaN/garbage).
  {
    float r = evalMod(42.0f, 0.0f);
    bool pass = std::fabs(r) < eps && std::isfinite(r);
    ok = ok && pass;
    printf("[selftest-modint] ModInt(42,0)=%.6f want=0.000000 (fork:mod0->0) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: Value=0, Mod=1 (ModInt.t3 defaults) → 0 % 1 = 0.
  {
    float r = evalMod(0.0f, 1.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-modint] ModInt(0,1)=%.6f want=0.000000 (t3 defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
