// BoolToFloat value op (value-op self-registration seam leaf — Phase C numbers/bool mining).
// TiXL authority: Operators/Lib/numbers/bool/convert/BoolToFloat.cs (+ BoolToFloat.t3 defaults).
//
//   BoolToFloat.cs Update():
//     var trueValue  = ForTrue.GetValue(context);
//     var falseValue = ForFalse.GetValue(context);
//     Result.Value = BoolValue.GetValue(context) ? trueValue : falseValue;
//
//   BoolToFloat.t3 DefaultValues: ForTrue=1.0, ForFalse=0.0, BoolValue=false.
//     (Default eval: false → ForFalse = 0.0.)
//
// 3 inputs (BoolValue/ForTrue/ForFalse) → 1 float output (Result). Pure stateless value op:
// behaviour is entirely the evaluate fn, registered via the ValueOp seam (no GPU cook).
//
// BOOL-AS-FLOAT CONVENTION (same as Not / Xor / Or / And / All / Any already shipped):
//   - bool INPUT read as `(in[N] != 0.0f)` — non-zero is true.
//   - ForTrue / ForFalse are plain float inputs — no conversion needed.
//   - BoolValue port carries Widget::Bool + range [0,1].
//
// FORKS (named):
//   None. The ternary maps cleanly; no divide-by-zero or NaN path exists.
//   ForTrue/ForFalse are float-typed in TiXL; this runtime's Float value spine stores them
//   identically — no precision fork.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runBoolToFloatSelfTest(bool injectBug);

namespace {

// in[] order = port spec order: BoolValue, ForTrue, ForFalse.
// Result = BoolValue ? ForTrue : ForFalse  (TiXL BoolToFloat.cs verbatim).
// bool-as-float: BoolValue != 0 is true.
float evalBoolToFloatOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const bool bv     = (in[0] != 0.0f);  // bool-as-float convention
  const float trueV  = in[1];
  const float falseV = in[2];
  return bv ? trueV : falseV;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_booltofloat{
    // BoolToFloat (TiXL Lib.numbers.bool.convert.BoolToFloat):
    //   Result = BoolValue ? ForTrue : ForFalse.
    // Port order MUST match evalBoolToFloatOp's in[] read: BoolValue, ForTrue, ForFalse, then out.
    // Defaults from BoolToFloat.t3: ForTrue=1.0, ForFalse=0.0, BoolValue=false(0).
    {"BoolToFloat", "BoolToFloat",
     {{"BoolValue", "BoolValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"ForTrue",   "ForTrue",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"ForFalse",  "ForFalse",  "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      {"out",       "out",       "Float", false}},
     evalBoolToFloatOp},
    "booltofloat", runBoolToFloatSelfTest};

// --- BoolToFloat MATH golden -------------------------------------------------------------------
// Builds a 1-node BoolToFloat graph, sets params, pulls "out" via evalFloat, and compares to
// the hand-derived TiXL formula Result = BoolValue ? ForTrue : ForFalse.
// injectBug swaps the expected true/false branch so the typical-case assertion flips RED.
int runBoolToFloatSelfTest(bool injectBug) {
  const float eps = 1e-6f;
  bool ok = true;

  // Helper: evaluate BoolToFloat with explicit params.
  auto evalB2F = [&](float boolValue, float forTrue, float forFalse) -> float {
    const NodeSpec* spec = findSpec("BoolToFloat");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "BoolToFloat";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["BoolValue"] = boolValue;
    g.node(nid)->params["ForTrue"]   = forTrue;
    g.node(nid)->params["ForFalse"]  = forFalse;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL (true branch): BoolValue=1, ForTrue=5.0, ForFalse=2.0 → 5.0.
  // injectBug expects 2.0 (the false branch) → flips RED, proving the branch is tested.
  {
    float r = evalB2F(1.0f, 5.0f, 2.0f);
    float want = injectBug ? 2.0f : 5.0f;  // bug: returns ForFalse instead of ForTrue
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-booltofloat] BoolToFloat(true,5,2)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // TYPICAL (false branch): BoolValue=0, ForTrue=5.0, ForFalse=2.0 → 2.0.
  {
    float r = evalB2F(0.0f, 5.0f, 2.0f);
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-booltofloat] BoolToFloat(false,5,2)=%.6f want=2.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // DEFAULT: BoolToFloat.t3 BoolValue=false, ForTrue=1.0, ForFalse=0.0 → 0.0.
  {
    float r = evalB2F(0.0f, 1.0f, 0.0f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-booltofloat] BoolToFloat(defaults)=%.6f want=0.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: any non-zero BoolValue is treated as true (bool-as-float convention).
  // BoolValue=0.5, ForTrue=7.0, ForFalse=3.0 → 7.0  (0.5 != 0 → true → ForTrue).
  {
    float r = evalB2F(0.5f, 7.0f, 3.0f);
    bool pass = std::fabs(r - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-booltofloat] BoolToFloat(0.5=true,7,3)=%.6f want=7.000000 (nonzero->true) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
