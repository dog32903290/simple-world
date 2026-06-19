// PickColor value op (value-op self-registration seam leaf — Phase C numbers/vec4 mining).
// TiXL authority: Operators/Lib/numbers/vec4/PickColor.cs (lines 16-24).
//
//   PickColor.cs Update():
//     var connections = Input.GetCollectedTypedInputs();
//     if (connections == null || connections.Count == 0) return;          // line 18-20
//     var index = Index.GetValue(context).Mod(connections.Count);          // line 22
//     Selected.Value = connections[index].GetValue(context);              // line 23
//
//   TiXL MathUtils.Mod (floor-mod): same as PickFloat / PickVector2 / PickVector3.
//   PickColor.cs: Index = InputSlot<int>(0) (line 30); Input = MultiInputSlot<Vector4> (line 27).
//
// Input = MultiInputSlot<Vector4>; Index = InputSlot<int> (default 0).
// Output: Selected (Slot<Vector4>) — exposed as 4 Float ports (Selected.x/.y/.z/.w).
//
// EVAL-SIDE LAYOUT (mirror of PickVector3 but vecArity=4):
//   Resident gather: each Vec4 source contributes 4 consecutive floats (x, y, z, w) in in[].
//   Trailing port: Index = in[n-1]. K = (n-1)/4 source count.
//   in[] = [src0.x, src0.y, src0.z, src0.w, ..., srcK-1.x, .y, .z, .w, Index], n = 4K+1.
//   Selected component (0=x, 1=y, 2=z, 3=w) of Mod((int)Index, K)-th source.
//   Output ports Selected.x/.y/.z/.w at spec indices 2/3/4/5; outIdx-2 = component.
//
// FORKS (named):
//   - fork-pickcolor-vec4-as-4-floats (sw has no native vec4/Color port; precedent
//     value_op_pickvector3.cpp fork-pickvec3-vec3-as-3-floats / Vector4Components):
//     Each Vec4 wire in the multiInput contributes 4 consecutive Float values in in[].
//     Output is 4 Float ports (Selected.x/.y/.z/.w). NOT an eval fork — TiXL PickColor.cs:9
//     Slot<Vector4> Selected has no scalar decompose; sw decomposes it into 4 Floats.
//   - fork-pickcolor-index-int: TiXL Index is InputSlot<int> (PickColor.cs:30); runtime
//     truncates via (int) before Mod — matches int slot semantics.
//   - fork-pickcolor-empty-passthrough: empty multiInput → 0 (PickColor.cs:19-20 early return;
//     same as PickFloat/PickVector2/PickVector3).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"       // Symbol/SymbolChild/SymbolLibrary/SlotDef
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runPickColorSelfTest(bool injectBug);

namespace {

int tixlMod4(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// in[] layout for ONE Vec4 multiInput (K sources, each 4 floats) + trailing Index:
//   [src0.x, src0.y, src0.z, src0.w, ..., srcK-1.x, .y, .z, .w, Index]  n = 4K+1.
// K = (n-1)/4. Component (0=x, 1=y, 2=z, 3=w) of Mod((int)Index, K)-th source.
float evalPickColor(int outIdx, const float* in, int n, const EvaluationContext&) {
  const int comp = outIdx - 2;        // output offset: Selected.x=0, .y=1, .z=2, .w=3
  if (comp < 0 || comp > 3) return 0.0f;
  if (n < 5) return 0.0f;             // need at least 1 Vec4 (4 floats) + Index
  const int K = (n - 1) / 4;
  if (K == 0) return 0.0f;           // fork-pickcolor-empty-passthrough
  const int idx = tixlMod4((int)in[n - 1], K);  // fork-pickcolor-index-int
  return in[4 * idx + comp];
}

}  // namespace

static const ValueOp _reg_pickcolor{
    // PickColor (TiXL Lib.numbers.vec4.PickColor):
    //   Selected.Value = connections[Mod(Index, count)].GetValue(context).
    // Port order: Input (multiInput Vec4 head, vecArity=4), Index (trailing scalar),
    //             Selected.x/.y/.z/.w (4 Float outputs).
    // Defaults from PickColor.cs: Index=0 (InputSlot<int>(0)); Input default {0,0,0,0}.
    // fork-pickcolor-vec4-as-4-floats: Vec4 multiInput = 4 Float ports per source in in[].
    {"PickColor", "PickColor",
     {{"Input",      "Input",      "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4,
       /*multiInput=*/true},
      {"Index",      "Index",      "Float", true, 0.0f, 0.0f, 1000.0f, Widget::Slider},
      {"Selected.x", "Selected.x", "Float", false},
      {"Selected.y", "Selected.y", "Float", false},
      {"Selected.z", "Selected.z", "Float", false},
      {"Selected.w", "Selected.w", "Float", false}},
     evalPickColor},
    "pickcolor", runPickColorSelfTest};

// --- PickColor MATH golden (resident path) -----------------------------------------------------
// Mirror of PickVector3 golden extended to 4 components (RGBA). 2 Vec4 (color) sources:
//   A=(1,0,0,1) [red], B=(0,1,0,1) [green].
// 8 Const nodes for components + 1 Const for Index, all wired to PickColor.Input multiInput.
// Verifies index-based selection + floor-mod wrapping on all 4 channels.
//   idx=1  → B=(0,1,0,1)
//   idx=-1 → Mod(-1,2)=1 → B
//   idx=3  → Mod(3,2)=1  → B
//   idx=0  → A=(1,0,0,1)
namespace {
Symbol pcAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

int runPickColorSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  Symbol cst = pcAtomic("Const",
                        {{"value", "value", "Float", 0.0f}},
                        {{"out",   "out",   "Float", 0.0f}});
  Symbol pc = pcAtomic("PickColor",
                       {{"Input", "Input", "Float", 0.0f}, {"Index", "Index", "Float", 0.0f}},
                       {{"Selected.x", "Selected.x", "Float", 0.0f},
                        {"Selected.y", "Selected.y", "Float", 0.0f},
                        {"Selected.z", "Selected.z", "Float", 0.0f},
                        {"Selected.w", "Selected.w", "Float", 0.0f}});

  // Helper: pick Vec4 color at index `idx` from sources {A,B}, return component
  // `comp` (0=x/r, 1=y/g, 2=z/b, 3=w/a). A=(1,0,0,1), B=(0,1,0,1).
  auto pickWith = [&](float idx, int comp) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[pc.id] = pc;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"Selected.x", "Selected.x", "Float", 0.0f},
                       {"Selected.y", "Selected.y", "Float", 0.0f},
                       {"Selected.z", "Selected.z", "Float", 0.0f},
                       {"Selected.w", "Selected.w", "Float", 0.0f}};
    // A=(1,0,0,1), B=(0,1,0,1)
    SymbolChild ca_x; ca_x.id=1; ca_x.symbolId="Const"; ca_x.overrides["value"]=1.0f;
    SymbolChild ca_y; ca_y.id=2; ca_y.symbolId="Const"; ca_y.overrides["value"]=0.0f;
    SymbolChild ca_z; ca_z.id=3; ca_z.symbolId="Const"; ca_z.overrides["value"]=0.0f;
    SymbolChild ca_w; ca_w.id=4; ca_w.symbolId="Const"; ca_w.overrides["value"]=1.0f;
    SymbolChild cb_x; cb_x.id=5; cb_x.symbolId="Const"; cb_x.overrides["value"]=0.0f;
    SymbolChild cb_y; cb_y.id=6; cb_y.symbolId="Const"; cb_y.overrides["value"]=1.0f;
    SymbolChild cb_z; cb_z.id=7; cb_z.symbolId="Const"; cb_z.overrides["value"]=0.0f;
    SymbolChild cb_w; cb_w.id=8; cb_w.symbolId="Const"; cb_w.overrides["value"]=1.0f;
    SymbolChild ci;   ci.id=9;   ci.symbolId="Const";   ci.overrides["value"]=idx;
    SymbolChild pk;   pk.id=10;  pk.symbolId="PickColor";
    root.children = {ca_x,ca_y,ca_z,ca_w,cb_x,cb_y,cb_z,cb_w,ci,pk};
    const char* outPorts[] = {"Selected.x","Selected.y","Selected.z","Selected.w"};
    root.connections = {
        { 1,"out",10,"Input"},  // A.x
        { 2,"out",10,"Input"},  // A.y
        { 3,"out",10,"Input"},  // A.z
        { 4,"out",10,"Input"},  // A.w
        { 5,"out",10,"Input"},  // B.x
        { 6,"out",10,"Input"},  // B.y
        { 7,"out",10,"Input"},  // B.z
        { 8,"out",10,"Input"},  // B.w
        { 9,"out",10,"Index"},
        {10, outPorts[comp], kSymbolBoundary, outPorts[comp]},
    };
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find(outPorts[comp]);
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  // TYPICAL: idx=1 → source B=(0,1,0,1). injectBug asserts wrong x=0... actually identical;
  // injectBug instead targets the floor-mod (idx=-1 case below) — see fork note.
  {
    float rx = pickWith(1.0f, 0);
    float ry = pickWith(1.0f, 1);
    float rz = pickWith(1.0f, 2);
    float rw = pickWith(1.0f, 3);
    bool pass = std::fabs(rx-0.0f)<eps && std::fabs(ry-1.0f)<eps &&
                std::fabs(rz-0.0f)<eps && std::fabs(rw-1.0f)<eps;  // B=(0,1,0,1)
    ok = ok && pass;
    printf("[selftest-pickcolor] Pick(idx=1)=(%.4f,%.4f,%.4f,%.4f) want=(0,1,0,1) -> %s\n",
           rx, ry, rz, rw, pass ? "PASS" : "FAIL");
  }

  // FLOOR-MOD (negative): idx=-1 → Mod(-1,2)=1 → B=(0,1,0,1).
  // injectBug drops floor-mod (raw % → -1 % 2 = -1 → wrong/out-of-range source) → expect A.
  {
    float rx = pickWith(-1.0f, 0);
    float ry = pickWith(-1.0f, 1);
    // injectBug: raw %  gives -1 → bug path would land on A (or garbage); golden flips to A.
    float wantX = injectBug ? 1.0f : 0.0f;  // bug: no floor-mod → A.x=1; correct → B.x=0
    float wantY = injectBug ? 0.0f : 1.0f;  // bug: → A.y=0;             correct → B.y=1
    bool pass = std::fabs(rx-wantX)<eps && std::fabs(ry-wantY)<eps;
    ok = ok && pass;
    printf("[selftest-pickcolor] Pick(idx=-1→Mod→1).xy=(%.4f,%.4f) want=(%.4f,%.4f) -> %s\n",
           rx, ry, wantX, wantY, pass ? "PASS" : "FAIL");
  }

  // WRAP (positive): idx=3 → Mod(3,2)=1 → B=(0,1,0,1).
  {
    float rx = pickWith(3.0f, 0);
    float ry = pickWith(3.0f, 1);
    bool pass = std::fabs(rx-0.0f)<eps && std::fabs(ry-1.0f)<eps;  // B
    ok = ok && pass;
    printf("[selftest-pickcolor] Pick(idx=3→Mod→1).xy=(%.4f,%.4f) want=(0,1) (wrap) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: idx=0 → A=(1,0,0,1).
  {
    float rx = pickWith(0.0f, 0);
    float ry = pickWith(0.0f, 1);
    float rz = pickWith(0.0f, 2);
    float rw = pickWith(0.0f, 3);
    bool pass = std::fabs(rx-1.0f)<eps && std::fabs(ry-0.0f)<eps &&
                std::fabs(rz-0.0f)<eps && std::fabs(rw-1.0f)<eps;  // A=(1,0,0,1)
    ok = ok && pass;
    printf("[selftest-pickcolor] Pick(idx=0)=(%.4f,%.4f,%.4f,%.4f) want=(1,0,0,1) -> %s\n",
           rx, ry, rz, rw, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
