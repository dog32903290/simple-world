// Or value op (value-op self-registration seam leaf — boolean combine family).
// TiXL authority: Operators/Lib/numbers/bool/combine/Or.cs.
//
//   Or.cs Update():
//     // evaluate both to clear dirty flag
//     var a = A.GetValue(context);
//     var b = B.GetValue(context);
//     var resultValue = a || b;
//     Result.Value = resultValue;
//
//   Ports (Or.cs): A = InputSlot<bool>, B = InputSlot<bool> (no source default → false).
//   Output: Result = Slot<bool>.
//   Note: Or.cs uses C# `||` and explicitly reads BOTH A and B BEFORE combining ("evaluate both to
//   clear dirty flag"), so although `||` is short-circuiting, both inputs are read unconditionally
//   in TiXL. For two pure-value bool inputs the observable result is identical to a plain `a || b`:
//   true iff EITHER is true. We mirror it with `(a || b)` over the booleanized Float inputs; reading
//   a Float port has no side effects in this runtime (no GetValue subscriber to retrigger), so the
//   short-circuit-vs-read-both distinction is moot here.
//
// 2 bool inputs (A, B) → 1 bool output (Result). Pure stateless value op: behaviour is entirely
// the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// BOOL-AS-FLOAT CONVENTION (Cut 32, NOT a fork — the established runtime contract):
//   This runtime has no Bool port type; every bool dissolves to a Float. Established precedent:
//     - bool INPUT read as truthy via `(x != 0.0f)` (value_eval_ops.cpp:369 Invert, :412 Clamp).
//     - bool OUTPUT emitted as `cond ? 1.0f : 0.0f` (value_eval_ops.cpp:576 IsGreater, :605 Compare).
//     - the Inspector affordance for a bool Float input is Widget::Bool (node_registry_math.cpp:605).
//   Or follows all three verbatim, exactly as the sibling And leaf (value_op_and.cpp) does. Default
//   for an unsourced TiXL InputSlot<bool> = false → 0.0f.
//
// FORK: none. (No NaN/div-by-zero edge — pure boolean. The Cut-32 bool-as-Float dissolve is the
//   runtime-wide contract, applied identically by IsGreater/Compare/Invert/And, not an Or-specific
//   divergence. TiXL Or.cs has no _lastResult dirty-flag — it writes Result.Value every Update — so
//   there is not even a stateless dirty-flag fork.)
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runOrSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: A, B.
// Result = (A || B) → 1.0f iff either truthy, else 0.0f  (TiXL Or.cs verbatim).
float evalOrOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const bool a = (in[0] != 0.0f);  // bool-as-Float read (Cut 32, see header)
  const bool b = (in[1] != 0.0f);
  return (a || b) ? 1.0f : 0.0f;   // bool-as-Float emit
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_or{
    // Or (TiXL Lib.numbers.bool.combine.Or): Result = A || B, both bool, output bool 0/1.
    // Port order MUST match evalOrOp's in[] read: A, B, then out.
    // Bool Float inputs: def=0.0f (TiXL false), range [0,1], Widget::Bool (node_registry_math.cpp).
    {"Or", "Or",
     {{"A", "A", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"B", "B", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"out", "out", "Float", false}},
     evalOrOp},
    "or", runOrSelfTest};

// --- Or MATH golden ----------------------------------------------------------------------------
// Builds a 1-node Or graph, sets params, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL truth table. injectBug flips the both-false expectation
// from 0 to 1 so the all-false-row assertion goes RED, proving the tooth bites.
int runOrSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate Or with explicit A, B.
  auto evalOr = [&](float a, float b) -> float {
    const NodeSpec* spec = findSpec("Or");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Or";
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

  // TRUTH TABLE (TiXL: Result = A || B). 1=true, 0=false.
  //   A=1,B=1 → 1
  //   A=1,B=0 → 1
  //   A=0,B=1 → 1
  //   A=0,B=0 → 0 (the ONLY false row)
  struct Row { float a, b, want; const char* label; };
  Row rows[] = {
      {1.0f, 1.0f, 1.0f, "1|1=1"},
      {1.0f, 0.0f, 1.0f, "1|0=1"},
      {0.0f, 1.0f, 1.0f, "0|1=1"},
      {0.0f, 0.0f, 0.0f, "0|0=0"},
  };
  for (const auto& r : rows) {
    // injectBug: flip ONLY the both-false row (0|0) from 0 to 1 — the canonical "Or collapsed to
    // always-true" / "swapped to Nor" failure mode → forces RED.
    float want = (injectBug && r.a == 0.0f && r.b == 0.0f) ? 1.0f : r.want;
    float v = evalOr(r.a, r.b);
    bool pass = std::fabs(v - want) < eps;
    ok = ok && pass;
    printf("[selftest-or] Or(%s) A=%.0f B=%.0f =%.6f want=%.6f -> %s\n",
           r.label, r.a, r.b, v, want, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: non-0/1 truthy inputs still booleanize (any non-zero = true).
  //   A=0.3, B=0.0 → A truthy → 1.   A=0.0, B=0.0 → both false → 0 (already covered, recheck half).
  {
    float v = evalOr(0.3f, 0.0f);
    bool pass = std::fabs(v - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-or] Or(truthy) A=0.3 B=0.0 =%.6f want=1.000000 (any!=0 is true) -> %s\n",
           v, pass ? "PASS" : "FAIL");
  }
  {
    float v = evalOr(0.3f, 2.0f);
    bool pass = std::fabs(v - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-or] Or(both-truthy) A=0.3 B=2.0 =%.6f want=1.000000 -> %s\n",
           v, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
