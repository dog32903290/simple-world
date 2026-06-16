// IsIntEven value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/int/logic/IsIntEven.cs
//
//   IsIntEven.cs Update():
//     Result.Value = Value.GetValue(context) % 2 == 0;
//
//   Ports: Value = InputSlot<int> (no source default → 0). Output: Result (Slot<bool>).
//
// 1 input (Value) → 1 output (Result). Pure stateless value op: behaviour is entirely the
// evaluate fn, registered via the ValueOp seam (no GPU cook). bool output is dissolved to
// Float 0/1, matching the existing IsGreater/Compare convention (value_eval_ops.cpp:555,
// "Bool output dissolved to Float 0/1 (Cut 32 decision: no Bool port type in this runtime)").
//
// FORKS (named):
//   - fork-isinteven-value-int: TiXL `Value` is int; this runtime has only Float ports. The Float
//     input is truncated to int via (int) BEFORE %2 — same convention as PickFloat's Index
//     (value_op_pickfloat.cpp:61, "(int)in[n-1]"). C++ (int) truncates toward zero; for the only
//     inputs that make sense (whole numbers) this is the identity, so it is byte-identical to TiXL.
//   - negative-odd correctness: C++ `%` truncates toward zero exactly like C# `%`, so the parity
//     test is identical across the sign boundary: (-3)%2 == -1 → odd (0.0f), (-4)%2 == 0 → even
//     (1.0f), matching TiXL's `int % 2 == 0`. (This is NOT a fork — C# and C++ agree; it is the
//     boundary the prompt flagged, asserted in the golden below.)
//   - fork-isinteven-stateless: TiXL's Result is a Slot<bool> that re-fires subscribers on change;
//     this runtime is pure-per-frame and emits the identical value each frame (same spirit as the
//     fork-isgreater-stateless note). The output VALUE is identical; no _lastResult dirty-flag.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runIsIntEvenSelfTest(bool injectBug);

namespace {

// in[] = [Value]. Result = ((int)Value % 2 == 0) ? 1.0f : 0.0f  (TiXL IsIntEven.cs verbatim,
// with the Float-port→int truncation fork). See header for fork names.
float evalIsIntEven(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 1.0f;  // no input → Value defaults to int 0 → 0 % 2 == 0 → even → 1.0f
  const int v = (int)in[0];  // fork-isinteven-value-int (truncate toward zero, like PickFloat Index)
  return (v % 2 == 0) ? 1.0f : 0.0f;  // TiXL IsIntEven.cs:16 "Value % 2 == 0"
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched).
static const ValueOp _reg_isinteven{
    // IsIntEven (TiXL Lib.numbers.int.logic.IsIntEven): Result = (Value % 2 == 0).
    // Port order MUST match evalIsIntEven's in[] read: Value, then out.
    // Default Value=0 (TiXL InputSlot<int> no source default). Output "out" carries bool→Float 0/1.
    {"IsIntEven", "IsIntEven",
     {{"Value", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalIsIntEven},
    "isinteven", runIsIntEvenSelfTest};

// --- IsIntEven MATH golden ---------------------------------------------------------------------
// Builds a 1-node IsIntEven graph, sets Value, pulls "out" via evalFloat (math_ops_selftest style),
// and compares to the hand-derived TiXL formula (Value % 2 == 0 ? 1 : 0). Covers even/odd typical
// plus the negative-odd / negative-even sign boundary the prompt flagged. injectBug flips the
// typical even-case expectation (asserts 0.0f where TiXL emits 1.0f) so the tooth bites RED.
int runIsIntEvenSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalEven = [&](float value) -> float {
    const NodeSpec* spec = findSpec("IsIntEven");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "IsIntEven";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value"] = value;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "out") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL even: Value=4 → 4 % 2 == 0 → true → 1.0f.
  // injectBug asserts the WRONG value 0.0f (claims 4 is odd) → flips RED.
  {
    float r = evalEven(4.0f);
    float want = injectBug ? 0.0f : 1.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-isinteven] IsIntEven(4)=%.6f want=%.6f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // TYPICAL odd: Value=3 → 3 % 2 == 1 → false → 0.0f.
  {
    float r = evalEven(3.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-isinteven] IsIntEven(3)=%.6f want=0.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY zero (default): Value=0 → 0 % 2 == 0 → true → 1.0f.
  {
    float r = evalEven(0.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-isinteven] IsIntEven(0)=%.6f want=1.000000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY negative-odd: Value=-3 → (-3) % 2 == -1 ≠ 0 → false → 0.0f.
  // (C# and C++ % both truncate toward zero, so -3 is odd in both — the prompt's correctness flag.)
  {
    float r = evalEven(-3.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-isinteven] IsIntEven(-3)=%.6f want=0.000000 (neg-odd) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY negative-even: Value=-4 → (-4) % 2 == 0 → true → 1.0f.
  {
    float r = evalEven(-4.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-isinteven] IsIntEven(-4)=%.6f want=1.000000 (neg-even) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-isinteven-value-int: Value=2.9 truncates (int)2.9==2 → even → 1.0f (toward-zero
  // truncation, not round). Locks the Float-port→int fork behaviour.
  {
    float r = evalEven(2.9f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-isinteven] IsIntEven(2.9)=%.6f want=1.000000 (trunc->2 even) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
