// runtime/resident_floatlist_cook — cookResidentFloatList: the PRODUCTION (resident-path) cook for the
// FLOATLIST currency (TiXL Slot<List<float>>). The resident twin of the flat cookFloatListNode
// (point_graph_hostvalue_cook.cpp) — the leg that actually LIVES in the running app. Extracted VERBATIM
// from resident_host_scalar_cook.cpp (which was at its 400-line cap; the COLORLIST→FLOATLIST BRIDGE
// gather needed room), a zero-behaviour-change move following the codebase's cook-extraction pattern
// (point_graph_*_cook.cpp). cookHostScalarNodes (the host-scalar per-frame pass) stays in the parent and
// calls cookResidentFloatList through its resident_value_cooks.h declaration (unchanged).
//
// WHAT IT DOES: walks a FloatList-producing resident node by PATH, gathering its inputs THROUGH the
// resident graph (following the Connection drivers the flatten step projects onto FloatList / ColorList /
// scalar-Float-MultiInput slots), runs the op's FloatListCookFn, and returns the cooked host list.
//   • a "FloatList" input port → follow each Connection driver (primary + extraConns, wire order) and
//     recurse into a gathered list per wire;
//   • a "ColorList" input port (COLORLIST→FLOATLIST BRIDGE, ColorListToInts.ColorLists) → recurse
//     cookResidentColorList per wire into inputColorLists;
//   • a scalar "Float" MultiInput port (FloatsToList.Input) → gather all wired scalar sources into ONE
//     list via evalResidentFloat, wire-declaration order.
//
// CROSS-FRAME STATE (STATEFUL ops — AmplifyValues): a process-lifetime static keyed by resident path
// (residentFloatListState) persists the FloatListState across frames, because the FloatList rail has NO
// single per-frame pass — it is pull-driven from several sites (ValuesToTexture, host-scalar,
// cookResidentString), so a process static reached internally is the only way to persist without
// threading a store through every pull point. A stateless op never creates an entry → no leak.
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h + the value-op
//   registries — all runtime).
#include "runtime/resident_eval_graph.h"

#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>  // simd::float4 (COLORLIST→FLOATLIST BRIDGE input)

#include "runtime/eval_context.h"           // EvaluationContext (FloatListCookCtx::ctx)
#include "runtime/floatlist_op_registry.h"  // FloatListCookFn / FloatListCookCtx / findFloatListOp
#include "runtime/graph.h"                   // NodeSpec / PortSpec / findSpec
#include "runtime/resident_value_cooks.h"   // cookResidentColorList (COLORLIST→FLOATLIST BRIDGE)

namespace sw {

namespace {

constexpr int kResidentFloatListDepthCap = 64;  // same cycle guard as evalResidentFloat / cookResident

// PRODUCTION-path cross-frame state for STATEFUL FloatList ops (AmplifyValues), keyed by resident path.
// Process-lifetime (a function-local static below), the FloatList twin of cook_host_values.cpp's
// s_colorListState / s_stringState — it lives HERE (not threaded) because the FloatList rail has NO
// single per-frame pass (see file header). A stateless op never creates an entry → no leak.
struct ResidentFloatListSlot {
  FloatListState state;
  uint32_t lastCookedFrame = 0xFFFFFFFFu;  // frameIndex of the last ADVANCE (cook-once-per-frame guard)
  bool everCooked = false;                 // distinguishes "never advanced" from "advanced on frame 0"
};

std::map<std::string, ResidentFloatListSlot>& residentFloatListState() {
  static std::map<std::string, ResidentFloatListSlot> s;  // process-lifetime; mirror of s_colorListState
  return s;
}

}  // namespace

// Test-only reset of the production FloatList state store (a golden runs multiple independent trajectories
// in one process; without this the previous trajectory's accumulated state would leak into the next). The
// flat path resets naturally (a fresh PointGraph per case); this clears the resident process static. No
// production caller.
void resetResidentFloatListState() { residentFloatListState().clear(); }

// Cook ONE upstream FloatList-producing resident node into `out` (host list), gathering its inputs
// THROUGH the resident graph. Mirror of the flat cookFloatListNode but walking ResidentInput drivers.
// Returns false if `path` is not a FloatList producer / unknown (caller treats as an empty list).
bool cookResidentFloatList(const ResidentEvalGraph& g, const std::string& path,
                           const ResidentEvalCtx& ctx, std::vector<float>& out, int depth,
                           FloatListState* state) {
  out.clear();
  if (depth > kResidentFloatListDepthCap) return false;
  const ResidentNode* n = g.node(path);
  if (!n) return false;
  const NodeSpec* s = findSpec(n->opType);
  if (!s) return false;
  const FloatListCookFn* fn = findFloatListOp(n->opType);
  if (!fn || !*fn) return false;

  // Gather inputs in spec port order (mirror cookFloatListNode's loop). Each entry is one upstream host
  // list (a FloatList source) or one aggregated list of scalar Float sources (a scalar Float MultiInput).
  std::vector<std::vector<float>> inputLists;
  std::vector<std::vector<simd::float4>> inputColorLists;  // COLORLIST→FLOATLIST BRIDGE (ColorListToInts)
  for (const PortSpec& port : s->ports) {
    if (!port.isInput) continue;
    const ResidentInput* ri = n->input(port.id);
    if (port.dataType == "FloatList") {
      // Follow the Connection driver(s). Primary first, then extraConns (wire-declaration order). A
      // Constant/absent driver on a FloatList slot = unwired → contributes no list (faithful to the
      // flat gather, where an unwired FloatList input yields no entry).
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::vector<float> up;
        cookResidentFloatList(g, ri->srcNodePath, ctx, up, depth + 1);
        inputLists.push_back(std::move(up));
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            std::vector<float> ue;
            cookResidentFloatList(g, ec.first, ctx, ue, depth + 1);
            inputLists.push_back(std::move(ue));
          }
        }
      }
    } else if (port.dataType == "ColorList") {
      // COLORLIST→FLOATLIST BRIDGE (resident twin of the flat gather in point_graph_hostvalue_cook.cpp):
      // a wired ColorList producer rides off the resident ColorList rail into ColorListToInts.ColorLists.
      // Primary Connection first, then extraConns (wire order). Unwired/Constant slot → no entry
      // (faithful to GetCollectedTypedInputs: connected inputs only).
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::vector<simd::float4> up;
        cookResidentColorList(g, ri->srcNodePath, ctx, up, depth + 1);
        inputColorLists.push_back(std::move(up));
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            std::vector<simd::float4> ue;
            cookResidentColorList(g, ec.first, ctx, ue, depth + 1);
            inputColorLists.push_back(std::move(ue));
          }
        }
      }
    } else if (port.dataType == "Float" && port.multiInput) {
      // Aggregate all wired scalar Float sources into ONE list (FloatsToList consumes inputLists[0]).
      // Wire-declaration order: primary Connection then extraConns. An unwired / Constant-only port
      // contributes an empty list (faithful to GetCollectedTypedInputs: CONNECTED inputs only).
      std::vector<float> scalars;
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        scalars.push_back(evalResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx));
        for (const auto& ec : ri->extraConns)
          scalars.push_back(evalResidentFloat(g, ec.first, ec.second, ctx));
      }
      inputLists.push_back(std::move(scalars));
    }
    // (Single scalar Float inputs / other dataTypes are read via resolved params, not gathered.)
  }

  // Build a 16-byte EvaluationContext from the resident ctx so a FloatList producer that reads LocalFxTime
  // (the bars clock) — AnimFloatList — sees the SAME time the flat path hands it (fc.ctx = &ctx). The pure
  // producers (FloatsToList/IntsToList/ColorListToInts) ignore it; this only populates it for time readers.
  EvaluationContext ec{};
  ec.frameIndex  = ctx.frameIndex;
  ec.time        = ctx.localFxTime;  // (existing readers touch .time; AnimFloatList reads .localFxTime)
  ec.deltaTime   = 0.0f;
  ec.localFxTime = ctx.localFxTime;  // BARS — TiXL EvaluationContext.LocalFxTime
  // Resolve THIS node's Float params inline (the memo-free twin of cookResident's nodeParams). AnimFloatList
  // reads Phase/Rate/Ratio/...; ColorListToInts reads OutputMode; the pure producers read none.
  std::map<std::string, float> params = resolveResidentFloatInputs(g, *n, ctx);

  // CROSS-FRAME STATE + cook-once guard (only for a STATEFUL op — AmplifyValues). A stateless op leaves
  // fc.state null and re-cooks freely (byte-identical). For a stateful op: resolve its state slot (an
  // explicit `state` from a golden, else the process static keyed by path), then guard the ADVANCE to
  // ONCE per frameIndex — a later pull this frame re-publishes the settled output WITHOUT re-advancing.
  const bool stateful = floatListOpIsStateful(n->opType);
  FloatListState* st = nullptr;
  ResidentFloatListSlot* slot = nullptr;
  if (stateful) {
    if (state) {
      st = state;  // golden-supplied slot (deterministic, no static); no cook-once guard needed (1 pull)
    } else {
      slot = &residentFloatListState()[path];  // process static; operator[] default-creates
      st = &slot->state;
      // Already advanced this frame? Re-publish the settled output, do NOT re-run the op (no double damp).
      if (slot->everCooked && slot->lastCookedFrame == ctx.frameIndex) {
        out = slot->state.output;
        return true;
      }
    }
  }

  FloatListCookCtx fc;
  fc.dev = nullptr; fc.lib = nullptr; fc.queue = nullptr;  // host-only ops ignore these
  fc.ctx = &ec;          // LocalFxTime-bearing (AnimFloatList's bars clock)
  fc.nodeId = 0;
  fc.inputLists = &inputLists;
  fc.inputColorLists = &inputColorLists;  // COLORLIST→FLOATLIST BRIDGE; empty for every non-ColorListToInts op
  fc.output = &out;
  fc.params = &params;   // resolved Float params (AnimFloatList's shape/rate/..., ColorListToInts.OutputMode)
  fc.state = st;         // cross-frame slot for a stateful op (AmplifyValues); null for a stateless one
  (*fn)(fc);
  if (slot) { slot->lastCookedFrame = ctx.frameIndex; slot->everCooked = true; }  // mark advanced this frame
  return true;
}

}  // namespace sw
