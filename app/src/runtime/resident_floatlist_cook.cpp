// runtime/resident_floatlist_cook — cookResidentFloatList + the production-path FloatList cross-frame
// state store. Split out of resident_host_scalar_cook.cpp (which crossed the 400-line cap when the
// dict-currency Dict gather landed in cookHostScalarNodes). ZERO behaviour change: this is a verbatim
// move of the cookResidentFloatList function + its ResidentFloatListSlot state struct / process static /
// resetResidentFloatListState, all previously living in resident_host_scalar_cook.cpp. Both TUs are
// declared in resident_value_cooks.h / resident_eval_graph.h so every caller is unaffected.
//
// This cooks ONE upstream FloatList-producing resident node (FloatsToList / AmplifyValues / ...) into a
// host list by walking the resident Connection drivers — the resident twin of the flat cookFloatListNode.
// It is pull-driven from several sites (ValuesToTexture, host-scalar consumers, cookResidentString, the
// list-currency buffer/gradient bridge), which is why its cross-frame state lives in a process static
// reached internally rather than threaded through every call site.
//
// PLACEMENT: runtime leaf (pure CPU; resident_eval_graph.h + graph.h + the floatlist registry).
#include "runtime/resident_eval_graph.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "runtime/eval_context.h"           // EvaluationContext (the 16-byte ctx lift for AnimFloatList)
#include "runtime/floatlist_op_registry.h"  // FloatListCookFn / FloatListCookCtx / FloatListState / findFloatListOp
#include "runtime/graph.h"                   // NodeSpec / PortSpec / findSpec

namespace sw {

namespace {

constexpr int kResidentFloatListDepthCap = 64;  // same cycle guard as evalResidentFloat / cookResident

// PRODUCTION-path cross-frame state for STATEFUL FloatList ops (AmplifyValues), keyed by resident path.
// Process-lifetime (a function-local static below), the FloatList twin of cook_host_values.cpp's
// s_colorListState / s_stringState — BUT it lives HERE (not threaded from cook_host_values) because the
// FloatList rail has NO single per-frame pass: it is pull-driven from several sites (ValuesToTexture,
// host-scalar, cookResidentString). A process static reached internally is the only way to persist state
// across frames from any of those pull points without threading a store through every call site. A
// stateless op never creates an entry → no leak for a graph without a stateful floatlist op.
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
// THROUGH the resident graph. Mirror of the flat cookFloatListNode (point_graph.cpp:633) but walking
// ResidentInput drivers instead of flat Graph connections:
//   • a "FloatList" input port → follow each Connection driver (primary + extraConns, wire order) and
//     recurse this same fn into a gathered list per wire;
//   • a scalar "Float" MultiInput port (FloatsToList.Input) → gather all wired scalar sources into ONE
//     list via evalResidentFloat, in wire-declaration order (primary then extraConns).
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
    } else if (port.dataType == "Float" && port.multiInput) {
      // Aggregate all wired scalar Float sources into ONE list (FloatsToList consumes inputLists[0]).
      // Wire-declaration order: primary Connection then extraConns. An unwired / Constant-only port
      // contributes an empty list (FloatsToList → empty output, faithful to GetCollectedTypedInputs:
      // it collects CONNECTED inputs only, so a constant value on the slot is NOT a collected scalar).
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

  // Build a 16-byte EvaluationContext from the resident ctx so a FloatList producer that reads
  // LocalFxTime (the bars clock) — AnimFloatList — sees the SAME time the flat path hands it
  // (point_graph_hostvalue_cook.cpp:116 fc.ctx = &ctx). Mirror of resident_eval_graph.cpp:185-192's
  // ResidentEvalCtx→EvaluationContext lift. Held local so fc.ctx stays valid through (*fn)(fc).
  // The pure producers (FloatsToList/IntsToList) ignore ctx; this only POPULATES it for the time-
  // reading ones. The struct stays 16 bytes; no transport/cook-core spine touched.
  EvaluationContext ec{};
  ec.frameIndex  = ctx.frameIndex;
  ec.time        = ctx.localFxTime;  // (existing readers touch .time; AnimFloatList reads .localFxTime)
  ec.deltaTime   = 0.0f;
  ec.localFxTime = ctx.localFxTime;  // BARS — TiXL EvaluationContext.LocalFxTime
  // Resolve THIS node's Float params inline (the memo-free twin of cookResident's nodeParams; same
  // pure resolver the host-scalar/mesh resident cooks use). Held local so fc.params stays valid.
  // FloatsToList/IntsToList read none (the map is harmlessly unused for them); AnimFloatList reads
  // Phase/Rate/Ratio/Amplitude/Offset/Bias/Shape/OffsetNumber/OffsetCycle through it.
  std::map<std::string, float> params = resolveResidentFloatInputs(g, *n, ctx);

  // CROSS-FRAME STATE + cook-once guard (only for a STATEFUL op — AmplifyValues). A stateless op leaves
  // fc.state null and re-cooks freely (byte-identical). For a stateful op: resolve its state slot (an
  // explicit `state` from a golden, else the process-lifetime static keyed by resident path), then guard
  // the ADVANCE to ONCE per frameIndex — a later pull this frame (fan-out: ValuesToTexture + host-scalar)
  // re-publishes the already-settled state->output WITHOUT advancing the damp again (mirror of the flat
  // floatListCooked memo / the colorlist resident state=nullptr-on-recursion split).
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
  fc.dev = nullptr; fc.lib = nullptr; fc.queue = nullptr;  // host-only ops (FloatsToList) ignore these
  fc.ctx = &ec;          // LocalFxTime-bearing (AnimFloatList's bars clock); was nullptr (no time reader)
  fc.nodeId = 0;
  fc.inputLists = &inputLists;
  fc.output = &out;
  fc.params = &params;   // resolved Float params (AnimFloatList's shape/rate/...); was nullptr
  fc.state = st;         // cross-frame slot for a stateful op (AmplifyValues); null for a stateless one
  (*fn)(fc);
  if (slot) { slot->lastCookedFrame = ctx.frameIndex; slot->everCooked = true; }  // mark advanced this frame
  return true;
}

}  // namespace sw
