// PickBool value op (value-op self-registration seam leaf — MultiInput<bool> floor-mod select).
// TiXL authority: Operators/Lib/numbers/bool/logic/PickBool.cs (+ Core/Utils/MathUtils.Mod,
// + PickBool.t3 DefaultValue: BoolValues=false, Index=0).
//
//   PickBool.cs Update():
//     var connections = BoolValues.GetCollectedTypedInputs();
//     BoolValues.DirtyFlag.Clear();
//     if (connections == null || connections.Count == 0) return;          // empty → no write
//     var index = Index.GetValue(context).Mod(connections.Count);
//     Selected.Value = connections[index].GetValue(context);              // pick the one bool
//
//   MathUtils.Mod(int val, int repeat):
//     if (repeat == 0) return 0;
//     var x = val % repeat; if (x < 0) x = repeat + x; return x;          // floor-mod → [0,repeat)
//
//   PickBool.t3 Inputs: BoolValues (MultiInputSlot<bool>) DefaultValue=false; Index
//   (InputSlot<int>) DefaultValue=0. Output: Selected (Slot<bool>).
//
// This is PickFloat (value_op_pickfloat.cpp) EXACTLY — same MultiInput head + trailing Index,
// same floor-mod select of one element — but the list/output type is bool instead of float. The
// gather/select logic is byte-identical; only the port type + the bool-as-Float emit differ.
//
// EVAL-SIDE LAYOUT (mixed-MultiInput convention, same as PickFloat): the resident gather expands
// the ONE multiInput port (BoolValues) into the in[] PREFIX, then appends the K=1 trailing regular
// port (Index). So in[] = [BoolValues… , Index], count = n-1, Index = in[n-1]. The flat evalFloat
// path does NOT expand multiInput (one slot per port), so the golden below exercises the RESIDENT
// path (buildEvalGraph + evalResidentFloat), exactly like value_op_pickfloat.cpp.
//
// FORKS (named):
//   - fork-pickbool-bool-on-float-port: TiXL BoolValues/Selected are `bool`. This runtime has only
//     Float value ports, so each list element is read as a bool via (in[i] != 0.0f) and the picked
//     element is emitted as Float 1.0f (true) / 0.0f (false). This is the bool-as-Float convention
//     already shipped for the value spine (value_op_any.cpp / value_op_or.cpp / value_op_and.cpp,
//     value_eval_ops.cpp Cut 32). Any nonzero Float counts as true — identical to C# `bool` once a
//     0|1 bool slider is the only producer TiXL ever wires here.
//   - fork-pickbool-index-int: TiXL Index is int; this runtime has only Float ports, so Index is a
//     Float truncated to int via (int) before Mod — matches C# int semantics for whole-number
//     inputs (the only ones that make sense for an index). Mirror of fork-pickfloat-index-int.
//   - fork-pickbool-empty-passthrough: TiXL returns WITHOUT writing Selected when the list is empty
//     (count==0), leaving the prior Slot value. This runtime is stateless-per-frame (no prior value
//     to keep), and the resident gather always yields at least the primary default slot (count≥1),
//     so the empty case cannot arise here; if n<1 we return 0 (TiXL-empty ≈ false, the same neutral
//     the rest of the value spine uses). Mirror of fork-pickfloat-empty-passthrough.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runPickBoolSelfTest(bool injectBug);

namespace {

// TiXL MathUtils.Mod (floor-mod): repeat==0 → 0; else x=val%repeat, x<0 → x+=repeat.
int tixlMod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// in[] = [BoolValues… (multiInput prefix), Index (trailing regular port)].
// Selected = (BoolValues[ Mod((int)Index, count) ] != 0) ? 1 : 0, count = n-1.
//   Empty (n<1) → false → 0.0f (fork-pickbool-empty-passthrough; matches TiXL no-write neutral).
float evalPickBool(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return (n == 1 && in[0] != 0.0f) ? 1.0f : 0.0f;  // fork-pickbool-empty-passthrough
  const int count = n - 1;                                     // exclude the trailing Index
  const int index = tixlMod((int)in[n - 1], count);           // fork-pickbool-index-int
  return in[index] != 0.0f ? 1.0f : 0.0f;  // fork-pickbool-bool-on-float-port: nonzero == true
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf (no shared edit point), feeds
// valueOpSpecSink() + valueOpSelfTests() during pre-main dynamic init.
static const ValueOp _reg_pickbool{
    // PickBool (TiXL Lib.numbers.bool.logic.PickBool): pick BoolValues[Mod(Index, count)] from a
    // MultiInput<bool> list. BoolValues = the multiInput head (vary-length, default false); Index
    // trails it (default 0). Output emitted as Float 1/0 (bool-as-Float convention). BoolValues
    // min/max 0..1 = the bool widget's 0|1 domain. Port order MUST match evalPickBool's in[] read:
    // BoolValues (multiInput) first, Index last.
    {"PickBool", "PickBool",
     {{"BoolValues", "BoolValues", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, false, 1,
       /*multiInput=*/true},
      {"Index", "Index", "Float", true, 0.0f, 0.0f, 1000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalPickBool},
    "pickbool", runPickBoolSelfTest};

// --- PickBool MATH golden (resident path — multiInput needs the resident gather) ----------------
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol pbAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(a), Const(b), Const(c) → PickBool.BoolValues (multiInput); Const(idx) →
// PickBool.Index }. count=3. Pick index i → BoolValues[Mod(i,3)], emitted 1/0.
//   idx=1 over [1,0,1] → BoolValues[1]=0 ; idx=2 → BoolValues[2]=1 ; idx=4 → Mod(4,3)=1 → 0 ;
//   idx=-1 → Mod(-1,3)=2 → 1 (floor-mod wraps).
// A gather that mis-placed Index (folded it into BoolValues, or read the primary wire only) would
// NOT reproduce these. injectBug flips the typical expectation so the tooth bites.
int runPickBoolSelfTest(bool injectBug) {
  Symbol cst = pbAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  Symbol pick = pbAtomic("PickBool",
                         {{"BoolValues", "BoolValues", "Float", 0.0f},
                          {"Index", "Index", "Float", 0.0f}},
                         {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires three Const into PickBool.BoolValues + one Const into Index, with a
  // given index value, and returns the selected (bool-as-Float) output.
  auto pickWith = [&](float a, float b, float c, float idx) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[pick.id] = pick;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = a;
    SymbolChild c2; c2.id = 2; c2.symbolId = "Const"; c2.overrides["value"] = b;
    SymbolChild c3; c3.id = 3; c3.symbolId = "Const"; c3.overrides["value"] = c;
    SymbolChild ci; ci.id = 5; ci.symbolId = "Const"; ci.overrides["value"] = idx;
    SymbolChild pk; pk.id = 4; pk.symbolId = "PickBool";
    root.children = {c1, c2, c3, ci, pk};
    root.connections = {
        {1, "out", 4, "BoolValues"},            // primary multiInput source
        {2, "out", 4, "BoolValues"},            // extra
        {3, "out", 4, "BoolValues"},            // extra
        {5, "out", 4, "Index"},                 // trailing regular port (must NOT join BoolValues)
        {4, "out", kSymbolBoundary, "out"},
    };
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 1e-4f;

  // TYPICAL: list [1,0,1], idx=1 → BoolValues[Mod(1,3)=1] = 0. injectBug asserts the WRONG pick
  // (BoolValues[0]=1, the broken-gather / index-ignored failure mode) → flips RED.
  {
    float r = pickWith(1.0f, 0.0f, 1.0f, 1.0f);
    float want = injectBug ? 1.0f : 0.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-pickbool] Pick([1,0,1], idx=1)=%.4f want=%.4f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // SELECT-TRUE: list [1,0,1], idx=2 → BoolValues[2] = 1. Proves the LAST wire (not just primary)
  // is gathered; a primary-only gather would mis-fold to BoolValues[0].
  {
    float r = pickWith(1.0f, 0.0f, 1.0f, 2.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickbool] Pick([1,0,1], idx=2)=%.4f want=1.0000 (last wire) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // WRAP (Mod positive): list [1,0,1], idx=4 → Mod(4,3)=1 → BoolValues[1] = 0.
  {
    float r = pickWith(1.0f, 0.0f, 1.0f, 4.0f);
    bool pass = std::fabs(r - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickbool] Pick([1,0,1], idx=4)=%.4f want=0.0000 (wrap) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // WRAP (negative → floor-mod): list [1,0,1], idx=-1 → Mod(-1,3)=2 → BoolValues[2] = 1.
  {
    float r = pickWith(1.0f, 0.0f, 1.0f, -1.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickbool] Pick([1,0,1], idx=-1)=%.4f want=1.0000 (floor-mod) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: idx=0 → BoolValues[0] = 1.
  {
    float r = pickWith(1.0f, 0.0f, 1.0f, 0.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickbool] Pick([1,0,1], idx=0)=%.4f want=1.0000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // FORK fork-pickbool-bool-on-float-port: a NONZERO non-1 Float in the picked slot reads as true.
  // list [0,0.5,0], idx=1 → BoolValues[1]=0.5 != 0 → emitted 1.
  {
    float r = pickWith(0.0f, 0.5f, 0.0f, 1.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickbool] Pick([0,0.5,0], idx=1)=%.4f want=1.0000 (nonzero-Float=true) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
