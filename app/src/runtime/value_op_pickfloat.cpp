// PickFloat value op (value-op self-registration seam leaf — SECOND consumer; proves the seam
// is parallel-safe: an independent leaf .cpp with no shared edit point vs value_op_sin.cpp).
// TiXL authority: Operators/Lib/numbers/float/logic/PickFloat.cs (+ Core/Utils/MathUtils.Mod).
//
//   PickFloat.cs Update():
//     var connections = FloatValues.GetCollectedTypedInputs();
//     if (connections == null || connections.Count == 0) { ...Clear(); return; }   // empty → no write
//     var index = Index.GetValue(context).Mod(connections.Count);
//     Selected.Value = connections[index].GetValue(context);
//
//   MathUtils.Mod(int val, int repeat):
//     if (repeat == 0) return 0;
//     var x = val % repeat; if (x < 0) x = repeat + x; return x;   // floor-mod → always in [0,repeat)
//
//   Ports: FloatValues = MultiInputSlot<float> (the variable-length list); Index = InputSlot<int>
//   (default 0). Output: Selected (Slot<float>).
//
// EVAL-SIDE LAYOUT (mixed-MultiInput convention, same as BlendValues batch35): the resident gather
// expands the ONE multiInput port (Values) into the in[] PREFIX, then appends the K=1 trailing
// regular port (Index). So in[] = [Values… , Index], count = n-1, Index = in[n-1]. The flat
// evalFloat path does NOT expand multiInput (one slot per port), so the golden below exercises the
// RESIDENT path (buildEvalGraph + evalResidentFloat), exactly like runMultiInputSelfTest.
//
// FORKS (named):
//   - fork-pickfloat-index-int: TiXL Index is int; this runtime has only Float ports, so Index is
//     a Float truncated to int via (int) before Mod — matches C# int semantics for whole-number
//     inputs (the only ones that make sense for an index).
//   - fork-pickfloat-empty-passthrough: TiXL returns WITHOUT writing Selected when the list is
//     empty (count==0), leaving the prior Slot value. This runtime is stateless-per-frame (no
//     prior value to keep), and the resident gather always yields at least the primary default
//     slot (count≥1), so the empty case cannot arise here; if n<1 we return 0 (TiXL-empty ≈ 0,
//     the same neutral the rest of the value spine uses).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runPickFloatSelfTest(bool injectBug);

namespace {

// TiXL MathUtils.Mod (floor-mod): repeat==0 → 0; else x=val%repeat, x<0 → x+=repeat.
int tixlMod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// in[] = [Values… (multiInput prefix), Index (trailing regular port)].
// Selected = Values[ Mod((int)Index, count) ], count = n-1.
float evalPickFloat(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return n == 1 ? in[0] : 0.0f;  // fork-pickfloat-empty-passthrough (see header)
  const int count = n - 1;                   // exclude the trailing Index
  const int index = tixlMod((int)in[n - 1], count);  // fork-pickfloat-index-int
  return in[index];
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent of value_op_sin.cpp (no shared edit
// point: this is the whole point of the parallel-weave seam).
static const ValueOp _reg_pickfloat{
    // PickFloat (TiXL Lib.numbers.float.logic.PickFloat): pick Values[Mod(Index, count)] from a
    // MultiInput<float> list. Values = the multiInput head (vary-length); Index trails it.
    // Port order MUST match evalPickFloat's in[] read: Values (multiInput) first, Index last.
    {"PickFloat", "PickFloat",
     {{"Values", "Values", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"Index", "Index", "Float", true, 0.0f, 0.0f, 1000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalPickFloat},
    "pickfloat", runPickFloatSelfTest};

// --- PickFloat MATH golden (resident path — multiInput needs the resident gather) ---------------
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol pfAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(10), Const(20), Const(30) → PickFloat.Values (multiInput); Const(idx) →
// PickFloat.Index }. count=3. Pick index i → Values[Mod(i,3)].
//   idx=1 → 20 ; idx=4 → Mod(4,3)=1 → 20 ; idx=-1 → Mod(-1,3)=2 → 30 (floor-mod wraps).
// A gather that mis-placed Index (folded it into Values, or read the primary wire only) would NOT
// reproduce these. injectBug flips the typical expectation to a wrong index so the tooth bites.
int runPickFloatSelfTest(bool injectBug) {
  Symbol cst = pfAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  Symbol pick = pfAtomic("PickFloat",
                         {{"Values", "Values", "Float", 0.0f}, {"Index", "Index", "Float", 0.0f}},
                         {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires three Const into PickFloat.Values + one Const into Index, with a given
  // index value, and returns the selected output.
  auto pickWith = [&](float idx) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[pick.id] = pick;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = 10.0f;
    SymbolChild c2; c2.id = 2; c2.symbolId = "Const"; c2.overrides["value"] = 20.0f;
    SymbolChild c3; c3.id = 3; c3.symbolId = "Const"; c3.overrides["value"] = 30.0f;
    SymbolChild ci; ci.id = 5; ci.symbolId = "Const"; ci.overrides["value"] = idx;
    SymbolChild pk; pk.id = 4; pk.symbolId = "PickFloat";
    root.children = {c1, c2, c3, ci, pk};
    root.connections = {
        {1, "out", 4, "Values"},                // primary multiInput source
        {2, "out", 4, "Values"},                // extra
        {3, "out", 4, "Values"},                // extra
        {5, "out", 4, "Index"},                 // trailing regular port (must NOT join Values)
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

  // TYPICAL: idx=1 → Values[Mod(1,3)=1] = 20. injectBug asserts the WRONG pick (Values[0]=10,
  // the broken-gather / index-ignored failure mode) → flips RED.
  {
    float r = pickWith(1.0f);
    float want = injectBug ? 10.0f : 20.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-pickfloat] Pick([10,20,30], idx=1)=%.4f want=%.4f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // WRAP (Mod positive): idx=4 → Mod(4,3)=1 → 20.
  {
    float r = pickWith(4.0f);
    bool pass = std::fabs(r - 20.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickfloat] Pick([10,20,30], idx=4)=%.4f want=20.0000 (wrap) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // WRAP (negative → floor-mod): idx=-1 → Mod(-1,3)=2 → 30.
  {
    float r = pickWith(-1.0f);
    bool pass = std::fabs(r - 30.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickfloat] Pick([10,20,30], idx=-1)=%.4f want=30.0000 (floor-mod) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: idx=0 → Values[0] = 10.
  {
    float r = pickWith(0.0f);
    bool pass = std::fabs(r - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickfloat] Pick([10,20,30], idx=0)=%.4f want=10.0000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
