// PickVector2 value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/PickVector2.cs (+ PickVector2.t3 defaults).
//
//   PickVector2.cs Update():
//     var connections = Input.GetCollectedTypedInputs();
//     if (connections == null || connections.Count == 0) return;
//     var index = Index.GetValue(context).Mod(connections.Count);
//     Selected.Value = connections[index].GetValue(context);
//
//   TiXL MathUtils.Mod (floor-mod): same as PickFloat — val%repeat, negatives wrap positively.
//   PickVector2.t3: Index default 0; Input default {X:0.0, Y:0.0}.
//
// Input = MultiInputSlot<Vector2> (variable N wires of Vec2); Index = InputSlot<int> (default 0).
// Output: Selected (Slot<Vector2>) — exposes as 2 Float outputs (Selected.x, Selected.y).
//
// EVAL-SIDE LAYOUT (mixed-MultiInput convention — exact mirror of PickFloat, value_op_pickfloat.cpp):
//   The resident gather expands the ONE multiInput port (Input) into the in[] PREFIX, each Vec2
//   source contributing 2 consecutive floats (x then y). The trailing regular port (Index) = in[n-1].
//   So in[] = [src0.x, src0.y, src1.x, src1.y, ..., srcK-1.x, srcK-1.y, Index], n = 2K+1.
//   Selected = connections[Mod((int)Index, K)].Value — the 2-float block at in[2*idx .. 2*idx+1].
//   Output port Selected.x is at spec index 2 (after Input + Index), Selected.y at 3.
//   outIdx-2 gives component 0=x or 1=y.
//
// GOLDEN: uses resident eval path (buildEvalGraph + evalResidentFloat), same as PickFloat.
//   Each Vec2 source is a pair of Const nodes wired into Input (x and y consecutively).
//   The golden exercises 3 Vec2 inputs and confirms index-based selection + floor-mod wrapping.
//
// FORKS (named):
//   - fork-pickvec2-vec2-as-2-floats (fork-vec4-decompose-arity precedent): each Vec2 wire in the
//     multiInput produces 2 consecutive Float values in in[] (x, y). The resident gather already
//     handles this for multi-component ports — the output is also 2 Float ports (Selected.x, .y).
//   - fork-pickvec2-index-int: TiXL Index is int; runtime truncates the Float port value via (int)
//     before Mod — matches PickFloat's fork-pickfloat-index-int.
//   - fork-pickvec2-empty-passthrough: TiXL returns without writing Selected when Input is empty.
//     Runtime returns 0 (same neutral as PickFloat's fork-pickfloat-empty-passthrough).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"       // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (golden)
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runPickVector2SelfTest(bool injectBug);

namespace {

// TiXL MathUtils.Mod (floor-mod) — same helper as PickFloat.
int tixlMod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// in[] layout for ONE Vec2 multiInput (K sources, each 2 floats) + trailing Index:
//   [src0.x, src0.y, ..., srcK-1.x, srcK-1.y, Index]  n = 2K+1.
// K = (n-1)/2 (number of connected Vec2 sources).
// Selected component (0=x, 1=y) of Mod((int)Index, K)-th source.
float evalPickVec2(int outIdx, const float* in, int n, const EvaluationContext&) {
  // outIdx-2: 0=Selected.x, 1=Selected.y (output ports after Input head + Index)
  const int comp = outIdx - 2;
  if (comp < 0 || comp > 1) return 0.0f;
  if (n < 3) return 0.0f;              // need at least 1 Vec2 (2 floats) + Index
  const int K = (n - 1) / 2;          // number of Vec2 sources
  if (K == 0) return 0.0f;            // fork-pickvec2-empty-passthrough
  const int idx = tixlMod((int)in[n - 1], K);  // fork-pickvec2-index-int
  return in[2 * idx + comp];          // select 2-float block, pick component
}

}  // namespace

static const ValueOp _reg_pickvector2{
    // PickVector2 (TiXL Lib.numbers.vec2.PickVector2):
    //   Selected.Value = connections[Mod(Index, count)].GetValue(context).
    // Port order: Input (multiInput Vec2 head, vecArity=2), Index (trailing scalar), Selected.x/.y.
    // Defaults from PickVector2.t3: Index=0; Input default {X:0.0, Y:0.0}.
    // fork-pickvec2-vec2-as-2-floats: Vec2 multiInput = 2 Float ports per source in in[].
    {"PickVector2", "PickVector2",
     {{"Input",   "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2,
       /*multiInput=*/true},
      {"Index",      "Index",      "Float", true, 0.0f, 0.0f, 1000.0f, Widget::Slider},
      {"Selected.x", "Selected.x", "Float", false},
      {"Selected.y", "Selected.y", "Float", false}},
     evalPickVec2},
    "pickvector2", runPickVector2SelfTest};

// --- PickVector2 MATH golden (resident path — multiInput needs the resident gather) --------------
// Mirror of PickFloat golden. 3 Vec2 sources with distinct (x,y) values → verify index-based
// selection and floor-mod wrapping. Each Vec2 source is one atomic Const-pair symbol wired into
// the PickVector2.Input multiInput port. evalResidentFloat is called once per output component.
namespace {
Symbol pv2Atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

int runPickVector2SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // 3 Vec2 sources: A=(1.0,2.0), B=(3.0,4.0), C=(5.0,6.0).
  // PickVector2 multiInput: each Vec2 is 2 Float ports per source.
  // Golden: index=1 → B=(3.0,4.0); index=4 → Mod(4,3)=1 → B; index=-1 → Mod(-1,3)=2 → C=(5.0,6.0).
  //
  // Build symbols: ConstX/ConstY produce individual float components; PickVector2 receives them.
  // IMPORTANT: the resident gather for a vecArity=2 multiInput collects pairs (x,y) per source.
  // We wire ConstX → Input (head), ConstY → Input (second — same head port, extends the vec row).
  // Actually: for a Vec2 multiInput, each "source" is 2 wires. The conventional approach is to
  // wire 2 consts per source into the multiInput head. But the resident gather for vecArity=2
  // collects N*2 floats. Let's use Vec2-valued atomic consts instead of float pairs.

  // Simpler approach consistent with PickFloat golden: we use a PickVector2 node where each
  // Vec2 input source contributes its x and y as separate Const nodes wired to "Input" (the
  // multiInput head). The resident gather collects them in order: [src0.x, src0.y, ...].
  // Since this is vecArity=2, each PAIR of consecutive floats in the multiInput gather forms
  // one Vec2. So we wire {ConstA_x → Input, ConstA_y → Input, ConstB_x → Input, ConstB_y → Input,
  // ConstC_x → Input, ConstC_y → Input} to represent 3 Vec2 sources.
  // Then Index wire selects which Vec2: 0→(1,2), 1→(3,4), 2→(5,6).

  Symbol cst = pv2Atomic("Const",
                          {{"value", "value", "Float", 0.0f}},
                          {{"out",   "out",   "Float", 0.0f}});
  Symbol pv2 = pv2Atomic("PickVector2",
                          {{"Input", "Input", "Float", 0.0f}, {"Index", "Index", "Float", 0.0f}},
                          {{"Selected.x", "Selected.x", "Float", 0.0f},
                           {"Selected.y", "Selected.y", "Float", 0.0f}});

  // Helper: pick Vec2 at index `idx` from sources {(1,2),(3,4),(5,6)}, return component `comp` (0=x,1=y).
  auto pickWith = [&](float idx, int comp) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[pv2.id] = pv2;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"Selected.x", "Selected.x", "Float", 0.0f},
                       {"Selected.y", "Selected.y", "Float", 0.0f}};
    // 6 Const nodes: A.x=1, A.y=2, B.x=3, B.y=4, C.x=5, C.y=6 + 1 Const for Index.
    SymbolChild ca_x; ca_x.id = 1; ca_x.symbolId = "Const"; ca_x.overrides["value"] = 1.0f;
    SymbolChild ca_y; ca_y.id = 2; ca_y.symbolId = "Const"; ca_y.overrides["value"] = 2.0f;
    SymbolChild cb_x; cb_x.id = 3; cb_x.symbolId = "Const"; cb_x.overrides["value"] = 3.0f;
    SymbolChild cb_y; cb_y.id = 4; cb_y.symbolId = "Const"; cb_y.overrides["value"] = 4.0f;
    SymbolChild cc_x; cc_x.id = 5; cc_x.symbolId = "Const"; cc_x.overrides["value"] = 5.0f;
    SymbolChild cc_y; cc_y.id = 6; cc_y.symbolId = "Const"; cc_y.overrides["value"] = 6.0f;
    SymbolChild ci;   ci.id   = 7; ci.symbolId   = "Const"; ci.overrides["value"]   = idx;
    SymbolChild pk;   pk.id   = 8; pk.symbolId   = "PickVector2";
    root.children = {ca_x, ca_y, cb_x, cb_y, cc_x, cc_y, ci, pk};
    root.connections = {
        {1, "out", 8, "Input"},       // A.x → multiInput
        {2, "out", 8, "Input"},       // A.y → multiInput (same port, extends gather)
        {3, "out", 8, "Input"},       // B.x → multiInput
        {4, "out", 8, "Input"},       // B.y → multiInput
        {5, "out", 8, "Input"},       // C.x → multiInput
        {6, "out", 8, "Input"},       // C.y → multiInput
        {7, "out", 8, "Index"},       // trailing regular port
        {8, comp == 0 ? "Selected.x" : "Selected.y", kSymbolBoundary,
         comp == 0 ? "Selected.x" : "Selected.y"},
    };
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    const char* outPort = (comp == 0) ? "Selected.x" : "Selected.y";
    auto it = g.outputs.find(outPort);
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  // TYPICAL: idx=1 → source B=(3,4). injectBug asserts wrong x=1 (source A) → flips RED.
  {
    float rx = pickWith(1.0f, 0);
    float wantX = injectBug ? 1.0f : 3.0f;  // bug: index ignored → always picks A.x=1
    bool pass = std::fabs(rx - wantX) < eps;
    ok = ok && pass;
    printf("[selftest-pickvector2] Pick(idx=1).x=%.4f want=%.4f -> %s\n",
           rx, wantX, pass ? "PASS" : "FAIL");
  }
  {
    float ry = pickWith(1.0f, 1);
    bool pass = std::fabs(ry - 4.0f) < eps;  // source B.y=4
    ok = ok && pass;
    printf("[selftest-pickvector2] Pick(idx=1).y=%.4f want=4.0000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }

  // WRAP (positive): idx=4 → Mod(4,3)=1 → source B=(3,4).
  {
    float rx = pickWith(4.0f, 0);
    bool pass = std::fabs(rx - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickvector2] Pick(idx=4→Mod→1).x=%.4f want=3.0000 (wrap) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  // WRAP (negative floor-mod): idx=-1 → Mod(-1,3)=2 → source C=(5,6).
  {
    float rx = pickWith(-1.0f, 0);
    float ry = pickWith(-1.0f, 1);
    bool pass = std::fabs(rx - 5.0f) < eps && std::fabs(ry - 6.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickvector2] Pick(idx=-1→Mod→2)=(%.4f,%.4f) want=(5,6) floor-mod -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: idx=0 → source A=(1,2).
  {
    float rx = pickWith(0.0f, 0);
    float ry = pickWith(0.0f, 1);
    bool pass = std::fabs(rx - 1.0f) < eps && std::fabs(ry - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickvector2] Pick(idx=0)=(%.4f,%.4f) want=(1,2) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
