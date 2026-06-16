// MultiplyInts value op (value-op self-registration seam leaf — MultiInput<int> product).
// TiXL authority: Operators/Lib/numbers/int/basic/MultiplyInts.cs.
//
//   MultiplyInts.cs Update():
//     Result.Value = 1;
//     var connectedCount = 0;
//     var total = 1;
//     foreach (var input in InputValues.GetCollectedTypedInputs()) {
//         total *= input.GetValue(context);   // int *= int
//         connectedCount++;
//     }
//     Result.Value = connectedCount == 0 ? 0 : total;   // EMPTY → 0 (NOT the identity 1)
//     InputValues.DirtyFlag.Clear();
//
//   Ports: InputValues = MultiInputSlot<int> (the ONE variable-length list, default value 0).
//   Output: Result (Slot<int>).
//
// EVAL-SIDE LAYOUT (single-MultiInput convention, mirrors value_op_sumints.cpp): the resident
// gather expands the ONE multiInput port (Values) into the WHOLE in[] (no trailing regular ports,
// unlike PickFloat). So in[] = [Values…], count = n, Result = product of all n truncated-to-int
// inputs. The flat evalFloat path does NOT expand multiInput (one slot per port), so the golden
// below exercises the RESIDENT path (buildEvalGraph + evalResidentFloat), exactly like
// value_op_sumints.cpp / value_op_pickfloat.cpp.
//
// FORKS (named):
//   - fork-multiplyints-int-on-float-port: TiXL InputValues/Result are `int`. This runtime has only
//     Float value ports, so each input is read as a Float and truncated to int via (int) BEFORE
//     multiplying, then the int product is emitted as a Float. The (int) cast = C# truncation toward
//     zero (NOT floor) — identical to the convention shipped for SumInts (value_op_sumints.cpp),
//     AddInts (value_op_addints.cpp) and Floor.cs (value_eval_ops.cpp evalFloor: `(float)(int)in[0]`).
//     For whole-number inputs (the only ones an int slider produces in TiXL) this is byte-identical:
//     3*4*5 = 60, etc. Fractional Float inputs (which TiXL's int slot cannot hold) truncate toward
//     zero before multiplying, matching the C# (int) cast TiXL would apply if it ever saw one.
//   - fork-multiplyints-empty-equals-zero: TiXL writes Result = 0 when connectedCount==0 (a FAITHFUL
//     port — NOT the math identity 1). In this runtime the resident gather always yields at least the
//     primary default slot (count≥1, never empty), so the TiXL connectedCount==0 branch cannot arise
//     via the resident path: with exactly one slot (the primary default, value 0) the product IS
//     (int)0 = 0, which already coincides with TiXL's empty→0. (Same gather-yields-primary invariant
//     relied on by value_op_sumints.cpp.) The defensive `if (n < 1) return 0` below still encodes the
//     faithful empty→0 (NOT 1) in case a future flat/empty path ever reaches it.
//   - fork-multiplyints-int-overflow: a Float exactly represents integers only up to 2^24, and the
//     int product can also overflow a 32-bit int (C# `int` wraps; products grow fast). Both are
//     inherent to the runtime's single Float value spine (every int-typed value op shares it), not a
//     behavioural choice. In the int slider's practical range it never bites.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runMultiplyIntsSelfTest(bool injectBug);

namespace {

// in[] = [Values…] — the whole multiInput list (no trailing regular ports).
// Result = product over i of (int)in[i], emitted as Float  (TiXL MultiplyInts.cs, int semantics).
float evalMultiplyInts(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;  // fork-multiplyints-empty-equals-zero (faithful: empty→0, NOT identity 1)
  int total = 1;
  for (int i = 0; i < n; ++i)
    total *= (int)in[i];  // fork-multiplyints-int-on-float-port (truncate toward zero, C# (int) cast)
  return (float)total;
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_multiplyints{
    // MultiplyInts (TiXL Lib.numbers.int.basic.MultiplyInts): product of a MultiInput<int> list.
    // Single multiInput port (Values) — port order MUST match evalMultiplyInts's in[] read.
    // Default value 0 (MultiInputSlot<int> default).
    {"MultiplyInts", "MultiplyInts",
     {{"Values", "Values", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Float", false}},
     evalMultiplyInts},
    "multiplyints", runMultiplyIntsSelfTest};

// --- MultiplyInts MATH golden (resident path — multiInput needs the resident gather) ------------
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol miAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(v1), Const(v2), … → MultiplyInts.Values (multiInput) }. Result = product of (int)vi.
//   [3,4,5]      → 60          (typical multi-input product)
//   [10]         → 10          (single connection — exercises gather-yields-primary path)
//   [-7,2,-3]    → 42          (mixed sign: -7*2*-3 = 42)
//   [3,0,9]      → 0           (any zero factor zeros the product)
//   [3.9,4.9,5.9]→ 60          (FORK trunc toward zero: 3*4*5, NOT round → 4*5*6=120)
//   [3.9,-2.9]   → -6          (FORK trunc toward zero negative: 3 * (-2))
// A gather that dropped extras (read only the primary wire) would yield 3 / 10 / -7 — NOT these
// products. injectBug flips the typical expectation to a wrong value so the tooth bites RED.
int runMultiplyIntsSelfTest(bool injectBug) {
  Symbol cst = miAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  Symbol mul = miAtomic("MultiplyInts", {{"Values", "Values", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires N Const into MultiplyInts.Values and returns the product output.
  auto productOf = [&](std::vector<float> vals) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[mul.id] = mul;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    const int mulChildId = 100;
    SymbolChild mm; mm.id = mulChildId; mm.symbolId = "MultiplyInts";
    root.children.clear();
    for (size_t i = 0; i < vals.size(); ++i) {
      SymbolChild c; c.id = (int)(i + 1); c.symbolId = "Const"; c.overrides["value"] = vals[i];
      root.children.push_back(c);
    }
    root.children.push_back(mm);
    root.connections.clear();
    for (size_t i = 0; i < vals.size(); ++i)
      root.connections.push_back({(int)(i + 1), "out", mulChildId, "Values"});  // multiInput sources
    root.connections.push_back({mulChildId, "out", kSymbolBoundary, "out"});
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 1e-4f;

  // TYPICAL: [3,4,5] → 60. injectBug asserts a WRONG product (e.g. dropped extras → 3) → flips RED.
  {
    float r = productOf({3.0f, 4.0f, 5.0f});
    float want = injectBug ? 3.0f : 60.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyints] Product([3,4,5])=%.4f want=%.4f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // SINGLE connection: [10] → 10 (gather-yields-primary; coincides with TiXL connectedCount==1).
  {
    float r = productOf({10.0f});
    bool pass = std::fabs(r - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyints] Product([10])=%.4f want=10.0000 (single) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // MIXED SIGN: [-7,2,-3] → (-7)*2*(-3) = 42 (even count of negatives → positive).
  {
    float r = productOf({-7.0f, 2.0f, -3.0f});
    bool pass = std::fabs(r - 42.0f) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyints] Product([-7,2,-3])=%.4f want=42.0000 (mixed sign) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // ZERO FACTOR: [3,0,9] → 0 (any zero factor zeros the whole product).
  {
    float r = productOf({3.0f, 0.0f, 9.0f});
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyints] Product([3,0,9])=%.4f want=0.0000 (zero factor) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-multiplyints-int-on-float-port: fractional inputs truncate toward zero BEFORE the
  //   product. (int)3.9 * (int)4.9 * (int)5.9 = 3 * 4 * 5 = 60 (NOT round → 4*5*6=120, NOT 112.9…).
  {
    float r = productOf({3.9f, 4.9f, 5.9f});
    bool pass = std::fabs(r - 60.0f) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyints] Product([3.9,4.9,5.9])=%.4f want=60.0000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  // Negative truncates toward zero: (int)-2.9 = -2. So 3.9 * (-2.9) → 3 * (-2) = -6.
  {
    float r = productOf({3.9f, -2.9f});
    bool pass = std::fabs(r - (-6.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-multiplyints] Product([3.9,-2.9])=%.4f want=-6.0000 (trunc toward zero) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
