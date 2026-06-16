// Not value op (value-op self-registration seam leaf — Phase C numbers/bool mining).
// TiXL authority: Operators/Lib/numbers/bool/logic/Not.cs (+ Not.t3 defaults).
//
//   Not.cs Update():
//     Result.Value = !BoolValue.GetValue(context);
//
//   Not.t3 DefaultValues: BoolValue = false.
//     (Default eval: !false = true → Float 1.0 here.)
//
// 1 bool input (BoolValue) → 1 bool output (Result). Pure stateless value op: behaviour is
// entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// BOOL-AS-FLOAT CONVENTION (Cut 32 — this runtime has no Bool port type; bool dissolves to
// Float 0/1, exactly like IsGreater/Compare/InvertFloat already shipped):
//   - bool INPUT read as `(in[0] != 0.0f)` — same as evalInvertFloat's Invert / Curve's Clamp
//     (value_eval_ops.cpp:369,412).
//   - bool OUTPUT emitted as `1.0f` (true) / `0.0f` (false) — same as evalIsGreater /
//     evalCompare (value_eval_ops.cpp:576,605).
//   - Input port carries Widget::Bool + range [0,1], mirroring InvertFloat.Invert
//     (node_registry_math.cpp:605).
//
// FORK — none. !BoolValue maps cleanly: any non-zero input is "true" → emits 0.0f; zero input is
// "false" → emits 1.0f. No NaN/divide-by-zero/degenerate path to guard. Cleanest possible leaf.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runNotSelfTest(bool injectBug);

namespace {

// in[0] = BoolValue (Float 0/1). Result = !BoolValue → 1.0f when input is false (==0), else 0.0f.
// (TiXL Not.cs verbatim: `Result.Value = !BoolValue.GetValue(context);`)
float evalNotOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 1.0f;  // no input == BoolValue default false → !false = true = 1.0f
  return (in[0] != 0.0f) ? 0.0f : 1.0f;  // bool-as-float convention (see header)
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_not{
    // Not (TiXL Lib.numbers.bool.logic.Not): Result = !BoolValue.
    // Port order MUST match evalNotOp's in[] read: BoolValue, then out.
    // Default from Not.t3: BoolValue=false (0). Bool ports carry Widget::Bool, range [0,1].
    {"Not", "Not",
     {{"BoolValue", "BoolValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"out", "out", "Float", false}},
     evalNotOp},
    "not", runNotSelfTest};

// --- Not MATH golden ----------------------------------------------------------------------------
// Builds a 1-node Not graph, sets BoolValue, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL formula (Result = !BoolValue, bool→Float 0/1). injectBug
// flips the typical-case expectation so the tooth bites RED.
int runNotSelfTest(bool injectBug) {
  const float eps = 1e-6f;
  bool ok = true;

  // Helper: evaluate Not with an explicit BoolValue.
  auto evalNot = [&](float boolValue) -> float {
    const NodeSpec* spec = findSpec("Not");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Not";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["BoolValue"] = boolValue;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL (false → true): BoolValue=0 → !false = true = 1.0f.
  // injectBug asserts the WRONG value 0.0f (negation dropped — output==input failure mode) → RED.
  {
    float r = evalNot(0.0f);
    float want = injectBug ? 0.0f : 1.0f;  // bug: pretend Not is identity
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-not] Not(false=0)=%.6f want=%.6f -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }

  // TYPICAL (true → false): BoolValue=1 → !true = false = 0.0f.
  {
    float r = evalNot(1.0f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-not] Not(true=1)=%.6f want=0.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // DEFAULT (matches Not.t3 BoolValue=false): default param 0 → 1.0f, same as the typical false case.
  {
    float r = evalNot(0.0f);  // 0 is the Not.t3 default
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-not] Not(default=false)=%.6f want=1.000000 -> %s\n", r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY (any non-zero input is "true"): BoolValue=0.5 → treated true → !true = 0.0f.
  // (bool-as-float: != 0 is true; matches evalInvertFloat's `in[1] != 0.0f` precedent.)
  {
    float r = evalNot(0.5f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-not] Not(0.5=true)=%.6f want=0.000000 (nonzero->true) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
