// PickVector3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/PickVector3.cs (+ PickVector3.t3 defaults).
//
//   PickVector3.cs Update():
//     var connections = Input.GetCollectedTypedInputs();
//     if (connections == null || connections.Count == 0) return;
//     var index = Index.GetValue(context).Mod(connections.Count);
//     Selected.Value = connections[index].GetValue(context);
//
//   TiXL MathUtils.Mod (floor-mod): same as PickFloat / PickVector2.
//   PickVector3.t3: Index default 0; Input default {X:0.0, Y:0.0, Z:0.0}.
//
// Input = MultiInputSlot<Vector3>; Index = InputSlot<int> (default 0).
// Output: Selected (Slot<Vector3>) — exposed as 3 Float ports (Selected.x, .y, .z).
//
// EVAL-SIDE LAYOUT (mirror of PickVector2 but vecArity=3):
//   Resident gather: each Vec3 source contributes 3 consecutive floats (x, y, z) in in[].
//   Trailing port: Index = in[n-1]. K = (n-1)/3 source count.
//   in[] = [src0.x, src0.y, src0.z, ..., srcK-1.x, .y, .z, Index], n = 3K+1.
//   Selected component (0=x, 1=y, 2=z) of Mod((int)Index, K)-th source.
//   Output ports Selected.x/.y/.z at spec indices 2/3/4; outIdx-2 = component.
//
// FORKS (named):
//   - fork-pickvec3-vec3-as-3-floats (fork-vec4-decompose-arity precedent — Vector4Components):
//     Each Vec3 wire in the multiInput contributes 3 consecutive Float values in in[].
//     Output is 3 Float ports (Selected.x/.y/.z). NOT an eval fork.
//   - fork-pickvec3-index-int: TiXL Index is int; runtime truncates via (int) before Mod.
//   - fork-pickvec3-empty-passthrough: empty multiInput → 0 (same as PickFloat/PickVector2).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdio>

#include "runtime/compound_graph.h"       // Symbol/SymbolChild/SymbolLibrary/SlotDef
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat
#include "runtime/value_op_registry.h"    // ValueOp self-registration

namespace sw {

int runPickVector3SelfTest(bool injectBug);

namespace {

int tixlMod3(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

// in[] layout for ONE Vec3 multiInput (K sources, each 3 floats) + trailing Index:
//   [src0.x, src0.y, src0.z, ..., srcK-1.x, .y, .z, Index]  n = 3K+1.
// K = (n-1)/3. Component (0=x, 1=y, 2=z) of Mod((int)Index, K)-th source.
float evalPickVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  const int comp = outIdx - 2;        // output offset: Selected.x=0, .y=1, .z=2
  if (comp < 0 || comp > 2) return 0.0f;
  if (n < 4) return 0.0f;             // need at least 1 Vec3 (3 floats) + Index
  const int K = (n - 1) / 3;
  if (K == 0) return 0.0f;           // fork-pickvec3-empty-passthrough
  const int idx = tixlMod3((int)in[n - 1], K);  // fork-pickvec3-index-int
  return in[3 * idx + comp];
}

}  // namespace

static const ValueOp _reg_pickvector3{
    // PickVector3 (TiXL Lib.numbers.vec3.PickVector3):
    //   Selected.Value = connections[Mod(Index, count)].GetValue(context).
    // Port order: Input (multiInput Vec3 head, vecArity=3), Index (trailing scalar),
    //             Selected.x/.y/.z (3 Float outputs).
    // Defaults from PickVector3.t3: Index=0; Input default {X:0,Y:0,Z:0}.
    // fork-pickvec3-vec3-as-3-floats: Vec3 multiInput = 3 Float ports per source in in[].
    {"PickVector3", "PickVector3",
     {{"Input",      "Input",      "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3,
       /*multiInput=*/true},
      {"Index",      "Index",      "Float", true, 0.0f, 0.0f, 1000.0f, Widget::Slider},
      {"Selected.x", "Selected.x", "Float", false},
      {"Selected.y", "Selected.y", "Float", false},
      {"Selected.z", "Selected.z", "Float", false}},
     evalPickVec3},
    "pickvector3", runPickVector3SelfTest};

// --- PickVector3 MATH golden (resident path) ---------------------------------------------------
// Mirror of PickVector2 golden extended to 3 components. 3 Vec3 sources:
//   A=(1,2,3), B=(4,5,6), C=(7,8,9).
// 9 Const nodes for components + 1 Const for Index, all wired to PickVector3.Input multiInput.
// Verifies index-based selection + floor-mod wrapping on all 3 components.
namespace {
Symbol pv3Atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}
}  // namespace

int runPickVector3SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  Symbol cst = pv3Atomic("Const",
                          {{"value", "value", "Float", 0.0f}},
                          {{"out",   "out",   "Float", 0.0f}});
  Symbol pv3 = pv3Atomic("PickVector3",
                          {{"Input", "Input", "Float", 0.0f}, {"Index", "Index", "Float", 0.0f}},
                          {{"Selected.x", "Selected.x", "Float", 0.0f},
                           {"Selected.y", "Selected.y", "Float", 0.0f},
                           {"Selected.z", "Selected.z", "Float", 0.0f}});

  // Helper: pick Vec3 at index `idx` from sources {A,B,C}, return component `comp` (0=x,1=y,2=z).
  auto pickWith = [&](float idx, int comp) -> float {
    SymbolLibrary lib;
    lib.symbols[cst.id] = cst;
    lib.symbols[pv3.id] = pv3;
    Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
    root.outputDefs = {{"Selected.x", "Selected.x", "Float", 0.0f},
                       {"Selected.y", "Selected.y", "Float", 0.0f},
                       {"Selected.z", "Selected.z", "Float", 0.0f}};
    // A=(1,2,3), B=(4,5,6), C=(7,8,9)
    SymbolChild ca_x; ca_x.id=1; ca_x.symbolId="Const"; ca_x.overrides["value"]=1.0f;
    SymbolChild ca_y; ca_y.id=2; ca_y.symbolId="Const"; ca_y.overrides["value"]=2.0f;
    SymbolChild ca_z; ca_z.id=3; ca_z.symbolId="Const"; ca_z.overrides["value"]=3.0f;
    SymbolChild cb_x; cb_x.id=4; cb_x.symbolId="Const"; cb_x.overrides["value"]=4.0f;
    SymbolChild cb_y; cb_y.id=5; cb_y.symbolId="Const"; cb_y.overrides["value"]=5.0f;
    SymbolChild cb_z; cb_z.id=6; cb_z.symbolId="Const"; cb_z.overrides["value"]=6.0f;
    SymbolChild cc_x; cc_x.id=7; cc_x.symbolId="Const"; cc_x.overrides["value"]=7.0f;
    SymbolChild cc_y; cc_y.id=8; cc_y.symbolId="Const"; cc_y.overrides["value"]=8.0f;
    SymbolChild cc_z; cc_z.id=9; cc_z.symbolId="Const"; cc_z.overrides["value"]=9.0f;
    SymbolChild ci;   ci.id=10;  ci.symbolId="Const";   ci.overrides["value"]=idx;
    SymbolChild pk;   pk.id=11;  pk.symbolId="PickVector3";
    root.children = {ca_x,ca_y,ca_z,cb_x,cb_y,cb_z,cc_x,cc_y,cc_z,ci,pk};
    const char* outPorts[] = {"Selected.x","Selected.y","Selected.z"};
    root.connections = {
        { 1,"out",11,"Input"},  // A.x
        { 2,"out",11,"Input"},  // A.y
        { 3,"out",11,"Input"},  // A.z
        { 4,"out",11,"Input"},  // B.x
        { 5,"out",11,"Input"},  // B.y
        { 6,"out",11,"Input"},  // B.z
        { 7,"out",11,"Input"},  // C.x
        { 8,"out",11,"Input"},  // C.y
        { 9,"out",11,"Input"},  // C.z
        {10,"out",11,"Index"},
        {11, outPorts[comp], kSymbolBoundary, outPorts[comp]},
    };
    lib.symbols[root.id] = root; lib.rootId = "Root";
    ResidentEvalCtx ctx;
    ResidentEvalGraph g = buildEvalGraph(lib, "Root");
    auto it = g.outputs.find(outPorts[comp]);
    return it == g.outputs.end() ? -999.0f
                                 : evalResidentFloat(g, it->second.first, it->second.second, ctx);
  };

  // TYPICAL: idx=1 → source B=(4,5,6). injectBug asserts wrong x=1 (source A.x) → flips RED.
  {
    float rx = pickWith(1.0f, 0);
    float wantX = injectBug ? 1.0f : 4.0f;  // bug: index ignored → A.x=1
    bool pass = std::fabs(rx - wantX) < eps;
    ok = ok && pass;
    printf("[selftest-pickvector3] Pick(idx=1).x=%.4f want=%.4f -> %s\n",
           rx, wantX, pass ? "PASS" : "FAIL");
  }
  {
    float ry = pickWith(1.0f, 1);
    bool pass = std::fabs(ry - 5.0f) < eps;  // B.y=5
    ok = ok && pass;
    printf("[selftest-pickvector3] Pick(idx=1).y=%.4f want=5.0000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = pickWith(1.0f, 2);
    bool pass = std::fabs(rz - 6.0f) < eps;  // B.z=6
    ok = ok && pass;
    printf("[selftest-pickvector3] Pick(idx=1).z=%.4f want=6.0000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // WRAP (positive): idx=4 → Mod(4,3)=1 → B=(4,5,6).
  {
    float rx = pickWith(4.0f, 0);
    bool pass = std::fabs(rx - 4.0f) < eps;
    ok = ok && pass;
    printf("[selftest-pickvector3] Pick(idx=4→Mod→1).x=%.4f want=4.0000 (wrap) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  // FLOOR-MOD (negative): idx=-1 → Mod(-1,3)=2 → C=(7,8,9).
  {
    float rx = pickWith(-1.0f, 0);
    float ry = pickWith(-1.0f, 1);
    float rz = pickWith(-1.0f, 2);
    bool pass = std::fabs(rx-7.0f)<eps && std::fabs(ry-8.0f)<eps && std::fabs(rz-9.0f)<eps;
    ok = ok && pass;
    printf("[selftest-pickvector3] Pick(idx=-1→Mod→2)=(%.4f,%.4f,%.4f) want=(7,8,9) -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: idx=0 → A=(1,2,3).
  {
    float rx = pickWith(0.0f, 0);
    float ry = pickWith(0.0f, 1);
    float rz = pickWith(0.0f, 2);
    bool pass = std::fabs(rx-1.0f)<eps && std::fabs(ry-2.0f)<eps && std::fabs(rz-3.0f)<eps;
    ok = ok && pass;
    printf("[selftest-pickvector3] Pick(idx=0)=(%.4f,%.4f,%.4f) want=(1,2,3) -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
