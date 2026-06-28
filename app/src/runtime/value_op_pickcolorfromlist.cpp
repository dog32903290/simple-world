// PickColorFromList value op (ColorList-consumer → host-value(vec4)-emit leaf — the LAST op of the
// vec4/color family). TiXL authority: external/tixl/Operators/Lib/numbers/color/PickColorFromList.cs:
//
//   PickColorFromList.cs Update():
//     var list = Input.GetValue(context);                   // InputSlot<List<Vector4>>
//     if (list == null || list.Count == 0) return;          // (TiXL keeps Selected at prev value)
//     var index = Index.GetValue(context).Mod(list.Count);  // T3 FLOOR-Mod (MathUtils.cs:273)
//     Selected.Value = list[index];                         // Slot<Vector4>
//
//   Ports: Input = InputSlot<List<Vector4>> (the ColorList rail); Index = InputSlot<int>(0).
//   Output: Selected = Slot<Vector4> — exposed here as 4 Float ports (Selected.x/.y/.z/.w).
//
// This is a HOST-EMIT op: its value comes from gathering the runtime ColorList currency + the resolved
// Index — NOT from a pure evaluate(Float...) decompose. So (exactly like value_op_pickcolor.cpp's PickColor
// host-emit leaf and RequestedResolution's cook-emit leaf) it carries evaluate==nullptr and is cooked once
// per frame by cookColorPickNodes (below) which writes ResidentNode::extOut[0..3] — the EXACT channel
// evalResidentFloat reads for a !evaluate node. A downstream Float wire to Selected.x/.y/.z/.w resolves to
// its extOut slot by port-id match (no new wire type, no new resolver). Routed onto extOut[] (NOT
// extColorOut — that vec4-LIST channel is the wrong key; we emit ONE picked vec4 = 4 scalars).
//
// MECHANISM (copies existing infra, invents none):
//   • Gather the ColorList input via cookResidentColorList (resident_colorlist_cook.cpp:54) — the SAME
//     resident-driver walk every ColorList consumer uses; the producer (ColorsToList) is settled first
//     because cook_host_values.cpp calls cookColorListNodes BEFORE cookColorPickNodes (producer-before-
//     consumer, cook_host_values.cpp:35,61).
//   • Recover the Index int with std::lround BEFORE floor-Mod (the fork-int-bool-dissolve-to-float
//     convention, host_scalar_ops_pickfloatfromlist.cpp:52) — NOT raw (int) truncation, so a negative
//     Index floor-Mods exactly like TiXL (Mod(-1,3)=2).
//
// FORKS (named):
//   - fork-pickcolorfromlist-empty-is-zero: TiXL leaves Selected at its PREVIOUS frame value on an
//     empty/null list (a stateful last-good cache). sw's host-emit path has NO per-node previous-value
//     cache, and the sibling host_scalar_ops_pickfloatfromlist.cpp already forks empty→0. We MATCH that
//     sibling: empty/null list → write (0,0,0,0) to extOut[0..3]. This is a degenerate edge case (an
//     empty color list); we deliberately do NOT build a new stateful previous-value seam for it.
//   - fork-pickcolorfromlist-vec4-as-4-floats: TiXL Selected is ONE Slot<Vector4>; sw decomposes it into
//     4 Float output ports (the established vecN-as-N-floats fork, mirror of value_op_pickcolor.cpp).
//   - fork-int-bool-dissolve-to-float: TiXL Index is InputSlot<int>; sw has no Int port → Index rides a
//     Float port, recovered with std::lround before floor-Mod (Cut32 convention).
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentEvalCtx / cookResidentColorList

#include <cmath>   // std::lround
#include <cstdio>
#include <vector>

#include <simd/simd.h>

#include "runtime/compound_graph.h"     // Symbol/SymbolChild/SymbolLibrary/SlotDef (golden graph build)
#include "runtime/graph.h"              // NodeSpec / PortSpec / Widget / findSpec
#include "runtime/value_op_registry.h"  // ValueOp self-registration + valueOpSelfTests()

namespace sw {

int runPickColorFromListSelfTest(bool injectBug);

namespace {

// Test-only injection seam (golden): when set, cookColorPickNodes DROPS the T3 floor-Mod and uses raw C
// remainder (`%`) instead — so a negative Index lands on the WRONG element (clamped into range), corrupting
// the REAL production output. The golden flips this ON, asserts the CORRECT (floor-mod) expected value, and
// the corrupted output then MISMATCHES → RED. This is a real impl tooth on the cook path, NOT an
// assertion/expected-value flip. Off in production. Mirror of colorListInjectBug()/hostScalarInjectBug().
bool& pickColorInjectBug() {
  static bool b = false;
  return b;
}

// T3 floor-Mod (MathUtils.cs:273-284): repeat==0 → 0; x = val % repeat; if (x<0) x += repeat.
int t3Mod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

}  // namespace

// Per-frame PRODUCTION cook-emit for PickColorFromList: gather the wired ColorList input through the
// resident drivers, recover Index, floor-Mod, and emit the picked vec4 onto extOut[0..3]. Mirror of
// cookValueOutputNodes (resident_value_output_cook.cpp) but the value comes from a gathered ColorList +
// the resolved Index rather than from ctx fields. Called from cook_host_values.cpp AFTER cookColorListNodes
// (so the ColorsToList producer this op consumes is already settled). Mutates g (writes extOut). No Metal.
void cookColorPickNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "PickColorFromList") continue;
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // Map the 4 Selected.x/.y/.z/.w OUTPUT ports to their extOut[] slots. evalResidentFloat's !evaluate
    // readback returns extOut[i] where i = the port's FULL index in s->ports (inputs included) — NOT a
    // 0-based output index (resident_eval_graph.cpp:112-114). With Input(0),Index(1) ahead of them the
    // Selected.* ports sit at spec indices 2..5, so we must write extOut[2..5], not extOut[0..3].
    int outSlot[4] = {-1, -1, -1, -1};  // extOut index for Selected.x/.y/.z/.w
    {
      int comp = 0;
      for (size_t i = 0; i < s->ports.size() && comp < 4; ++i)
        if (!s->ports[i].isInput) outSlot[comp++] = (int)i;
    }
    if (outSlot[3] < 0 || outSlot[3] >= 8) continue;  // defensive (PickColorFromList always has 4 outputs)

    // fork-pickcolorfromlist-empty-is-zero: default to (0,0,0,0); overwritten only when the list is
    // non-empty. (TiXL keeps the prev value; sw has no per-node prev cache → 0, matching PickFloat sibling.)
    for (int c = 0; c < 4; ++c) rn.extOut[outSlot[c]] = 0.0f;

    // Gather the "Input" ColorList port through the resident Connection driver (the SAME walk every
    // ColorList consumer uses). cookResidentColorList recurses the upstream ColorsToList producer.
    const ResidentInput* in = rn.input("Input");
    std::vector<simd::float4> list;
    if (in && in->driver == ResidentInput::Driver::Connection)
      cookResidentColorList(g, in->srcNodePath, ctx, list, 0, /*state=*/nullptr);

    if (list.empty()) continue;  // PickColorFromList.cs:19-20 — empty/null guard (→ the 0,0,0,0 above)

    // Resolve Index (Float-dissolved int → recover with lround, NOT (int) truncation — fork-int-bool-
    // dissolve-to-float) then T3 floor-Mod over the list size (PickColorFromList.cs:22).
    std::map<std::string, float> params = resolveResidentFloatInputs(g, rn, ctx);
    const int idxRaw = (int)std::lround(params.count("Index") ? params["Index"] : 0.0f);
    int idx;
    if (pickColorInjectBug()) {
      // -bug: DROP T3 floor-mod — use raw C `%` then clamp a negative remainder to 0 (the plausible guard a
      // floor-mod-less impl bolts on to avoid UB). For Index=-1,size=3: raw % = -1 → clamp → 0 = element A,
      // where the CORRECT floor-mod gives 2 = element C. So the production output is genuinely WRONG here.
      idx = idxRaw % (int)list.size();
      if (idx < 0) idx = 0;
    } else {
      idx = t3Mod(idxRaw, (int)list.size());  // correct: negative wraps Mod(-1,3)=2, always in [0,size)
    }
    const simd::float4 picked = list[(size_t)idx];     // PickColorFromList.cs:23 — Selected = list[index]
    rn.extOut[outSlot[0]] = picked.x;
    rn.extOut[outSlot[1]] = picked.y;
    rn.extOut[outSlot[2]] = picked.z;
    rn.extOut[outSlot[3]] = picked.w;
  }
}

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point; the
// SW_VALUE_OP_SRCS glob compiles value_op_*.cpp). evaluate==nullptr: the value is host-emitted by
// cookColorPickNodes onto extOut[], NOT computed by a pure evaluate(Float...) fn.
//   Ports: "Input" = the ColorList input (the list to pick from); "Index" = the Float index param
//          (int dissolved to Float; negative valid — floor-Mod wraps); "Selected.x/.y/.z/.w" = the 4
//          Float outputs (extOut[0..3], the picked vec4 decomposed).
// PortSpec field order: id,name,dataType,isInput,def,minV,maxV,widget,...  Index range wide+symmetric
// (negative indices are valid). Output ports FIRST→ no: input first matches PickColor's layout; outIdx
// for Selected.x..w maps to extOut[0..3] by the evalResidentFloat !evaluate readback (port-id → outCache
// index order). Selected.* declared AFTER the inputs, at output-port indices 0..3 of the OUTPUT subset.
static const ValueOp _reg_pickcolorfromlist{
    {"PickColorFromList", "PickColorFromList",
     {{"Input", "Input", "ColorList", true},
      {"Index", "Index", "Float", true, 0.0f, -100000.0f, 100000.0f},
      {"Selected.x", "Selected.x", "Float", false},
      {"Selected.y", "Selected.y", "Float", false},
      {"Selected.z", "Selected.z", "Float", false},
      {"Selected.w", "Selected.w", "Float", false}},
     /*evaluate=*/nullptr}};  // host-emit via cookColorPickNodes (extOut[0..3]); no selftest pair here —
                              // the golden is registered separately below (extOut readback needs a graph)

// --- PickColorFromList cook-emit GOLDEN (3-leg, [G] machine-gated) -------------------------------------
// Model on value_op_pickcolor.cpp:105 (resident-graph build with Const sources) +
// resident_value_output_cook.cpp:80 (extOut readback). The producer is a real ColorsToList wired into
// PickColorFromList.Input. List = [(1,0,0,1),(0,1,0,1),(0,0,1,1)] (ALL 4 components differ per color so a
// channel-collapse can't coincidentally pass). Legs:
//   (1) build the resident graph, run cookColorListNodes then cookColorPickNodes, read extOut[0..3];
//   (2) cross-check evalResidentFloat(Selected.x|.y|.z|.w) == extOut (the !evaluate readback path);
//   (3) assert hand-derived TiXL values: Index=4 → Mod(4,3)=1 → (0,1,0,1); Index=-1 → Mod(-1,3)=2 →
//       (0,0,1,1); Index=0 → (1,0,0,1);
//   (4) empty-list leg (no ColorsToList wired) → (0,0,0,0) (fork-pickcolorfromlist-empty-is-zero).
// -bug = pickColorInjectBug() flips the REAL cook to raw `%`+clamp (no floor-mod), so the Index=-1 leg's
//        PRODUCTION output becomes A instead of the correct C → the (unchanged) expected C makes it FAIL
//        (RED). The expected values NEVER move — the tooth is on the impl/cook path.
namespace {

Symbol pcflAtomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Build a graph: 3-color ColorsToList → PickColorFromList.Input, Const → Index. Cook both passes, read
// the 4 Selected outputs via extOut AND evalResidentFloat. `wireColors`==false omits ColorsToList (empty
// list leg). The -bug tooth lives in cookColorPickNodes (pickColorInjectBug() armed by the caller) — it
// corrupts the REAL cook output, so this builder needs no bug branch.
struct PickResult { float x, y, z, w; bool evalAgrees; };

PickResult pickFromList(float idx, bool wireColors) {
  SymbolLibrary lib;
  Symbol cst = pcflAtomic("Const", {{"value", "value", "Float", 0.0f}},
                          {{"out", "out", "Float", 0.0f}});
  Symbol c2l = pcflAtomic("ColorsToList",
                          {{"Colors.x", "Colors.x", "Float", 0.0f}, {"Colors.y", "Colors.y", "Float", 0.0f},
                           {"Colors.z", "Colors.z", "Float", 0.0f}, {"Colors.w", "Colors.w", "Float", 0.0f}},
                          {{"out", "out", "ColorList", 0.0f}});
  Symbol pk = pcflAtomic("PickColorFromList",
                         {{"Input", "Input", "ColorList", 0.0f}, {"Index", "Index", "Float", 0.0f}},
                         {{"Selected.x", "Selected.x", "Float", 0.0f},
                          {"Selected.y", "Selected.y", "Float", 0.0f},
                          {"Selected.z", "Selected.z", "Float", 0.0f},
                          {"Selected.w", "Selected.w", "Float", 0.0f}});
  lib.symbols[cst.id] = cst;
  lib.symbols[c2l.id] = c2l;
  lib.symbols[pk.id] = pk;

  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"Selected.x", "Selected.x", "Float", 0.0f},
                     {"Selected.y", "Selected.y", "Float", 0.0f},
                     {"Selected.z", "Selected.z", "Float", 0.0f},
                     {"Selected.w", "Selected.w", "Float", 0.0f}};

  // 3 colors: A=(1,0,0,1) B=(0,1,0,1) C=(0,0,1,1). 12 Const sources (3 per channel), id 1..12.
  //   channel x consts: ids 1,2,3   = A.x=1, B.x=0, C.x=0
  //   channel y consts: ids 4,5,6   = A.y=0, B.y=1, C.y=0
  //   channel z consts: ids 7,8,9   = A.z=0, B.z=0, C.z=1
  //   channel w consts: ids 10,11,12= A.w=1, B.w=1, C.w=1
  const float vx[3] = {1, 0, 0}, vy[3] = {0, 1, 0}, vz[3] = {0, 0, 1}, vw[3] = {1, 1, 1};
  auto mkConst = [&](int id, float v) { SymbolChild c; c.id = id; c.symbolId = "Const";
                                        c.overrides["value"] = v; root.children.push_back(c); };
  for (int i = 0; i < 3; ++i) mkConst(1 + i, vx[i]);
  for (int i = 0; i < 3; ++i) mkConst(4 + i, vy[i]);
  for (int i = 0; i < 3; ++i) mkConst(7 + i, vz[i]);
  for (int i = 0; i < 3; ++i) mkConst(10 + i, vw[i]);

  SymbolChild c2lChild; c2lChild.id = 20; c2lChild.symbolId = "ColorsToList";
  SymbolChild ci; ci.id = 21; ci.symbolId = "Const"; ci.overrides["value"] = idx;
  SymbolChild pkChild; pkChild.id = 22; pkChild.symbolId = "PickColorFromList";
  root.children.push_back(c2lChild);
  root.children.push_back(ci);
  root.children.push_back(pkChild);

  // Wire each channel's 3 consts into the matching ColorsToList component MultiInput (wire-order = color
  // order A,B,C). Then ColorsToList.out → PickColorFromList.Input, Const(idx) → PickColorFromList.Index.
  if (wireColors) {
    for (int i = 0; i < 3; ++i) root.connections.push_back({1 + i, "out", 20, "Colors.x"});
    for (int i = 0; i < 3; ++i) root.connections.push_back({4 + i, "out", 20, "Colors.y"});
    for (int i = 0; i < 3; ++i) root.connections.push_back({7 + i, "out", 20, "Colors.z"});
    for (int i = 0; i < 3; ++i) root.connections.push_back({10 + i, "out", 20, "Colors.w"});
    root.connections.push_back({20, "out", 22, "Input"});
  }
  root.connections.push_back({21, "out", 22, "Index"});
  const char* outP[4] = {"Selected.x", "Selected.y", "Selected.z", "Selected.w"};
  for (int c = 0; c < 4; ++c) root.connections.push_back({22, outP[c], kSymbolBoundary, outP[c]});

  lib.symbols[root.id] = root; lib.rootId = "Root";

  ResidentEvalGraph g = buildEvalGraph(lib, "Root");
  ResidentEvalCtx ctx;
  std::map<std::string, std::vector<simd::float4>> clState;
  cookColorListNodes(g, ctx, clState);  // producer first (cook_host_values producer-before-consumer rule)
  cookColorPickNodes(g, ctx);           // then the consumer host-emit

  // Find the PickColorFromList resident node (path "22") for the extOut readback.
  const ResidentNode* pkNode = g.node("22");
  PickResult r{0, 0, 0, 0, false};
  if (!pkNode) return r;

  // LEG 1 (extOut, direct): the Selected.* output ports sit at spec indices 2..5 (Input(0),Index(1)
  // ahead), so the cook wrote extOut[2..5]. Read those raw slots.
  const float ox = pkNode->extOut[2], oy = pkNode->extOut[3], oz = pkNode->extOut[4], ow = pkNode->extOut[5];

  // LEG 2 (evalResidentFloat, the !evaluate readback path the production graph uses) must AGREE.
  const float ex = evalResidentFloat(g, "22", "Selected.x", ctx);
  const float ey = evalResidentFloat(g, "22", "Selected.y", ctx);
  const float ez = evalResidentFloat(g, "22", "Selected.z", ctx);
  const float ew = evalResidentFloat(g, "22", "Selected.w", ctx);
  const float eps = 1e-5f;
  r.x = ex; r.y = ey; r.z = ez; r.w = ew;  // report the eval-readback values (the production read)
  r.evalAgrees = std::fabs(ex - ox) < eps && std::fabs(ey - oy) < eps &&
                 std::fabs(ez - oz) < eps && std::fabs(ew - ow) < eps;
  return r;
}

}  // namespace

int runPickColorFromListSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;
  pickColorInjectBug() = injectBug;  // arm the REAL cook-path tooth (raw % instead of floor-mod)

  auto check = [&](const char* label, float idx, bool wire, float wx, float wy, float wz, float ww) {
    PickResult r = pickFromList(idx, wire);
    bool pass = std::fabs(r.x - wx) < eps && std::fabs(r.y - wy) < eps &&
                std::fabs(r.z - wz) < eps && std::fabs(r.w - ww) < eps && r.evalAgrees;
    ok = ok && pass;
    std::printf("[selftest-pickcolorfromlist] %s got=(%.3f,%.3f,%.3f,%.3f) want=(%.0f,%.0f,%.0f,%.0f) "
                "evalAgrees=%d -> %s\n",
                label, r.x, r.y, r.z, r.w, wx, wy, wz, ww, r.evalAgrees ? 1 : 0, pass ? "PASS" : "FAIL");
  };

  // List = [A(1,0,0,1), B(0,1,0,1), C(0,0,1,1)].
  // Index=0 → A. Index=4 → Mod(4,3)=1 → B. Index=-1 → Mod(-1,3)=2 → C.
  // Expected values are FIXED (correct TiXL math) for both runs — under -bug the corrupted cook output
  // diverges from these on the negative-index leg → RED.
  check("Index=0 →A", 0.0f, true, 1, 0, 0, 1);
  check("Index=4 →Mod1→B", 4.0f, true, 0, 1, 0, 1);
  check("Index=-1 →Mod2→C", -1.0f, true, 0, 0, 1, 1);  // floor-mod tooth: -bug emits A → FAILS here

  // Empty-list leg (no ColorsToList wired) → (0,0,0,0). fork-pickcolorfromlist-empty-is-zero.
  check("empty list →0", 0.0f, false, 0, 0, 0, 0);

  pickColorInjectBug() = false;  // disarm (a golden may run several legs in one process)
  return ok ? 0 : 1;
}

// Golden registrar: DIRECT push into the live-consumed value-op selftest sink (no shared-file edit —
// selftests.cpp iterates valueOpSelfTests() for --selftest-<name> / -bug). Mirror of
// resident_value_output_cook.cpp:152 (RequestedResolutionGoldenRegistrar). Token "pickcolorfromlist".
namespace {
struct PickColorFromListGoldenRegistrar {
  PickColorFromListGoldenRegistrar() {
    valueOpSelfTests().push_back({"pickcolorfromlist", runPickColorFromListSelfTest});
  }
};
static const PickColorFromListGoldenRegistrar _reg_pickcolorfromlist_golden;
}  // namespace

}  // namespace sw
