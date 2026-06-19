// PickInt value op (value-op self-registration seam leaf — Phase C numbers/int mining).
// TiXL authority: Operators/Lib/numbers/int/logic/PickInt.cs (+ PickInt.t3 defaults).
//
//   PickInt.cs Update():
//     var connections = InputValues.GetCollectedTypedInputs();
//     var index = Index.GetValue(context).Mod(connections.Count);
//     if (connections.Count == 0) { InputValues.DirtyFlag.Clear(); return; }
//     Selected.Value = connections[index].GetValue(context);
//     foreach (var c in connections) c.GetValue(context);   // dirty-flag flush
//     InputValues.DirtyFlag.Clear();
//
//   MathUtils.Mod(int val, int repeat): floor-mod → always in [0, repeat).
//     (Same helper used by PickFloat. See value_op_pickfloat.cpp for full impl.)
//
//   PickInt.t3 DefaultValues: InputValues=0 (primary), Index=0.
//
// EVAL-SIDE LAYOUT (identical to PickFloat — multiInput + trailing regular port):
//   in[] = [InputValues… (multiInput prefix), Index (trailing regular port)].
//   count = n-1, Index = in[n-1].
//   The FLAT evalFloat path does NOT expand multiInput, so the golden exercises the RESIDENT
//   path (buildEvalGraph + evalResidentFloat), exactly like runPickFloatSelfTest.
//
// FORKS (named):
//   - fork-pickint-index-int: TiXL Index is int; this runtime has only Float ports, so Index
//     is truncated via (int) before Mod — same as PickFloat's fork-pickfloat-index-int.
//   - fork-pickint-int-on-float-port: TiXL InputValues/Selected are `int`. This runtime stores
//     them as Float. For whole-number inputs (the only values an int slider produces) this is
//     byte-identical to TiXL. Matches the fork already named in MultiplyInt / AddInts.
//   - fork-pickint-empty-passthrough: TiXL returns without writing Selected when count==0
//     (stateful "keep prior value"). This runtime is stateless-per-frame; if n<2 we return 0
//     (TiXL-empty ≈ 0, the same neutral the rest of the value spine uses). Matches
//     fork-pickfloat-empty-passthrough.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"        // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"   // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"     // ValueOp self-registration

namespace sw {

int runPickIntSelfTest(bool injectBug);

namespace {

// TiXL MathUtils.Mod (floor-mod): repeat==0 → 0; else x=val%repeat, x<0 → x+=repeat.
// (Identical helper to value_op_pickfloat.cpp — inlined per-file, no shared dep.)
int tixlModPickInt(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// in[] = [InputValues… (multiInput prefix), Index (trailing regular port)].
// Selected = (int)InputValues[ Mod((int)Index, count) ], emitted as Float.
// count = n-1. (TiXL PickInt.cs verbatim; fork-pickint-index-int + fork-pickint-int-on-float-port.)
float evalPickIntOp(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return n == 1 ? (float)(int)in[0] : 0.0f;  // fork-pickint-empty-passthrough
  const int count = n - 1;                               // exclude the trailing Index
  const int index = tixlModPickInt((int)in[n - 1], count);  // fork-pickint-index-int
  return (float)(int)in[index];                          // fork-pickint-int-on-float-port
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited (kTable / mathSpecs untouched).
static const ValueOp _reg_pickint{
    // PickInt (TiXL Lib.numbers.int.logic.PickInt): pick InputValues[Mod(Index, count)] from a
    // MultiInput<int> list. InputValues = the multiInput head (vary-length); Index trails it.
    // Port order MUST match evalPickIntOp's in[] read: InputValues (multiInput) first, Index last.
    // Defaults from PickInt.t3: InputValues=0 (primary), Index=0.
    {"PickInt", "PickInt",
     {{"InputValues", "InputValues", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"Index",       "Index",       "Float", true, 0.0f,     0.0f,  1000.0f, Widget::Slider},
      {"out",         "out",         "Float", false}},
     evalPickIntOp},
    "pickint", runPickIntSelfTest};

// --- PickInt MATH golden (resident path — multiInput needs the resident gather) ----------------
namespace {
// Atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol piAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const(10), Const(20), Const(30) → PickInt.InputValues (multiInput); Const(idx) →
// PickInt.Index }. count=3. Pick index i → InputValues[Mod(i,3)].
//   idx=2 → 30 ; idx=3 → Mod(3,3)=0 → 10 ; idx=-1 → Mod(-1,3)=2 → 30 (floor-mod wraps).
// injectBug flips the typical expectation to a wrong index so the tooth bites.
int runPickIntSelfTest(bool injectBug) {
  Symbol cst = piAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  Symbol pick = piAtomic("PickInt",
                         {{"InputValues", "InputValues", "Float", 0.0f},
                          {"Index",       "Index",       "Float", 0.0f}},
                         {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires three Const into PickInt.InputValues + one Const into Index, with a
  // given index value, and returns the selected output.
  auto pickWith = [&](float idx) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id]  = cst;
    lib.symbols[pick.id] = pick;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = 10.0f;
    SymbolChild c2; c2.id = 2; c2.symbolId = "Const"; c2.overrides["value"] = 20.0f;
    SymbolChild c3; c3.id = 3; c3.symbolId = "Const"; c3.overrides["value"] = 30.0f;
    SymbolChild ci; ci.id = 5; ci.symbolId = "Const"; ci.overrides["value"] = idx;
    SymbolChild pk; pk.id = 4; pk.symbolId = "PickInt";
    root.children = {c1, c2, c3, ci, pk};
    root.connections = {
        {1, "out", 4, "InputValues"},           // primary multiInput source
        {2, "out", 4, "InputValues"},           // extra
        {3, "out", 4, "InputValues"},           // extra
        {5, "out", 4, "Index"},                 // trailing regular port (must NOT join InputValues)
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

  // TYPICAL: idx=2 → InputValues[Mod(2,3)=2] = 30. injectBug asserts the WRONG pick
  // (InputValues[0]=10, the broken-gather / index-ignored failure mode) → flips RED.
  {
    float r = pickWith(2.0f);
    float want = injectBug ? 10.0f : 30.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-pickint] PickInt([10,20,30], idx=2)=%.4f want=%.4f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // WRAP (positive overflow): idx=3 → Mod(3,3)=0 → 10.
  {
    float r = pickWith(3.0f);
    bool pass = std::fabs(r - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickint] PickInt([10,20,30], idx=3)=%.4f want=10.0000 (wrap) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // WRAP (negative → floor-mod): idx=-1 → Mod(-1,3)=2 → 30.
  {
    float r = pickWith(-1.0f);
    bool pass = std::fabs(r - 30.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickint] PickInt([10,20,30], idx=-1)=%.4f want=30.0000 (floor-mod) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: idx=0 → InputValues[0] = 10.
  {
    float r = pickWith(0.0f);
    bool pass = std::fabs(r - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickint] PickInt([10,20,30], idx=0)=%.4f want=10.0000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
