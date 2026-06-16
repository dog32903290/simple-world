// All value op (value-op self-registration seam leaf — MultiInput<bool> AND-reduce).
// TiXL authority: Operators/Lib/numbers/bool/combine/All.cs (+ All.t3 defaults).
//
//   All.cs Update():
//     var result = true;
//     var anyConnected = false;
//     foreach (var input in Input.GetCollectedTypedInputs()) {
//         anyConnected = true;
//         result &= input.GetValue(context);          // bool &= bool
//     }
//     Result.Value = result & anyConnected;            // empty list → false (anyConnected==false)
//     Input.DirtyFlag.Clear();
//
//   Ports: Input = MultiInputSlot<bool> (the ONE variable-length list). Output: Result (Slot<bool>).
//   All.t3 DefaultValue: false (so the primary/unwired default slot reads false).
//
// EVAL-SIDE LAYOUT (single-MultiInput convention, structural twin of value_op_sumints.cpp): the
// resident gather expands the ONE multiInput port (Input) into the WHOLE in[] (no trailing regular
// ports, unlike PickFloat). So in[] = [Input…], count = n, Result = AND of all n bool inputs. The
// flat evalFloat path does NOT expand multiInput (one slot per port), so the golden below exercises
// the RESIDENT path (buildEvalGraph + evalResidentFloat), exactly like value_op_pickfloat.cpp /
// value_op_sumints.cpp / value_op_addints.cpp.
//
// FORKS (named):
//   - fork-all-bool-on-float-port: TiXL Input/Result are `bool`. This runtime has only Float value
//     ports, so each gathered input is read as a Float and interpreted non-zero == true via
//     `(in[i] != 0.0f)` BEFORE the AND, then the bool result is emitted as Float 0/1. This is the
//     SAME bool-on-Float convention already shipped (Cut 32): InvertFloat's Invert
//     (value_eval_ops.cpp evalInvertFloat: `bool shouldInvert = (in[1] != 0.0f)`), LerpVec3's
//     Clamp, and the IsGreater/Compare 0/1 bool outputs. Not a behavioural choice — the runtime
//     has no Bool port type; every bool-typed value op shares this representation.
//   - fork-all-empty-equals-false: TiXL distinguishes "anyConnected==false (empty) → Result = false"
//     from "≥1 connected → Result = AND of connected". In this runtime the resident gather always
//     yields at least the primary default slot (count≥1, never empty — proven by the empty-Sum case
//     in runMultiInputSelfTest, resident_eval_graph_selftest.cpp:339-352). Because All.t3's default
//     is `false` (0), an UNWIRED All gathers in[] = [0] → AND over {false} == false == 0, which is
//     EXACTLY TiXL's empty branch (result & anyConnected = true & false = false). So the two TiXL
//     branches CONVERGE here: AND-reducing the gathered in[] reproduces both branches without a
//     special-case — the empty branch can't arise, and the default-false slot folds it into the AND.
//     (Same gather-yields-primary invariant relied on by value_op_pickfloat.cpp's empty fork and
//     value_op_sumints.cpp's empty fork; the difference is All's neutral element for the empty
//     gather is the default-false slot, not the identity-0 of a sum.)
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runAllSelfTest(bool injectBug);

namespace {

// in[] = [Input…] — the whole multiInput list (no trailing regular ports).
// Result = AND over i of (in[i] != 0), emitted as Float 0/1  (TiXL All.cs verbatim, bool semantics).
// With All.t3's default-false primary slot, the unwired (gather-yields-primary) case is in[]=[0] →
// AND over {false} == false == 0, matching TiXL's empty → (result & anyConnected) == false branch.
float evalAll(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 1) return 0.0f;  // fork-all-empty-equals-false (gather never empty; defensive == false)
  for (int i = 0; i < n; ++i)
    if (in[i] == 0.0f) return 0.0f;  // fork-all-bool-on-float-port: any false → AND is false
  return 1.0f;                       // all non-zero (true) → AND is true
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_all{
    // All (TiXL Lib.numbers.bool.combine.All): logical AND of a MultiInput<bool> list.
    // Single multiInput port (Input) — port order MUST match evalAll's in[] read.
    // bool-on-Float range 0..1; default 0 (false) per All.t3 DefaultValue:false.
    {"All", "All",
     {{"Input", "Input", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Float", false}},
     evalAll},
    "all", runAllSelfTest};

// --- All MATH golden (resident path — multiInput needs the resident gather) ---------------------
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol allAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(v1), Const(v2), … → All.Input (multiInput) }. Result = AND over (vi != 0) as 0/1.
//   [1,1,1]   → 1   (all true)
//   [1,0,1]   → 0   (one false → AND false)
//   [0,0,0]   → 0   (all false)
//   [1]       → 1   (single true; exercises gather-yields-primary path)
//   {}(empty) → 0   (FORK fork-all-empty-equals-false: unwired → default-false slot → AND false)
//   [3.0]     → 1   (FORK fork-all-bool-on-float-port: non-zero Float == true)
// A gather that dropped extras (read only the primary wire) would give 1 for [1,0,1] — NOT 0.
// injectBug flips the typical expectation to a wrong value so the tooth bites RED.
int runAllSelfTest(bool injectBug) {
  Symbol cst = allAtomic("Const", {{"value", "value", "Float", 0.0f}},
                         {{"out", "out", "Float", 0.0f}});
  Symbol all = allAtomic("All", {{"Input", "Input", "Float", 0.0f}},
                         {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires N Const into All.Input and returns the AND-reduced output. An empty
  // vals list wires NOTHING into All (truly unconnected) → exercises the empty/default-false fork.
  auto allOf = [&](std::vector<float> vals) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[all.id] = all;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    const int allChildId = 100;
    SymbolChild ac; ac.id = allChildId; ac.symbolId = "All";
    root.children.clear();
    for (size_t i = 0; i < vals.size(); ++i) {
      SymbolChild c; c.id = (int)(i + 1); c.symbolId = "Const"; c.overrides["value"] = vals[i];
      root.children.push_back(c);
    }
    root.children.push_back(ac);
    root.connections.clear();
    for (size_t i = 0; i < vals.size(); ++i)
      root.connections.push_back({(int)(i + 1), "out", allChildId, "Input"});  // multiInput sources
    root.connections.push_back({allChildId, "out", kSymbolBoundary, "out"});
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 1e-4f;

  // TYPICAL all-true: [1,1,1] → 1. injectBug asserts WRONG (pretend one-false → 0) → flips RED.
  {
    float r = allOf({1.0f, 1.0f, 1.0f});
    float want = injectBug ? 0.0f : 1.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-all] All([1,1,1])=%.4f want=%.4f -> %s\n", r, want, pass ? "PASS" : "FAIL");
  }

  // ONE FALSE: [1,0,1] → 0 (a gather dropping extras would wrongly give 1 — this bites that bug too).
  {
    float r = allOf({1.0f, 0.0f, 1.0f});
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-all] All([1,0,1])=%.4f want=0.0000 (one false) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // ALL FALSE: [0,0,0] → 0.
  {
    float r = allOf({0.0f, 0.0f, 0.0f});
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-all] All([0,0,0])=%.4f want=0.0000 (all false) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // SINGLE TRUE: [1] → 1 (gather-yields-primary; anyConnected=true, result=true).
  {
    float r = allOf({1.0f});
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-all] All([1])=%.4f want=1.0000 (single true) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-all-empty-equals-false: NO wires → unwired default-false slot → AND false → 0.
  //   TiXL empty branch: Result = result(true) & anyConnected(false) = false = 0.
  {
    float r = allOf({});
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-all] All({})=%.4f want=0.0000 (fork:empty->false) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-all-bool-on-float-port: any non-zero Float counts as true. [3.0,1.0] → both true → 1.
  // (TiXL never feeds a non-0/1 into a bool slot; this proves the runtime's `!= 0` interpretation.)
  {
    float r = allOf({3.0f, 1.0f});
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-all] All([3.0,1.0])=%.4f want=1.0000 (fork:nonzero->true) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  // And a non-zero with a zero → still false (the zero dominates the AND).
  {
    float r = allOf({3.0f, 0.0f});
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-all] All([3.0,0.0])=%.4f want=0.0000 (nonzero AND false) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
