// CompareInt value op (value-op self-registration seam leaf — FIRST DUAL-OUTPUT value op).
// TiXL authority: Operators/Lib/numbers/int/logic/CompareInt.cs (+ CompareInt.t3 defaults).
//
//   CompareInt.cs Update():
//     var v    = Value.GetValue(context);        // int
//     var test = TestValue.GetValue(context);    // int
//     var result = false;
//     switch ((Modes)Mode.GetValue(context).Clamp(0, Enum.GetValues(typeof(Modes)).Length - 1)) {
//         case Modes.IsSmaller:  result = v <  test; break;   // 0
//         case Modes.IsEqual:    result = v == test; break;   // 1
//         case Modes.IsLarger:   result = v >  test; break;   // 2
//         case Modes.IsNotEqual: result = v != test; break;   // 3
//     }
//     IsTrue.Value      = result;                                              // Slot<bool>
//     ResultValue.Value = result ? ResultForTrue.GetValue(context)            // Slot<int>
//                                : ResultForFalse.GetValue(context);
//
//   enum Modes { IsSmaller=0, IsEqual=1, IsLarger=2, IsNotEqual=3 };
//   Inputs (CompareInt.cs:64-77, all InputSlot<int>): Value, TestValue, Mode (MappedType=Modes),
//     ResultForTrue, ResultForFalse.  Outputs: IsTrue (Slot<bool>), ResultValue (Slot<int>).
//   CompareInt.t3 DefaultValues: Value=0, TestValue=0, Mode=1 (IsEqual), ResultForTrue=1,
//     ResultForFalse=0.  → Default eval: 0==0 → true → IsTrue=1, ResultValue=ResultForTrue=1.
//
// 5 inputs → 2 outputs. Pure stateless value op (no GPU cook); behaviour is entirely evaluate(),
// registered via the ValueOp seam.
//
// DUAL-OUTPUT EVAL LAYOUT (mirror of Vector3Components, value_eval_ops.cpp:298): the in[] gather
// walks the 5 Float input ports in spec order → in = [Value, TestValue, Mode, ResultForTrue,
// ResultForFalse], n=5. Output ports follow at spec indices 5 (IsTrue) and 6 (ResultValue), so the
// pulled output is selected by `k = outIdx - n`: k=0 → IsTrue, k=1 → ResultValue. The resident eval
// path (resident_eval_graph.cpp:106) resolves outIdx from the pulled output SLOT id, so a wire to
// "IsTrue" vs "ResultValue" hits the right branch. The golden below pulls BOTH ports via the
// RESIDENT path (each Root outputDef connected to a distinct CompareInt output slot).
//
// CONVENTIONS (NOT forks — established runtime contracts):
//   - int-on-Float: this runtime has only Float value ports. Each int input is read as a Float and
//     truncated to int via (int) (C# truncation toward zero, NOT floor) BEFORE compare — identical
//     to SumInts/AddInts (value_op_sumints.cpp) and Floor.cs (value_eval_ops.cpp `(float)(int)in[0]`).
//     For whole-number inputs (the only ones a TiXL int slider produces) this is byte-identical.
//   - bool-as-Float emit: IsTrue is a Slot<bool>; emitted as `result ? 1.0f : 0.0f` (Cut 32 contract,
//     same as IsGreater/Compare/And — value_eval_ops.cpp:576/605, value_op_and.cpp:50).
//   - ResultValue is a Slot<int>: ResultForTrue/ResultForFalse are read int-on-Float (truncated) and
//     the selected one re-emitted as Float (same int-on-Float dissolve as the inputs).
//
// FORK (named) — fork-compareint-mode-clamp:
//   TiXL clamps the Mode int into [0, Enum.Length-1] = [0,3] via `.Clamp(0, 3)` so an out-of-range
//   Mode never throws ArgumentOutOfRangeException (TiXL's default: case would otherwise throw). We
//   reproduce that clamp on the int-truncated Mode: modeIdx = clamp((int)in[2], 0, 3). This makes a
//   degenerate Mode (e.g. 5.0 or -2.0) collapse to the nearest valid mode (3 or 0) exactly as TiXL,
//   instead of throwing or hitting undefined switch fall-through. The default Mode=1 (IsEqual) and
//   every in-range Mode is byte-identical to TiXL.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"       // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runCompareIntSelfTest(bool injectBug);

namespace {

// TiXL .Clamp(0, n-1) on the int-truncated Mode (fork-compareint-mode-clamp, see header).
int clampMode(int m) {
  if (m < 0) return 0;
  if (m > 3) return 3;
  return m;
}

// in[] = [Value, TestValue, Mode, ResultForTrue, ResultForFalse] (spec input-port order), n=5.
// outIdx selects the pulled output port: k = outIdx - n → 0=IsTrue, 1=ResultValue (mirror of
// Vector3Components). All values int-on-Float (truncate toward zero); IsTrue emitted bool-as-Float.
float evalCompareInt(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  const int v    = (int)in[0];               // int-on-Float (trunc toward zero)
  const int test = (int)in[1];
  const int mode = clampMode((int)in[2]);    // fork-compareint-mode-clamp
  const int forTrue  = (int)in[3];
  const int forFalse = (int)in[4];

  bool result;
  switch (mode) {
    case 0:  result = v <  test; break;  // IsSmaller
    case 1:  result = v == test; break;  // IsEqual
    case 2:  result = v >  test; break;  // IsLarger
    default: result = v != test; break;  // 3 = IsNotEqual (clamp guarantees mode∈[0,3])
  }

  const int k = outIdx - n;  // output port index → which output (0=IsTrue, 1=ResultValue)
  if (k == 0) return result ? 1.0f : 0.0f;          // IsTrue (bool-as-Float emit, Cut 32)
  if (k == 1) return (float)(result ? forTrue : forFalse);  // ResultValue (int-on-Float emit)
  return 0.0f;
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_compareint{
    // CompareInt (TiXL Lib.numbers.int.logic.CompareInt): int compare (mode enum) → IsTrue(bool)
    // + ResultValue(int). Port order MUST match evalCompareInt's in[] read: the 5 inputs first
    // (Value, TestValue, Mode, ResultForTrue, ResultForFalse), then the 2 outputs (IsTrue,
    // ResultValue). Output ports MUST follow all inputs so outIdx-n indexing holds.
    // Defaults from CompareInt.t3: Value=0, TestValue=0, Mode=1 (IsEqual), ResultForTrue=1,
    // ResultForFalse=0. Mode is Widget::Enum (MappedType=Modes) with the 4 mode labels.
    {"CompareInt", "CompareInt",
     {{"Value", "Value", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"TestValue", "TestValue", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"Mode", "Mode", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
       {"IsSmaller", "IsEqual", "IsLarger", "IsNotEqual"}},
      {"ResultForTrue", "ResultForTrue", "Float", true, 1.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"ResultForFalse", "ResultForFalse", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"IsTrue", "IsTrue", "Float", false},
      {"ResultValue", "ResultValue", "Float", false}},
     evalCompareInt},
    "compareint", runCompareIntSelfTest};

// --- CompareInt MATH golden (resident path — dual-output pulls each port via a distinct wire) ----
namespace {
// atomic symbol whose id == a registered NodeSpec type (so evalResidentFloat finds evaluate).
Symbol ciAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root with TWO output defs ("isTrue" ← CompareInt.IsTrue, "result" ← CompareInt.ResultValue) and
// five Const inputs feeding the five CompareInt inputs. Evaluating each Root output pulls the
// corresponding CompareInt output slot through evalResidentFloat → exercises the k=0 / k=1 dual-
// output branch (the FIRST value op to do so). A single-output bug (e.g. always returning IsTrue,
// or k computed wrong) would make the two ports collide and the ResultValue assertions go RED.
int runCompareIntSelfTest(bool injectBug) {
  Symbol cst = ciAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  // inputDefs/outputDefs ids must match the registered spec's port ids so connections resolve;
  // the in[] gather + outIdx are driven by findSpec("CompareInt") (the registered NodeSpec).
  Symbol cmp = ciAtomic(
      "CompareInt",
      {{"Value", "Value", "Float", 0.0f},
       {"TestValue", "TestValue", "Float", 0.0f},
       {"Mode", "Mode", "Float", 1.0f},
       {"ResultForTrue", "ResultForTrue", "Float", 1.0f},
       {"ResultForFalse", "ResultForFalse", "Float", 0.0f}},
      {{"IsTrue", "IsTrue", "Float", 0.0f}, {"ResultValue", "ResultValue", "Float", 0.0f}});

  // Build a Root, set the 5 inputs, and pull BOTH outputs. Returns {isTrue, resultValue}.
  struct Both { float isTrue, result; };
  auto cmpWith = [&](float value, float test, float mode, float rTrue, float rFalse) -> Both {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[cmp.id] = cmp;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"isTrue", "isTrue", "Float", 0.0f},
                       {"result", "result", "Float", 0.0f}};
    SymbolChild cv;  cv.id = 1;  cv.symbolId = "Const"; cv.overrides["value"] = value;
    SymbolChild ct;  ct.id = 2;  ct.symbolId = "Const"; ct.overrides["value"] = test;
    SymbolChild cm;  cm.id = 3;  cm.symbolId = "Const"; cm.overrides["value"] = mode;
    SymbolChild crt; crt.id = 4; crt.symbolId = "Const"; crt.overrides["value"] = rTrue;
    SymbolChild crf; crf.id = 5; crf.symbolId = "Const"; crf.overrides["value"] = rFalse;
    SymbolChild ci;  ci.id = 6;  ci.symbolId = "CompareInt";
    root.children = {cv, ct, cm, crt, crf, ci};
    root.connections = {
        {1, "out", 6, "Value"},
        {2, "out", 6, "TestValue"},
        {3, "out", 6, "Mode"},
        {4, "out", 6, "ResultForTrue"},
        {5, "out", 6, "ResultForFalse"},
        {6, "IsTrue", kSymbolBoundary, "isTrue"},        // pull output port k=0
        {6, "ResultValue", kSymbolBoundary, "result"},   // pull output port k=1
    };
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    Both b{-999.0f, -999.0f};
    auto itT = g.outputs.find("isTrue");
    auto itR = g.outputs.find("result");
    if (itT != g.outputs.end())
      b.isTrue = evalResidentFloat(g, itT->second.first, itT->second.second, ctx);
    if (itR != g.outputs.end())
      b.result = evalResidentFloat(g, itR->second.first, itR->second.second, ctx);
    return b;
  };

  bool ok = true;
  const float eps = 1e-4f;
  auto check = [&](const char* label, float got, float want) {
    bool pass = std::fabs(got - want) < eps;
    ok = ok && pass;
    printf("[selftest-compareint] %s = %.4f want=%.4f -> %s\n", label, got, want,
           pass ? "PASS" : "FAIL");
  };

  // TYPICAL IsEqual (Mode=1, t3 default): Value=5, Test=5 → equal → IsTrue=1, ResultValue=ResultForTrue=42.
  //   Pulls BOTH ports: proves k=0 (IsTrue) and k=1 (ResultValue) reach distinct branches.
  // injectBug: flip the IsTrue expectation to 0 → typical-case assertion goes RED.
  {
    Both b = cmpWith(5.0f, 5.0f, 1.0f, 42.0f, 99.0f);
    check("IsEqual(5,5).IsTrue", b.isTrue, injectBug ? 0.0f : 1.0f);
    check("IsEqual(5,5).ResultValue", b.result, 42.0f);  // true → ResultForTrue
  }

  // FALSE branch (Mode=1 IsEqual): Value=5, Test=6 → not equal → IsTrue=0, ResultValue=ResultForFalse=99.
  //   Proves ResultValue selects ResultForFalse on the false branch (k=1 reads the right input).
  {
    Both b = cmpWith(5.0f, 6.0f, 1.0f, 42.0f, 99.0f);
    check("IsEqual(5,6).IsTrue", b.isTrue, 0.0f);
    check("IsEqual(5,6).ResultValue", b.result, 99.0f);  // false → ResultForFalse
  }

  // Mode=0 IsSmaller: 3 < 7 → true → IsTrue=1, ResultValue=1 (ResultForTrue=1).
  {
    Both b = cmpWith(3.0f, 7.0f, 0.0f, 1.0f, 0.0f);
    check("IsSmaller(3,7).IsTrue", b.isTrue, 1.0f);
    check("IsSmaller(3,7).ResultValue", b.result, 1.0f);
  }
  // Mode=0 IsSmaller boundary: 7 < 7 → false (strict <) → IsTrue=0.
  {
    Both b = cmpWith(7.0f, 7.0f, 0.0f, 1.0f, 0.0f);
    check("IsSmaller(7,7).IsTrue (strict)", b.isTrue, 0.0f);
  }

  // Mode=2 IsLarger: 9 > 4 → true → IsTrue=1.
  {
    Both b = cmpWith(9.0f, 4.0f, 2.0f, 1.0f, 0.0f);
    check("IsLarger(9,4).IsTrue", b.isTrue, 1.0f);
  }

  // Mode=3 IsNotEqual: 5 != 6 → true → IsTrue=1; and 5 != 5 → false → IsTrue=0.
  {
    Both b = cmpWith(5.0f, 6.0f, 3.0f, 1.0f, 0.0f);
    check("IsNotEqual(5,6).IsTrue", b.isTrue, 1.0f);
  }
  {
    Both b = cmpWith(5.0f, 5.0f, 3.0f, 1.0f, 0.0f);
    check("IsNotEqual(5,5).IsTrue", b.isTrue, 0.0f);
  }

  // int-on-Float trunc toward zero: Value=5.9 → (int)5.9 = 5, Test=5 → IsEqual → 5==5 → IsTrue=1.
  //   (If it rounded, 5.9→6 != 5 → would be 0. Asserting 1 pins truncation, matching C# (int).)
  {
    Both b = cmpWith(5.9f, 5.0f, 1.0f, 1.0f, 0.0f);
    check("IsEqual(5.9->5, 5).IsTrue (trunc)", b.isTrue, 1.0f);
  }
  // Negative trunc toward zero: Value=-2.9 → (int)-2.9 = -2, Test=-2 → IsEqual → true.
  {
    Both b = cmpWith(-2.9f, -2.0f, 1.0f, 1.0f, 0.0f);
    check("IsEqual(-2.9->-2, -2).IsTrue (trunc neg)", b.isTrue, 1.0f);
  }

  // FORK fork-compareint-mode-clamp: Mode=5 → clamp to 3 (IsNotEqual). 4 != 5 → true → IsTrue=1.
  //   (Mode=5 unclamped would throw / undefined in TiXL's switch; clamp collapses it to IsNotEqual.)
  {
    Both b = cmpWith(4.0f, 5.0f, 5.0f, 1.0f, 0.0f);
    check("clamp Mode=5->3 IsNotEqual(4,5).IsTrue", b.isTrue, 1.0f);
  }
  // FORK clamp negative: Mode=-2 → clamp to 0 (IsSmaller). 4 < 5 → true → IsTrue=1.
  {
    Both b = cmpWith(4.0f, 5.0f, -2.0f, 1.0f, 0.0f);
    check("clamp Mode=-2->0 IsSmaller(4,5).IsTrue", b.isTrue, 1.0f);
  }

  return ok ? 0 : 1;
}

}  // namespace sw
