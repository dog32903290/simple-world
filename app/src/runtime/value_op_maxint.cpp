// MaxInt value op (value-op self-registration seam leaf — MultiInput<int> max).
// TiXL authority: Operators/Lib/numbers/int/process/MaxInt.cs.
//
//   MaxInt.cs Update():
//     var max = Int32.MinValue;
//     foreach (var i in Ints.CollectedInputs)
//         max = Math.Max(max, i.GetValue(context));   // int max
//     Ints.DirtyFlag.Clear();
//     Result.Value = max;
//
//   Ports: Ints = MultiInputSlot<int> (the ONE variable-length list, default value 0).
//   Output: Result (Slot<int>).
//
// EVAL-SIDE LAYOUT (single-MultiInput convention, exact twin of value_op_sumints.cpp): the resident
// gather expands the ONE multiInput port (Ints) into the WHOLE in[] (no trailing regular ports,
// unlike PickFloat). So in[] = [Ints…], count = n, Result = max of all n truncated-to-int inputs.
// The flat evalFloat path does NOT expand multiInput (one slot per port), so the golden below
// exercises the RESIDENT path (buildEvalGraph + evalResidentFloat), exactly like
// value_op_sumints.cpp / value_op_pickfloat.cpp.
//
// FORKS (named):
//   - fork-maxint-int-on-float-port: TiXL Ints/Result are `int`. This runtime has only Float value
//     ports, so each input is read as a Float and truncated to int via (int) BEFORE the max, then
//     the int max is emitted as a Float. The (int) cast = C# truncation toward zero (NOT floor) —
//     identical to the convention already shipped for SumInts/SubInts/AddInts (value_op_*.cpp) and
//     Floor.cs (value_eval_ops.cpp evalFloor: `(float)(int)in[0]`). For whole-number inputs (the
//     only ones an int slider produces in TiXL) this is byte-identical: max(3,7,5) = 7, etc.
//     Fractional Float inputs (which TiXL's int slot cannot hold) truncate toward zero BEFORE the
//     max, matching the C# (int) cast TiXL would apply if it ever saw one. NOTE: truncation runs
//     per-element before max, so e.g. max((int)2.9=2, (int)2.1=2)=2 — never the pre-trunc 2.9.
//   - fork-maxint-empty-zero: TiXL seeds the accumulator with Int32.MinValue and, if NO inputs are
//     connected (CollectedInputs empty), emits that Int32.MinValue. This runtime's resident gather
//     ALWAYS yields at least the primary default slot (count≥1, never empty), so the empty branch
//     cannot arise here — we seed the accumulator from in[0] (the primary default), so Int32.MinValue
//     is never observed and never emitted. For the purely defensive n<1 path (which the gather can't
//     reach) we return 0, NOT Int32.MinValue: a Float cannot exactly represent Int32.MinValue
//     (-2147483648), and emitting a ±2.1e9 sentinel into this runtime's Float value spine /
//     inspector / Metal cbuffers would be a poison value. 0 is the neutral the rest of the value
//     spine uses (same spirit as the Sin period-zero → Offset and SumInts empty → default forks).
//   - fork-maxint-float-mantissa: a Float exactly represents integers only up to 2^24; beyond that
//     a max'd int value can lose low bits the C# `int` would keep. Inherent to the runtime's single
//     Float value spine (every int-typed value op shares it), not a behavioural choice. In the int
//     slider's practical range it never bites.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runMaxIntSelfTest(bool injectBug);

namespace {

// in[] = [Ints…] — the whole multiInput list (no trailing regular ports).
// Result = max over i of (int)in[i], emitted as Float  (TiXL MaxInt.cs, int semantics).
float evalMaxInt(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;  // fork-maxint-empty-zero (gather never empty; defensive — see header)
  int max = (int)in[0];    // seed from primary default slot (Int32.MinValue never observed)
  for (int i = 1; i < n; ++i)
    max = std::max(max, (int)in[i]);  // fork-maxint-int-on-float-port (trunc toward zero per element)
  return (float)max;
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_maxint{
    // MaxInt (TiXL Lib.numbers.int.process.MaxInt): max of a MultiInput<int> list.
    // Single multiInput port (Ints) — port order MUST match evalMaxInt's in[] read.
    // Default value 0 (MultiInputSlot<int> default).
    {"MaxInt", "MaxInt",
     {{"Ints", "Ints", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Float", false}},
     evalMaxInt},
    "maxint", runMaxIntSelfTest};

// --- MaxInt MATH golden (resident path — multiInput needs the resident gather) ------------------
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol miAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(v1), Const(v2), … → MaxInt.Ints (multiInput) }. Result = max of (int)vi.
//   [3,7,5]      → 7           (typical multi-input max — last is NOT the max, first is NOT the max)
//   [10]         → 10          (single connection — exercises gather-yields-primary path)
//   [-7,-2,-9]   → -2          (all negative: max is the least-negative, NOT 0/seed)
//   [2.9,2.1]    → 2           (FORK trunc-per-element BEFORE max: max(2,2)=2, NOT 2.9)
//   [3.9,-2.9]   → 3           (FORK trunc toward zero: max(3, -2) = 3)
// A reduce that summed, min'd, or read only the primary wire would yield 15 / 3 / 3 / different —
// NOT these maxes. injectBug flips the typical expectation to a wrong value so the tooth bites RED.
int runMaxIntSelfTest(bool injectBug) {
  Symbol cst = miAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  Symbol mx = miAtomic("MaxInt", {{"Ints", "Ints", "Float", 0.0f}},
                       {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires N Const into MaxInt.Ints and returns the max output.
  auto maxOf = [&](std::vector<float> vals) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[mx.id] = mx;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    const int maxChildId = 100;
    SymbolChild mc; mc.id = maxChildId; mc.symbolId = "MaxInt";
    root.children.clear();
    for (size_t i = 0; i < vals.size(); ++i) {
      SymbolChild c; c.id = (int)(i + 1); c.symbolId = "Const"; c.overrides["value"] = vals[i];
      root.children.push_back(c);
    }
    root.children.push_back(mc);
    root.connections.clear();
    for (size_t i = 0; i < vals.size(); ++i)
      root.connections.push_back({(int)(i + 1), "out", maxChildId, "Ints"});  // multiInput sources
    root.connections.push_back({maxChildId, "out", kSymbolBoundary, "out"});
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 1e-4f;

  // TYPICAL: [3,7,5] → 7. injectBug asserts a WRONG max (e.g. read primary wire only → 3) → RED.
  {
    float r = maxOf({3.0f, 7.0f, 5.0f});
    float want = injectBug ? 3.0f : 7.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-maxint] Max([3,7,5])=%.4f want=%.4f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // SINGLE connection: [10] → 10 (gather-yields-primary; converges with TiXL empty→seed branch).
  {
    float r = maxOf({10.0f});
    bool pass = std::fabs(r - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-maxint] Max([10])=%.4f want=10.0000 (single) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // ALL NEGATIVE: [-7,-2,-9] → -2. Guards against a seed bug (e.g. seeding 0 → would wrongly give 0).
  {
    float r = maxOf({-7.0f, -2.0f, -9.0f});
    bool pass = std::fabs(r - (-2.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-maxint] Max([-7,-2,-9])=%.4f want=-2.0000 (all negative) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-maxint-int-on-float-port: trunc-per-element BEFORE max. (int)2.9=(int)2.1=2 → max=2.
  // A max-before-trunc bug would give 2.9 → catches the wrong ordering of trunc vs max.
  {
    float r = maxOf({2.9f, 2.1f});
    bool pass = std::fabs(r - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-maxint] Max([2.9,2.1])=%.4f want=2.0000 (trunc-per-element) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  // Negative truncates toward zero: (int)-2.9 = -2, (int)3.9 = 3 → max(3,-2) = 3.
  {
    float r = maxOf({3.9f, -2.9f});
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-maxint] Max([3.9,-2.9])=%.4f want=3.0000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
