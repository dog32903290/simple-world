// RemapValues value op (value-op self-registration seam leaf — Phase C numbers/float mining).
// TiXL authority: Operators/Lib/numbers/float/process/RemapValues.cs (verbatim below).
//
//   RemapValues.cs Update():
//     var inputValue = InputValue.GetValue(context);
//     var minDistances = float.PositiveInfinity;
//     var bestValue = 0f;
//     var bestMatchingIndex = -1;
//     var list = InputAndOutputPairs.GetCollectedTypedInputs();   // List<Vector2>
//     for (var index = 0; index < list.Count; index++) {
//         var lookUpValue = list[index].GetValue(context);        // Vector2 (X=key, Y=mapped value)
//         var distance = MathF.Abs(lookUpValue.X - inputValue);
//         if (distance < minDistances) {                          // STRICT < → first wins ties
//             minDistances = distance;
//             bestValue = lookUpValue.Y;
//             bestMatchingIndex = index;
//         }
//     }
//     Result.Value = bestMatchingIndex == -1 ? 0 : bestValue;     // empty list → 0
//
//   Ports: InputAndOutputPairs = MultiInputSlot<Vector2> (the variable-length list of (key,value)
//   pairs); InputValue = InputSlot<float> (default 0). Output: Result (Slot<float>).
//   RemapValues.t3 DefaultValues: InputValue = 0 (InputAndOutputPairs has no scalar default — it is a
//   list whose empty state yields Result=0).
//
//   SEMANTICS: nearest-neighbour lookup. Among the connected (key,value) pairs, pick the pair whose
//   key (.X) is closest to InputValue (absolute distance); return that pair's value (.Y). This is a
//   step/quantize remap, NOT a linear interpolation between pairs.
//
// EVAL-SIDE LAYOUT (mixed-MultiInput convention, mirror of PickFloat: multiInput prefix, regular
// port trailing; AND the Vec2-multiInput gather proven by MaxInt2 — a multiInput head with
// vecArity=2 collects each Vector2 source as 2 consecutive floats in CONNECTION order):
//   in[] = [pair0.x, pair0.y, pair1.x, pair1.y, ..., pairK-1.x, pairK-1.y, InputValue]
//   n = 2K + 1 (2 floats per Vector2 pair + the trailing scalar InputValue).
//   InputValue = in[n-1]; pair i = (in[2i], in[2i+1]) for i in [0, K).
//   The flat evalFloat path does NOT expand multiInput → the golden exercises the RESIDENT path
//   (buildEvalGraph + evalResidentFloat), exactly like PickFloat / MaxInt2.
//
// FORKS (named):
//   - fork-remapvalues-vec2-as-2-floats-per-source (precedent: fork-maxint2-int2-as-2-floats-per-source):
//     TiXL has a native MultiInputSlot<Vector2>. This runtime exposes the pairs as a single multiInput
//     head port "InputAndOutputPairs" with vecArity=2; each connected pair sends 2 consecutive wires
//     (.x then .y). The resident gather collects them interleaved (src0.x, src0.y, src1.x, ...). The
//     nearest-match math is byte-identical; only the port data model differs.
//   - fork-remapvalues-empty-zero: TiXL returns Result=0 when no pairs are connected (bestMatchingIndex
//     == -1). On this runtime the resident gather always yields at least the multiInput head's default
//     slot, so the empty case is bounded by n<3 here (need >=2 pair floats + 1 InputValue) → return 0,
//     the same neutral TiXL's empty path produces.
//   - fork-remapvalues-strict-less-ties: TiXL uses STRICT `distance < minDistances`, so on exact
//     distance ties the FIRST (lowest-index) pair wins. Ported verbatim (the loop keeps the first).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"       // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runRemapValuesSelfTest(bool injectBug);

namespace {

// in[] = [pair0.x, pair0.y, pair1.x, pair1.y, ..., InputValue].  n = 2K + 1.
// Result = .Y of the pair whose .X is nearest (abs) to InputValue; first wins ties; empty → 0.
float evalRemapValues(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;             // fork-remapvalues-empty-zero (need >=1 pair + InputValue)
  const float inputValue = in[n - 1];  // trailing regular port
  const int pairFloats = n - 1;        // exclude InputValue
  const int pairs = pairFloats / 2;    // each pair = 2 floats (.x, .y)
  float minDist = INFINITY;            // float.PositiveInfinity
  float bestValue = 0.0f;
  for (int i = 0; i < pairs; ++i) {
    const float key = in[2 * i];
    const float val = in[2 * i + 1];
    const float dist = std::fabs(key - inputValue);
    if (dist < minDist) {  // fork-remapvalues-strict-less-ties: STRICT < → first wins
      minDist = dist;
      bestValue = val;
    }
  }
  return bestValue;
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf (no shared edit point).
static const ValueOp _reg_remapvalues{
    // RemapValues (TiXL Lib.numbers.float.process.RemapValues): nearest-key lookup over a
    // MultiInput<Vector2> of (key,value) pairs → the value of the pair whose key is closest to
    // InputValue. InputAndOutputPairs = the multiInput head (vecArity=2, vary-length); InputValue
    // trails it (PickFloat mixed convention).
    // Port order MUST match evalRemapValues's in[] read: InputAndOutputPairs (multiInput) first,
    // InputValue last.  Defaults from RemapValues.t3: InputValue = 0.
    {"RemapValues", "RemapValues",
     {{"InputAndOutputPairs", "InputAndOutputPairs", "Float", true, 0.0f, -100000.0f, 100000.0f,
       Widget::Vec, {}, false, /*vecArity=*/2, /*multiInput=*/true},
      {"InputValue", "InputValue", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"out", "out", "Float", false}},
     evalRemapValues},
    "remapvalues", runRemapValuesSelfTest};

// --- RemapValues MATH golden (resident path — multiInput Vector2 needs the resident gather) -------
namespace {
Symbol rvAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

// Root { Const pairs (x,y per pair) → RemapValues.InputAndOutputPairs (multiInput Vector2);
//        Const(inputValue) → RemapValues.InputValue }.
//   Pairs: P0=(0,100), P1=(10,200), P2=(20,300).  Nearest-key lookup:
//     inputValue=9  → nearest key is 10 (|10-9|=1 < |0-9|=9, |20-9|=11) → 200.
//     inputValue=3  → nearest key is 0  (|0-3|=3 < |10-3|=7)            → 100.
//     inputValue=15 → tie |10-15|=5 == |20-15|=5 → STRICT < keeps FIRST (P1 key 10) → 200.
//     inputValue=20 → exact key 20                                      → 300.
// injectBug flips the typical expectation to a wrong pick (the value of the WRONG/first pair) → RED.
int runRemapValuesSelfTest(bool injectBug) {
  Symbol cst = rvAtomic("Const", {{"value", "value", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});
  Symbol rmv = rvAtomic("RemapValues",
                        {{"InputAndOutputPairs", "InputAndOutputPairs", "Float", 0.0f},
                         {"InputValue", "InputValue", "Float", 0.0f}},
                        {{"out", "out", "Float", 0.0f}});

  // Build a Root that wires K (x,y) Const pairs into InputAndOutputPairs + one Const into InputValue.
  auto remapWith = [&](std::vector<std::pair<float, float>> pairs, float inputValue) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[rmv.id] = rmv;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"out", "out", "Float", 0.0f}};
    const int rmvChildId = 200;
    SymbolChild rc; rc.id = rmvChildId; rc.symbolId = "RemapValues";
    root.children.clear();
    // Each pair: 2 Const nodes (x at id 2i+1, y at id 2i+2).
    for (size_t i = 0; i < pairs.size(); ++i) {
      SymbolChild cx; cx.id = (int)(2 * i + 1); cx.symbolId = "Const"; cx.overrides["value"] = pairs[i].first;
      SymbolChild cy; cy.id = (int)(2 * i + 2); cy.symbolId = "Const"; cy.overrides["value"] = pairs[i].second;
      root.children.push_back(cx);
      root.children.push_back(cy);
    }
    // InputValue Const (id 100).
    SymbolChild civ; civ.id = 100; civ.symbolId = "Const"; civ.overrides["value"] = inputValue;
    root.children.push_back(civ);
    root.children.push_back(rc);
    root.connections.clear();
    // Wire each (x, y) pair into the InputAndOutputPairs multiInput head (2 wires per pair, .x then
    // .y — vecArity=2 convention; resident gather interleaves to [p0.x, p0.y, p1.x, p1.y, ...]).
    for (size_t i = 0; i < pairs.size(); ++i) {
      root.connections.push_back({(int)(2 * i + 1), "out", rmvChildId, "InputAndOutputPairs"});  // .x
      root.connections.push_back({(int)(2 * i + 2), "out", rmvChildId, "InputAndOutputPairs"});  // .y
    }
    root.connections.push_back({100, "out", rmvChildId, "InputValue"});  // trailing regular port
    root.connections.push_back({rmvChildId, "out", kSymbolBoundary, "out"});
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find("out");
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  bool ok = true;
  const float eps = 1e-4f;
  const std::vector<std::pair<float, float>> P = {{0.0f, 100.0f}, {10.0f, 200.0f}, {20.0f, 300.0f}};

  // TYPICAL: inputValue=9 → nearest key 10 → 200. injectBug asserts 100 (the first pair's value, the
  // "ignored distance / picked index 0" failure mode) → flips RED.
  {
    float r = remapWith(P, 9.0f);
    float want = injectBug ? 100.0f : 200.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-remapvalues] Remap([0:100,10:200,20:300], in=9)=%.4f want=%.4f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }
  // NEAREST below the first key boundary: inputValue=3 → nearest key 0 → 100.
  {
    float r = remapWith(P, 3.0f);
    bool pass = std::fabs(r - 100.0f) < eps;
    ok = ok && pass;
    printf("[selftest-remapvalues] Remap(..., in=3)=%.4f want=100.0000 (nearest key 0) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  // TIE (strict-less keeps FIRST): inputValue=15 → |10-15|==|20-15|=5 → P1 (key 10) → 200.
  {
    float r = remapWith(P, 15.0f);
    bool pass = std::fabs(r - 200.0f) < eps;
    ok = ok && pass;
    printf("[selftest-remapvalues] Remap(..., in=15)=%.4f want=200.0000 (tie→first) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  // EXACT key: inputValue=20 → key 20 → 300.
  {
    float r = remapWith(P, 20.0f);
    bool pass = std::fabs(r - 300.0f) < eps;
    ok = ok && pass;
    printf("[selftest-remapvalues] Remap(..., in=20)=%.4f want=300.0000 (exact) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }
  // SINGLE pair: any input → that pair's value. in=999 → 100 (only pair P0).
  {
    float r = remapWith({{5.0f, 100.0f}}, 999.0f);
    bool pass = std::fabs(r - 100.0f) < eps;
    ok = ok && pass;
    printf("[selftest-remapvalues] Remap([5:100], in=999)=%.4f want=100.0000 (single pair) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
