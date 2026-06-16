// MinInt value op (value-op self-registration seam leaf — MultiInput<int> minimum).
// TiXL authority: Operators/Lib/numbers/int/process/MinInt.cs.
//
//   MinInt.cs Update():
//     var min = Int32.MaxValue;
//     foreach (var i in Ints.CollectedInputs)
//         min = Math.Min(min, i.GetValue(context));   // int min over the collected list
//     Result.Value = min;
//
//   Ports: Ints = MultiInputSlot<int> (the ONE variable-length list).
//   Output: Result (Slot<int>).
//
// EVAL-SIDE LAYOUT (single-MultiInput convention, identical to value_op_sumints.cpp): the resident
// gather expands the ONE multiInput port (Ints) into the WHOLE in[] (no trailing regular ports,
// unlike PickFloat). So in[] = [Ints…], count = n, Result = min over all n truncated-to-int inputs.
// The flat evalFloat path does NOT expand multiInput (one slot per port), so the golden below
// exercises the RESIDENT path (buildEvalGraph + evalResidentFloat), exactly like
// value_op_sumints.cpp / value_op_pickfloat.cpp.
//
// FORKS (named):
//   - fork-minint-int-on-float-port: TiXL Ints/Result are `int`. This runtime has only Float value
//     ports, so each input is read as a Float and truncated to int via (int) BEFORE the min, then
//     the int min is emitted as a Float. The (int) cast = C# truncation toward zero (NOT floor) —
//     identical to the convention already shipped for SumInts (value_op_sumints.cpp) and Floor.cs
//     (value_eval_ops.cpp evalFloor: `(float)(int)in[0]`). For whole-number inputs (the only ones an
//     int slider produces in TiXL) this is byte-identical. Fractional Float inputs (which TiXL's int
//     slot cannot hold) truncate toward zero before the min, matching the C# (int) cast TiXL would
//     apply if it ever saw one.
//   - fork-minint-empty-zero: TiXL seeds `min = Int32.MaxValue`, so an EMPTY input list yields
//     Int32.MaxValue (2147483647 ≈ 2.1e9). In this runtime the resident gather always yields at
//     least the primary default slot (count≥1, never empty), so the TiXL empty branch cannot arise
//     here; the defensive n<1 path returns 0 — the same neutral the rest of the Float value spine
//     uses (value_op_sumints.cpp / value_op_pickfloat.cpp), NOT Int32.MaxValue. With ≥1 connection
//     (the only reachable state) the seed Int32.MaxValue is always dominated by the first real
//     input, so the seed is invisible: min({v…}) = the actual list minimum, byte-identical to TiXL.
//   - fork-minint-float-mantissa: a Float exactly represents integers only up to 2^24; beyond that
//     a returned min near Int32 range can lose low bits the C# `int` would keep. Inherent to the
//     runtime's single Float value spine (every int-typed value op shares it), not a behavioural
//     choice. In the int slider's practical range it never bites.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <algorithm>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runMinIntSelfTest(bool injectBug);

namespace {

// in[] = [Ints…] — the whole multiInput list (no trailing regular ports).
// Result = min over i of (int)in[i], emitted as Float  (TiXL MinInt.cs, int semantics).
float evalMinInt(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;  // fork-minint-empty-zero (gather never empty; defensive ≠ Int32.MaxValue)
  int mn = (int)in[0];     // seed with first real input — TiXL's Int32.MaxValue seed is dominated by
                           // any connected input, so seeding from in[0] is byte-identical for n≥1.
  for (int i = 1; i < n; ++i)
    mn = std::min(mn, (int)in[i]);  // fork-minint-int-on-float-port (truncate toward zero, C# (int))
  return (float)mn;
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_minint{
    // MinInt (TiXL Lib.numbers.int.process.MinInt): min of a MultiInput<int> list.
    // Single multiInput port (Ints) — port order MUST match evalMinInt's in[] read.
    // Default value 0 (MultiInputSlot<int> default).
    {"MinInt", "MinInt",
     {{"Ints", "Ints", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Float", false}},
     evalMinInt},
    "minint", runMinIntSelfTest};

// --- MinInt MATH golden (resident path — multiInput needs the resident gather) ------------------
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol miAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(v1), Const(v2), … → MinInt.Ints (multiInput) }. Result = min of (int)vi.
//   [3,4,5]      → 3           (typical multi-input min)
//   [10]         → 10          (single connection — exercises gather-yields-primary path)
//   [-7,8,-2]    → -7          (mixed sign, negative wins)
//   [3.9,4.1,5.9]→ 3           (FORK trunc toward zero: min(3,4,5)=3, NOT min(3.9,…)=3.9)
//   [-2.9,3.9]   → -2          (FORK trunc toward zero negative: min((int)-2.9=-2, (int)3.9=3) = -2)
// A gather that dropped extras (read only the primary wire) would yield 3 / 10 / -7 for the first
// three — but [3,4,5] and [-7,8,-2] specifically distinguish min from a primary-only read only when
// the minimum is NOT the first element: [4,3,5]→3 and [8,-7,-2]→-7 below pin that. injectBug flips
// the typical expectation to a wrong value so the tooth bites RED.
int runMinIntSelfTest(bool injectBug) {
  Symbol cst = miAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  Symbol mn = miAtomic("MinInt", {{"Ints", "Ints", "Float", 0.0f}},
                       {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires N Const into MinInt.Ints and returns the min output.
  auto minOf = [&](std::vector<float> vals) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[mn.id] = mn;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    const int minChildId = 100;
    SymbolChild mc; mc.id = minChildId; mc.symbolId = "MinInt";
    root.children.clear();
    for (size_t i = 0; i < vals.size(); ++i) {
      SymbolChild c; c.id = (int)(i + 1); c.symbolId = "Const"; c.overrides["value"] = vals[i];
      root.children.push_back(c);
    }
    root.children.push_back(mc);
    root.connections.clear();
    for (size_t i = 0; i < vals.size(); ++i)
      root.connections.push_back({(int)(i + 1), "out", minChildId, "Ints"});  // multiInput sources
    root.connections.push_back({minChildId, "out", kSymbolBoundary, "out"});
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 1e-4f;

  // TYPICAL: [4,3,5] → 3 (min is NOT the first element → distinguishes min from primary-only read).
  // injectBug asserts a WRONG min (e.g. primary-only → 4) → flips RED.
  {
    float r = minOf({4.0f, 3.0f, 5.0f});
    float want = injectBug ? 4.0f : 3.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-minint] Min([4,3,5])=%.4f want=%.4f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // SINGLE connection: [10] → 10 (gather-yields-primary; the only reachable empty-equivalent state,
  // converges with TiXL's Int32.MaxValue-seed path since the seed is dominated by the one input).
  {
    float r = minOf({10.0f});
    bool pass = std::fabs(r - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-minint] Min([10])=%.4f want=10.0000 (single) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // MIXED SIGN (negative wins, NOT first element): [8,-7,-2] → -7.
  {
    float r = minOf({8.0f, -7.0f, -2.0f});
    bool pass = std::fabs(r - (-7.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-minint] Min([8,-7,-2])=%.4f want=-7.0000 (mixed sign) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-minint-int-on-float-port: fractional inputs truncate toward zero BEFORE the min.
  //   min((int)3.9, (int)4.1, (int)5.9) = min(3,4,5) = 3 (NOT min(3.9,4.1,5.9)=3.9).
  {
    float r = minOf({3.9f, 4.1f, 5.9f});
    bool pass = std::fabs(r - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-minint] Min([3.9,4.1,5.9])=%.4f want=3.0000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  // Negative truncates toward zero: (int)-2.9 = -2, (int)3.9 = 3. So min(-2,3) = -2 (NOT -2.9, NOT
  // floor -3). Pins that truncation is toward-zero, not floor, before the min.
  {
    float r = minOf({-2.9f, 3.9f});
    bool pass = std::fabs(r - (-2.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-minint] Min([-2.9,3.9])=%.4f want=-2.0000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
