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
// SCOPE — FloatList host-scalar ops (FloatListLength / PickFloatFromList) PLUS the StringLength leg
//   (now LIVE — the resident string-wire rail it waited on landed, task_32b5b6e5). HISTORY: this pass
//   originally SKIPPED StringLength because its String input (FloatToString.Output →
//   StringLength.InputString) is a STRING WIRE that the resident flatten DROPPED — so the resident
//   graph could not follow it, and cooking it would have read only StringLength's strDef constant
//   (the very self-deception this family exists to kill). That gate is now closed: the flatten projects
//   a ResidentInput onto every String slot (Connection when wired) and cookResidentString
//   (resident_string_cook.cpp) walks it. So StringLength's String input is gathered HERE inline via
//   cookResidentString — String-in → .size() → Float-out on extOut[0], mirror of the flat
//   cookStringLength branch (point_graph.cpp). The generic skip-on-String-input guard remains as
//   belt-and-suspenders for any FUTURE String-consuming host-scalar op with a registered cook fn whose
//   resident gather is not yet wired — StringLength is handled by its dedicated branch BEFORE the guard.
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
#include "runtime/string_op_registry.h"       // stringInjectBug (StringLength resident leg, the teeth)

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

void cookHostScalarNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    // StringLength resident leg (String-in → host scalar → extOut[0]). StringLength registers ONLY its
    // type name into the host-scalar set (its NodeSpec + stub cook live on the String rail), so it has
    // NO HostScalarCookFn — the generic loop below would skip it. It is the resident twin of the flat
    // cookStringLength branch (point_graph.cpp): gather its ONE String input via cookResidentString
    // (now wireable — the resident string-wire rail), take .size(), write the count onto extOut[0] (the
    // channel evalResidentFloat reads for a downstream Float input wired to StringLength.Length). When
    // the upstream String wire is corrupted by stringInjectBug, the cooked upstream string is already
    // shorter (FloatToString drops its last char) → the count is wrong → RED carries through; we ALSO
    // clear it under injectBug to mirror the flat cookStringLength's direct host-scalar tooth.
    if (rn.opType == "StringLength") {
      const NodeSpec* s = findSpec(rn.opType);
      if (!s) continue;
      std::string in;
      for (const PortSpec& port : s->ports) {
        if (port.isInput && port.dataType == "String") {
          const ResidentInput* ri = rn.input(port.id);
          if (ri && ri->driver == ResidentInput::Driver::Connection) {
            cookResidentString(g, ri->srcNodePath, ctx, in, 0);  // WIRED upstream string
          } else {
            auto it = rn.strInputs.find(port.id);                 // UNWIRED → strDef const
            in = (it != rn.strInputs.end()) ? it->second : std::string{};
          }
          break;  // StringLength has exactly one String input ("InputString")
        }
      }
      float len = (float)in.size();
      if (stringInjectBug()) len = 0.0f;  // golden teeth (mirror flat cookStringLength's clear)
      rn.extOut[0] = len;  // Length output port index 0
      continue;
    }
    // IndexOf resident leg (TWO String inputs → host scalar → extOut[0]). IndexOf uses the full
    // HostScalarOp registry (unlike StringLength which has a legacy dedicated driver branch), so
    // findHostScalarOp("IndexOf") returns a real cook fn. However the generic loop below would skip
    // it via the String-input guard. This dedicated branch handles it FIRST:
    //   • Port 1 "OriginalString" → gathers via cookResidentString (wired) or strInputs (const)
    //   • Port 2 "SearchPattern"  → same
    // Computes first-occurrence index (C++ find → -1 on npos), writes onto extOut[0].
    // Teeth: hostScalarInjectBug() writes a sentinel; the golden LEG 25 red case fires on the
    // actual cook path (NOT by flipping expected values — mirror of StringLength / cookIndexOf flat).
    if (rn.opType == "IndexOf") {
      const NodeSpec* s = findSpec(rn.opType);
      if (!s) continue;
      // Gather the two String inputs in spec port order (port 1 = OriginalString, port 2 = SearchPattern).
      std::string strings[2];
      int strIdx = 0;
      for (const PortSpec& port : s->ports) {
        if (!port.isInput || port.dataType != "String") continue;
        if (strIdx >= 2) break;
        const ResidentInput* ri = rn.input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection) {
          cookResidentString(g, ri->srcNodePath, ctx, strings[strIdx], 0);
        } else {
          auto it = rn.strInputs.find(port.id);
          strings[strIdx] = (it != rn.strInputs.end()) ? it->second : std::string{};
        }
        ++strIdx;
      }
      const std::string& original = strings[0];
      const std::string& pattern  = strings[1];
      float idx;
      if (original.empty() || pattern.empty()) {
        idx = -1.0f;
      } else {
        auto pos = original.find(pattern);
        idx = (pos == std::string::npos) ? -1.0f : (float)(int)pos;
      }
      if (hostScalarInjectBug()) idx = -999.0f;  // golden teeth (mirror flat cookIndexOf)
      rn.extOut[0] = idx;  // Index output port index 0
      continue;
    }
    const HostScalarCookFn* fn = findHostScalarOp(rn.opType);
    if (!fn || !*fn) continue;  // not a host-scalar op (or StringLength, handled above).
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
