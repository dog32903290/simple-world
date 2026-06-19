// BoolToInt value op (value-op self-registration seam leaf — Phase C numbers/bool mining).
// TiXL authority: Operators/Lib/numbers/bool/convert/BoolToInt.cs (+ BoolToInt.t3 defaults).
//
//   BoolToInt.cs Update():
//     var valueForTrue  = ResultForTrue.GetValue(context);
//     var valueForFalse = ResultForFalse.GetValue(context);
//     Result.Value = BoolValue.GetValue(context) ? valueForTrue : valueForFalse;
//
//   BoolToInt.t3 DefaultValues: ResultForFalse=0, BoolValue=false, ResultForTrue=1.
//     (Default eval: false → ResultForFalse = 0.)
//
// 3 inputs (BoolValue/ResultForTrue/ResultForFalse) → 1 int output (Result). Pure stateless
// value op: behaviour is entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// BOOL-AS-FLOAT CONVENTION (same as Not / BoolToFloat already shipped):
//   - BoolValue INPUT read as `(in[N] != 0.0f)` — non-zero is true.
//   - ResultForTrue / ResultForFalse are int in TiXL → emitted as Float from this runtime
//     (int-on-Float-port convention: truncate-to-int then emit as Float, same as MultiplyInt).
//
// FORKS (named):
//   - fork-booltoint-int-on-float-port: TiXL ResultForTrue/ResultForFalse/Result are `int`.
//     This runtime has only Float ports, so the selected value is truncated via (int) then
//     emitted as Float. For whole-number inputs (the only values an int slider produces) this
//     is byte-identical to TiXL. Matches the fork already named in MultiplyInt / AddInts.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runBoolToIntSelfTest(bool injectBug);

namespace {

// in[] order = port spec order: BoolValue, ResultForTrue, ResultForFalse.
// Result = (int)(BoolValue ? ResultForTrue : ResultForFalse), emitted as Float.
// (TiXL BoolToInt.cs verbatim; fork-booltoint-int-on-float-port applies to output.)
float evalBoolToIntOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const bool bv         = (in[0] != 0.0f);  // bool-as-float convention
  const int  trueVal    = (int)in[1];        // fork-booltoint-int-on-float-port: truncate
  const int  falseVal   = (int)in[2];        // same
  return (float)(bv ? trueVal : falseVal);
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_booltoint{
    // BoolToInt (TiXL Lib.numbers.bool.convert.BoolToInt):
    //   Result = BoolValue ? ResultForTrue : ResultForFalse.
    // Port order MUST match evalBoolToIntOp's in[] read: BoolValue, ResultForTrue, ResultForFalse,
    // then out.
    // Defaults from BoolToInt.t3: ResultForFalse=0, BoolValue=false(0), ResultForTrue=1.
    {"BoolToInt", "BoolToInt",
     {{"BoolValue",      "BoolValue",      "Float", true, 0.0f,  0.0f,    1.0f,    Widget::Bool},
      {"ResultForTrue",  "ResultForTrue",  "Float", true, 1.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"ResultForFalse", "ResultForFalse", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"out",            "out",            "Float", false}},
     evalBoolToIntOp},
    "booltoint", runBoolToIntSelfTest};

// --- BoolToInt MATH golden -------------------------------------------------------------------
// Builds a 1-node BoolToInt graph, sets params, pulls "out" via evalFloat, and compares to
// the hand-derived TiXL formula Result = (int)(BoolValue ? ResultForTrue : ResultForFalse).
// injectBug swaps the expected branch so the typical-case assertion flips RED.
int runBoolToIntSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate BoolToInt with explicit params.
  auto evalB2I = [&](float boolValue, float forTrue, float forFalse) -> float {
    const NodeSpec* spec = findSpec("BoolToInt");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "BoolToInt";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["BoolValue"]      = boolValue;
    g.node(nid)->params["ResultForTrue"]  = forTrue;
    g.node(nid)->params["ResultForFalse"] = forFalse;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL (true branch): BoolValue=1, ResultForTrue=42, ResultForFalse=7 → 42.
  // injectBug expects 7 (the false branch) → flips RED, proving the branch is tested.
  {
    float r = evalB2I(1.0f, 42.0f, 7.0f);
    float want = injectBug ? 7.0f : 42.0f;  // bug: returns ResultForFalse instead of ResultForTrue
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-booltoint] BoolToInt(true,42,7)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // TYPICAL (false branch): BoolValue=0, ResultForTrue=42, ResultForFalse=7 → 7.
  {
    float r = evalB2I(0.0f, 42.0f, 7.0f);
    bool pass = std::fabs(r - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-booltoint] BoolToInt(false,42,7)=%.6f want=7.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: BoolToInt.t3 BoolValue=false, ResultForTrue=1, ResultForFalse=0 → 0.
  {
    float r = evalB2I(0.0f, 1.0f, 0.0f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-booltoint] BoolToInt(defaults)=%.6f want=0.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK (int truncation): ResultForTrue=5.9, BoolValue=1 → (int)5.9 = 5, emitted as 5.0.
  // Proves truncate-toward-zero (fork-booltoint-int-on-float-port) not round.
  {
    float r = evalB2I(1.0f, 5.9f, 0.0f);
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-booltoint] BoolToInt(true,5.9,0)=%.6f want=5.000000 (trunc-toward-zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
