// Xor value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/bool/logic/Xor.cs.
//
//   Xor.cs Update():
//     var a = A.GetValue(context);
//     Result.Value = B.GetValue(context) ? !a : a;
//
//   Ports: A = InputSlot<bool> (no ctor default → false); B = InputSlot<bool> (no ctor default →
//   false). Output: Result (Slot<bool>).
//
// This is a faithful XOR. Truth table of TiXL's `B ? !a : a` (verified, NOT assumed):
//   A=0,B=0 → B false → a → 0     xor(0,0)=0  ✓
//   A=1,B=0 → B false → a → 1     xor(1,0)=1  ✓
//   A=0,B=1 → B true  → !a → 1    xor(0,1)=1  ✓
//   A=1,B=1 → B true  → !a → 0    xor(1,1)=0  ✓
// So `B ? !a : a` == `a XOR b` exactly. No fork on the math.
//
// FORKS (named):
//   - fork-xor-bool-as-float: this runtime has NO Bool port type (Cut 32 decision — bool inputs are
//     stored as Float, bool outputs dissolved to Float 0/1). So A/B are read as bool via the
//     existing runtime convention `(value != 0.0f) == true` (matches Invert.cs port handling at
//     value_eval_ops.cpp:369 `bool shouldInvert = (in[1] != 0.0f);`), and Result is emitted as
//     1.0f/0.0f (matches IsGreater.cs handling at value_eval_ops.cpp:576). The Float→bool read
//     treats any non-zero (incl. negatives / fractional) as true, which is the C# truthiness a
//     bool slot would already have produced upstream; the boolean math itself is byte-identical.
//   - fork-xor-default-false: TiXL A/B are InputSlot<bool> with no ctor default → C# bool default
//     is false. We mirror with Float default 0.0f and Widget::Bool (same widget the runtime uses
//     for the existing bool inputs Reset/Freeze in node_registry_math.cpp). Default eval = 0 XOR 0
//     = 0, byte-identical to TiXL's default Xor.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runXorSelfTest(bool injectBug);

namespace {

// in[] order = the Float input ports in spec order: A, B.
// Result = (B ? !a : a) as Float 0/1  (TiXL Xor.cs verbatim, bool dissolved to Float — see header).
float evalXorOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const bool a = (in[0] != 0.0f);  // fork-xor-bool-as-float (see header)
  const bool b = (in[1] != 0.0f);
  const bool result = b ? !a : a;  // TiXL Xor.cs line 17: "B ? !a : a"
  return result ? 1.0f : 0.0f;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_xor{
    // Xor (TiXL Lib.numbers.bool.logic.Xor): Result = B ? !a : a (== a XOR b).
    // Port order MUST match evalXorOp's in[] read: A, B, then out.
    // Defaults: A=false(0), B=false(0). Bool inputs → Widget::Bool (runtime bool convention).
    {"Xor", "Xor",
     {{"A", "A", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"B", "B", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"Result", "Result", "Float", false}},
     evalXorOp},
    "xor", runXorSelfTest};

// --- Xor MATH golden ----------------------------------------------------------------------------
// Builds a 1-node Xor graph, sets params, pulls "Result" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL truth table. injectBug flips ONE expected value (the
// A=1,B=1 case) so that assertion goes RED, proving the tooth bites.
int runXorSelfTest(bool injectBug) {
  const float eps = 1e-6f;
  bool ok = true;

  // Helper: evaluate Xor with explicit A/B float params.
  auto evalXor = [&](float a, float b) -> float {
    const NodeSpec* spec = findSpec("Xor");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Xor";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["A"] = a;
    g.node(nid)->params["B"] = b;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // Full truth table, hand-computed from TiXL Xor.cs "B ? !a : a":
  //   A=0,B=0 → 0 ; A=1,B=0 → 1 ; A=0,B=1 → 1 ; A=1,B=1 → 0.
  // injectBug flips the A=1,B=1 expectation from 0 to 1 → that assertion FAILS (RED).
  struct Case { float a, b, want; const char* note; };
  const Case cases[] = {
      {0.0f, 0.0f, 0.0f, "0^0=0"},
      {1.0f, 0.0f, 1.0f, "1^0=1"},
      {0.0f, 1.0f, 1.0f, "0^1=1"},
      {1.0f, 1.0f, 0.0f, "1^1=0"},  // <- injectBug target
  };
  for (const Case& c : cases) {
    float r = evalXor(c.a, c.b);
    float want = c.want;
    if (injectBug && c.a == 1.0f && c.b == 1.0f) want = 1.0f;  // bug: claim 1^1=1
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-xor] Xor(A=%.0f,B=%.0f)=%.1f want=%.1f (%s) -> %s\n",
           c.a, c.b, r, want, c.note, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY fork-xor-bool-as-float: any non-zero is truthy. A=0.3 (truthy), B=0 → !? B false → a.
  //   a = (0.3 != 0) = true → result = a = true → 1.
  {
    float r = evalXor(0.3f, 0.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-xor] Xor(A=0.3,B=0)=%.1f want=1.0 (nonzero=true) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: negative is also truthy. A=0, B=-1 (truthy) → B true → !a = !false = true → 1.
  {
    float r = evalXor(0.0f, -1.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-xor] Xor(A=0,B=-1)=%.1f want=1.0 (neg=true) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
