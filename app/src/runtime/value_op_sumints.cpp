// SumInts value op (value-op self-registration seam leaf — MultiInput<int> sum).
// TiXL authority: Operators/Lib/numbers/int/basic/SumInts.cs.
//
//   SumInts.cs Update():
//     Result.Value = 0;
//     var connectedCount = 0;
//     foreach (var input in InputValues.GetCollectedTypedInputs()) {
//         Result.Value += input.GetValue(context);   // int += int
//         connectedCount++;
//     }
//     if (connectedCount == 0)
//         Result.Value = InputValues.GetValue(context);   // fall back to the primary/default value
//     InputValues.DirtyFlag.Clear();
//
//   Ports: InputValues = MultiInputSlot<int> (the ONE variable-length list, default value 0).
//   Output: Result (Slot<int>).
//
// EVAL-SIDE LAYOUT (single-MultiInput convention): the resident gather expands the ONE multiInput
// port (Values) into the WHOLE in[] (no trailing regular ports, unlike PickFloat). So in[] =
// [Values…], count = n, Result = sum of all n truncated-to-int inputs. The flat evalFloat path does
// NOT expand multiInput (one slot per port), so the golden below exercises the RESIDENT path
// (buildEvalGraph + evalResidentFloat), exactly like value_op_pickfloat.cpp / value_op_addints.cpp.
//
// FORKS (named):
//   - fork-sumints-int-on-float-port: TiXL InputValues/Result are `int`. This runtime has only Float
//     value ports, so each input is read as a Float and truncated to int via (int) BEFORE summing,
//     then the int sum is emitted as a Float. The (int) cast = C# truncation toward zero (NOT floor)
//     — identical to the convention already shipped for AddInts (value_op_addints.cpp) and Floor.cs
//     (value_eval_ops.cpp evalFloor: `(float)(int)in[0]`). For whole-number inputs (the only ones an
//     int slider produces in TiXL) this is byte-identical: 3+4+5 = 12, etc. Fractional Float inputs
//     (which TiXL's int slot cannot hold) truncate toward zero before summing, matching the C# (int)
//     cast TiXL would apply if it ever saw one.
//   - fork-sumints-empty-equals-default: TiXL distinguishes "connectedCount==0 → Result = the
//     primary default value" from "≥1 connected → Result = sum of connected". In this runtime the
//     resident gather always yields at least the primary default slot (count≥1, never empty), so the
//     two TiXL branches CONVERGE here: with exactly one slot (the primary default, value 0) the sum
//     IS the default (0+nothing = 0 = InputValues default). Summing the gathered in[] therefore
//     reproduces both TiXL branches without a special-case — the empty branch can't arise. (Same
//     gather-yields-primary invariant relied on by value_op_pickfloat.cpp's empty fork.)
//   - fork-sumints-float-mantissa: a Float exactly represents integers only up to 2^24; beyond that
//     the int sum can lose low bits the C# `int` would keep. Inherent to the runtime's single Float
//     value spine (every int-typed value op shares it), not a behavioural choice. In the int
//     slider's practical range it never bites.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runSumIntsSelfTest(bool injectBug);

namespace {

// in[] = [Values…] — the whole multiInput list (no trailing regular ports).
// Result = sum over i of (int)in[i], emitted as Float  (TiXL SumInts.cs, int semantics).
float evalSumInts(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;  // fork-sumints-empty-equals-default (gather never empty; defensive)
  int sum = 0;
  for (int i = 0; i < n; ++i)
    sum += (int)in[i];  // fork-sumints-int-on-float-port (truncate toward zero, C# (int) cast)
  return (float)sum;
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_sumints{
    // SumInts (TiXL Lib.numbers.int.basic.SumInts): sum a MultiInput<int> list.
    // Single multiInput port (Values) — port order MUST match evalSumInts's in[] read.
    // Default value 0 (MultiInputSlot<int> default).
    {"SumInts", "SumInts",
     {{"Values", "Values", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Float", false}},
     evalSumInts},
    "sumints", runSumIntsSelfTest};

// --- SumInts MATH golden (resident path — multiInput needs the resident gather) -----------------
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol siAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(v1), Const(v2), … → SumInts.Values (multiInput) }. Result = sum of (int)vi.
//   [3,4,5]      → 12          (typical multi-input sum)
//   [10]         → 10          (single connection — exercises gather-yields-primary path)
//   [-7,8,-2]    → -1          (mixed sign)
//   [3.9,4.9,5.9]→ 12          (FORK trunc toward zero: 3+4+5, NOT round)
//   [3.9,-2.9]   → 1           (FORK trunc toward zero negative: 3 + (-2))
// A gather that dropped extras (read only the primary wire) would yield 3 / 10 / -7 — NOT these
// sums. injectBug flips the typical expectation to a wrong value so the tooth bites RED.
int runSumIntsSelfTest(bool injectBug) {
  Symbol cst = siAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  Symbol sum = siAtomic("SumInts", {{"Values", "Values", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires N Const into SumInts.Values and returns the summed output.
  auto sumOf = [&](std::vector<float> vals) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[sum.id] = sum;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    const int sumChildId = 100;
    SymbolChild sm; sm.id = sumChildId; sm.symbolId = "SumInts";
    root.children.clear();
    for (size_t i = 0; i < vals.size(); ++i) {
      SymbolChild c; c.id = (int)(i + 1); c.symbolId = "Const"; c.overrides["value"] = vals[i];
      root.children.push_back(c);
    }
    root.children.push_back(sm);
    root.connections.clear();
    for (size_t i = 0; i < vals.size(); ++i)
      root.connections.push_back({(int)(i + 1), "out", sumChildId, "Values"});  // multiInput sources
    root.connections.push_back({sumChildId, "out", kSymbolBoundary, "out"});
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 1e-4f;

  // TYPICAL: [3,4,5] → 12. injectBug asserts a WRONG sum (e.g. dropped extras → 3) → flips RED.
  {
    float r = sumOf({3.0f, 4.0f, 5.0f});
    float want = injectBug ? 3.0f : 12.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-sumints] Sum([3,4,5])=%.4f want=%.4f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // SINGLE connection: [10] → 10 (gather-yields-primary; converges with TiXL empty→default branch).
  {
    float r = sumOf({10.0f});
    bool pass = std::fabs(r - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-sumints] Sum([10])=%.4f want=10.0000 (single) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // MIXED SIGN: [-7,8,-2] → -1.
  {
    float r = sumOf({-7.0f, 8.0f, -2.0f});
    bool pass = std::fabs(r - (-1.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-sumints] Sum([-7,8,-2])=%.4f want=-1.0000 (mixed sign) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-sumints-int-on-float-port: fractional inputs truncate toward zero BEFORE summing.
  //   (int)3.9 + (int)4.9 + (int)5.9 = 3 + 4 + 5 = 12 (NOT round-to-15, NOT 14.7).
  {
    float r = sumOf({3.9f, 4.9f, 5.9f});
    bool pass = std::fabs(r - 12.0f) < eps;
    ok = ok && pass;
    printf("[selftest-sumints] Sum([3.9,4.9,5.9])=%.4f want=12.0000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  // Negative truncates toward zero: (int)-2.9 = -2. So 3.9 + (-2.9) → 3 + (-2) = 1.
  {
    float r = sumOf({3.9f, -2.9f});
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-sumints] Sum([3.9,-2.9])=%.4f want=1.0000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
