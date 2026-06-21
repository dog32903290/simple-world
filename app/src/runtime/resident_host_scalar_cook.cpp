// runtime/resident_host_scalar_cook — cookHostScalarNodes: the PRODUCTION (resident-path) cook for the
// FloatList→Float BRIDGE (list-routing seam). This is the resident twin of the flat cookHostScalar
// branch (point_graph.cpp:791) — and it is the leg that actually LIVES in the running app.
//
// WHY THIS FILE EXISTS (the refuter-found self-deception it repairs):
//   The flat cookHostScalar only runs when the host-scalar op is the TERMINAL of a flat cook() (the
//   only flat caller is a golden). Production renders via PointGraph::cookResident + evalResidentFloat
//   (frame_cook.cpp) — and NOTHING on the resident path cooked host-scalar nodes. So a real graph
//   FloatsToList→FloatListLength→Multiply evaluated 0 in the running app (evalResidentFloat reads
//   ResidentNode::extOut for any !evaluate node, but extOut was never written for host-scalar nodes —
//   only AudioReaction/stateful ops had a per-frame extOut writer). The golden proved the FLAT rail,
//   which has zero production callers — the bridge was mechanism-only, not actually wired.
//
//   This file adds the missing per-frame pass: it walks the resident graph, cooks every host-scalar
//   node (FloatListLength / PickFloatFromList) by gathering its upstream FloatList inputs THROUGH THE
//   RESIDENT GRAPH (following the Connection drivers the flatten step DOES project onto FloatList
//   slots — verified: a FloatList wire becomes ResidentInput{Driver::Connection} because FloatList
//   slots, unlike String slots, get a ResidentInput in resident_eval_flatten.cpp:95-126), runs the
//   op's HostScalarCookFn, and writes the scalar into ResidentNode::extOut[outputPortIndex] — the EXACT
//   channel evalResidentFloat already reads (resident_eval_graph.cpp:68-70). Mirror of how
//   cookAudioReactionNodes writes extOut[] each frame (frame_cook.cpp:166-168).
//
// SCOPE — FloatList host-scalar ops only. StringLength is in the host-scalar TYPE set (its Length rides
//   extOut on the flat path), but it is NOT cooked here, and intentionally so: its String input
//   (FloatToString.Output → StringLength.InputString) is a STRING WIRE, and the resident flatten DROPS
//   every String wire (resident_eval_flatten.cpp:100-103 — String slots carry no driver/wire, only a
//   resolved-constant strInputs). So the resident graph has no way to follow a String wire to cook
//   StringLength faithfully; doing so would read only StringLength's strDef constant, never the wired
//   upstream string — the very self-deception this fix exists to kill. StringLength's resident bridge
//   needs the resident STRING-wire rail built first (a separate seam; the flat string-rail b247602 is
//   itself flat-only). cookHostScalarNodes skips any host-scalar op that has a String input port, so
//   StringLength stays correctly at 0 on the resident path until that seam lands (no FAKE green).
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h + the two op
//   registries — all runtime). Called from app/frame_cook.cpp once per frame, same slot as
//   cookAudioReactionNodes / cookStatefulValueNodes (ARCHITECTURE: app owns the per-frame orchestration;
//   the compute body lives in runtime).
#include "runtime/resident_eval_graph.h"

#include <map>
#include <string>
#include <vector>

#include "runtime/eval_context.h"             // EvaluationContext (HostScalarCookCtx::ctx)
#include "runtime/floatlist_op_registry.h"    // FloatListCookFn / FloatListCookCtx / findFloatListOp
#include "runtime/graph.h"                     // NodeSpec / PortSpec / findSpec
#include "runtime/host_scalar_op_registry.h"  // HostScalarCookFn / HostScalarCookCtx / findHostScalarOp

namespace sw {

namespace {

constexpr int kResidentFloatListDepthCap = 64;  // same cycle guard as evalResidentFloat / cookResident

}  // namespace

// Cook ONE upstream FloatList-producing resident node into `out` (host list), gathering its inputs
// THROUGH the resident graph. Mirror of the flat cookFloatListNode (point_graph.cpp:633) but walking
// ResidentInput drivers instead of flat Graph connections:
//   • a "FloatList" input port → follow each Connection driver (primary + extraConns, wire order) and
//     recurse this same fn into a gathered list per wire;
//   • a scalar "Float" MultiInput port (FloatsToList.Input) → gather all wired scalar sources into ONE
//     list via evalResidentFloat, in wire-declaration order (primary then extraConns).
// Returns false if `path` is not a FloatList producer / unknown (caller treats as an empty list).
bool cookResidentFloatList(const ResidentEvalGraph& g, const std::string& path,
                           const ResidentEvalCtx& ctx, std::vector<float>& out, int depth) {
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

  FloatListCookCtx fc;
  fc.dev = nullptr; fc.lib = nullptr; fc.queue = nullptr;  // host-only ops (FloatsToList) ignore these
  fc.ctx = nullptr;
  fc.nodeId = 0;
  fc.inputLists = &inputLists;
  fc.output = &out;
  fc.params = nullptr;  // FloatsToList reads no Float params; a future param-driven list op needs a map
  (*fn)(fc);
  return true;
}

void cookHostScalarNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    const HostScalarCookFn* fn = findHostScalarOp(rn.opType);
    if (!fn || !*fn) continue;  // StringLength is in the type set but has NO cook fn here (String rail).
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // SKIP host-scalar ops with a String input — the resident graph drops String wires (flatten:100-103),
    // so we cannot faithfully gather them. (Today only StringLength; the findHostScalarOp(StringLength)
    // already returns null since StringLength registers no cook fn — this guard is belt-and-suspenders
    // for any FUTURE String-consuming host-scalar op that DOES register a cook fn before the string-wire
    // rail exists. extOut stays 0 → evalResidentFloat reads 0, same as the flat-rail-only behaviour.)
    bool hasStringInput = false;
    for (const PortSpec& port : s->ports)
      if (port.isInput && port.dataType == "String") { hasStringInput = true; break; }
    if (hasStringInput) continue;

    // Gather FloatList inputs by following the resident Connection drivers (cookResidentFloatList),
    // in spec port order, mirroring the flat cookHostScalar's FloatList gather. An unwired FloatList
    // input contributes NO entry → empty → count/pick 0, matching TiXL null→0.
    std::vector<std::vector<float>> inputLists;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "FloatList")) continue;
      const ResidentInput* ri = rn.input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::vector<float> up;
        cookResidentFloatList(g, ri->srcNodePath, ctx, up, 0);
        inputLists.push_back(std::move(up));
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            std::vector<float> ue;
            cookResidentFloatList(g, ec.first, ctx, ue, 0);
            inputLists.push_back(std::move(ue));
          }
        }
      }
      // (An unwired / Constant FloatList input contributes nothing → empty inputLists → 0.)
    }

    // Resolved Float params of THIS node (PickFloatFromList.Index rides this) — the SAME value spine
    // the flat path uses (resolveResidentFloatInputs, mirror of flat nodeParams).
    std::map<std::string, float> params = resolveResidentFloatInputs(g, rn, ctx);

    // No String inputs on this op (guarded above), so an empty inputStrings is faithful.
    std::vector<std::string> inputStrings;

    float scalar = 0.0f;
    EvaluationContext gpuCtx{};
    gpuCtx.frameIndex = ctx.frameIndex;
    gpuCtx.time = ctx.localFxTime;  // wall clock (host-scalar ops are time-independent today; symmetry)
    gpuCtx.deltaTime = 0.0f;
    HostScalarCookCtx hc;
    hc.dev = nullptr; hc.lib = nullptr; hc.queue = nullptr;
    hc.ctx = &gpuCtx;
    hc.nodeId = 0;
    hc.inputLists = &inputLists;
    hc.inputStrings = &inputStrings;
    hc.params = &params;
    hc.output = &scalar;
    (*fn)(hc);  // computes the scalar; hostScalarInjectBug() (golden teeth) corrupts it IN the cook

    // Write the scalar onto the resident node's MAIN (and only) Float output = port index 0 (the
    // host-scalar layout: output port FIRST, FloatListLength.Length / PickFloatFromList.Selected). The
    // host-scalar ops emit exactly ONE Float output, so extOut[0] is the whole result.
    rn.extOut[0] = scalar;
  }
}

}  // namespace sw
