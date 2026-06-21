// runtime/resident_colorlist_cook — cookColorListNodes: the PRODUCTION (resident-path) cook for the
// COLORLIST currency (vec4-list cook flow = TiXL Slot<List<Vector4>>). The resident twin of the flat
// cookColorListNode branch (point_graph.cpp) — and the leg that actually LIVES in the running app.
//
// WHY THIS FILE EXISTS (the R-2 self-deception it repairs — see resident_host_scalar_cook.cpp:1-12 for
// the canonical statement of the trap):
//   The flat cookColorListNode only runs when a colorlist op is the TERMINAL of a flat cook() (the only
//   flat caller is a golden). Production renders via PointGraph::cookResident + the resident eval graph
//   (frame_cook.cpp) — and without this file NOTHING on the resident path cooks colorlist nodes. A
//   colorlist golden that proved ONLY the flat rail (zero production callers) would be fake-green: the
//   FloatList rail originally fell into exactly that trap (resident_host_scalar_cook.cpp). So this file
//   adds the missing per-frame pass: it walks the resident graph, cooks every colorlist node (ColorsToList)
//   by gathering its inputs THROUGH THE RESIDENT GRAPH (following the Connection drivers the flatten step
//   projects onto each Float component slot — a Float MultiInput slot becomes ResidentInput primary +
//   extraConns, verified resident_eval_flatten.cpp:256-268), runs the op's ColorListCookFn, and writes
//   the host color list into ResidentNode::extColorOut[outputPortIndex] (the vec4 channel — NOT packed
//   into the scalar extOut[] slots). The colorlist golden drives THIS pass (buildEvalGraph →
//   cookColorListNodes → read extColorOut), proving the PRODUCTION resident path, not flat-only.
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h + the colorlist
//   registry — all runtime). Called from app/frame_cook.cpp once per frame, same slot as
//   cookHostScalarNodes / cookAudioReactionNodes.
#include "runtime/resident_eval_graph.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include "runtime/colorlist_op_registry.h"  // ColorListCookFn / ColorListCookCtx / findColorListOp
#include "runtime/eval_context.h"           // EvaluationContext (ColorListCookCtx::ctx)
#include "runtime/graph.h"                   // NodeSpec / PortSpec / findSpec

namespace sw {

namespace {

constexpr int kResidentColorListDepthCap = 64;  // same cycle guard as evalResidentFloat / cookResident

}  // namespace

// Cook ONE upstream ColorList-producing resident node into `out` (host color list), gathering its inputs
// THROUGH the resident graph. Mirror of the flat cookColorListNode but walking ResidentInput drivers
// instead of flat Graph connections:
//   • a "ColorList" input port → follow each Connection driver (primary + extraConns, wire order) and
//     recurse this same fn into a gathered list per wire (a future combiner; no consumer ships here);
//   • the 4 PARALLEL scalar "Float" MultiInput component ports (Colors.x/.y/.z/.w) → gather each port's
//     wired scalars via evalResidentFloat into its channel (primary then extraConns), then ColorsToList
//     zips index i across the 4 channels into one float4.
// Returns false if `path` is not a ColorList producer / unknown (caller treats as an empty list).
bool cookResidentColorList(const ResidentEvalGraph& g, const std::string& path,
                           const ResidentEvalCtx& ctx, std::vector<simd::float4>& out, int depth,
                           std::vector<simd::float4>* state) {
  out.clear();
  if (depth > kResidentColorListDepthCap) return false;
  const ResidentNode* n = g.node(path);
  if (!n) return false;
  const NodeSpec* s = findSpec(n->opType);
  if (!s) return false;
  const ColorListCookFn* fn = findColorListOp(n->opType);
  if (!fn || !*fn) return false;

  std::vector<std::vector<simd::float4>> inputLists;  // upstream ColorList sources (combiner future)
  std::array<std::vector<float>, 4> colorScalars;     // the 4 parallel vec4-MultiInput component channels
  int compChannel = 0;
  for (const PortSpec& port : s->ports) {
    if (!port.isInput) continue;
    const ResidentInput* ri = n->input(port.id);
    if (port.dataType == "ColorList") {
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::vector<simd::float4> up;
        cookResidentColorList(g, ri->srcNodePath, ctx, up, depth + 1, /*state=*/nullptr);
        inputLists.push_back(std::move(up));
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            std::vector<simd::float4> ue;
            cookResidentColorList(g, ec.first, ctx, ue, depth + 1, /*state=*/nullptr);
            inputLists.push_back(std::move(ue));
          }
        }
      }
    } else if (port.dataType == "Float" && port.multiInput && compChannel < 4) {
      // One component channel (Colors.x then .y then .z then .w). Wire-declaration order: primary
      // Connection then extraConns. An unwired / Constant-only port contributes an empty channel
      // (faithful to GetCollectedTypedInputs — a constant on the slot is NOT a collected scalar).
      std::vector<float>& chan = colorScalars[compChannel++];
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        chan.push_back(evalResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx));
        for (const auto& ec : ri->extraConns)
          chan.push_back(evalResidentFloat(g, ec.first, ec.second, ctx));
      }
    }
  }

  // RESOLVED Float params of THIS node — the SAME value spine the flat path's nodeParams supplies, mirror
  // of resident_host_scalar_cook.cpp:166. ColorsToList reads none (empty map → byte-identical); KeepColors
  // reads MaxLength/AddColorToList/Reset/Color.x..w here. Resolved unconditionally (cheap; one map walk).
  std::map<std::string, float> params = resolveResidentFloatInputs(g, *n, ctx);

  ColorListCookCtx cc;
  cc.dev = nullptr; cc.lib = nullptr; cc.queue = nullptr;  // host-only ops (ColorsToList) ignore these
  cc.ctx = nullptr;
  cc.nodeId = 0;
  cc.inputLists = &inputLists;
  cc.inputColorScalars = &colorScalars;
  cc.output = &out;
  cc.params = &params;  // ColorsToList ignores it (no Float params); KeepColors reads its scalars from it
  // Per-node CROSS-FRAME accumulator (KeepColors's `_list`): the caller (cookColorListNodes) supplies the
  // resident-path-keyed slot from s_colorListState (mirror of s_svState). null for a stateless op / a
  // single-frame golden path → KeepColors then sees an empty accumulator (still faithful for frame 0).
  cc.state = state;
  (*fn)(cc);
  return true;
}

void cookColorListNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                        std::map<std::string, std::vector<simd::float4>>& state) {
  for (ResidentNode& rn : g.nodes) {
    const ColorListCookFn* fn = findColorListOp(rn.opType);
    if (!fn || !*fn) continue;  // not a colorlist op
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // Cook the host color list through the resident graph (cookResidentColorList gathers the resident
    // Connection drivers — the SAME walk the flat cookColorListNode does over flat connections). The
    // per-node CROSS-FRAME accumulator slot (KeepColors's `_list`) is keyed by resident path in `state`
    // (the s_colorListState static frame_cook.cpp threads — mirror of s_svState); operator[] default-
    // creates the empty list on first cook (= TiXL's `_list = []` field default, KeepColors.cs:46). A
    // stateless op ignores its slot, so the entry stays empty (no behaviour change, no cross-frame leak).
    std::vector<simd::float4> list;
    cookResidentColorList(g, rn.path, ctx, list, 0, &state[rn.path]);

    // Write onto the colorlist op's MAIN ColorList output port. Find its port index (the channel the
    // resident eval / a downstream consumer reads). ColorsToList has exactly one "ColorList" output.
    int outPortIdx = -1;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      if (!s->ports[i].isInput && s->ports[i].dataType == "ColorList") { outPortIdx = (int)i; break; }
    }
    if (outPortIdx < 0) continue;  // no ColorList output (defensive; ColorsToList always has one)
    rn.extColorOut[outPortIdx] = std::move(list);
  }
}

}  // namespace sw
