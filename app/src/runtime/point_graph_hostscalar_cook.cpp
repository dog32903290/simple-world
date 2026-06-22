// runtime/point_graph_hostscalar_cook — the FLAT HOST-SCALAR cooks: the value flows where a node CONSUMES
// a host currency (a FloatList and/or a String) and PRODUCES a single host FLOAT — the FloatList→Float /
// String→Float BRIDGE (list-routing seam, SEAM_COMPLETION_PLAN §2 stage 1). cookFlatStringLength
// (StringLength: String → host scalar) + cookFlatHostScalar (the generalised FloatListLength /
// PickFloatFromList / … host-scalar registry consumers). Neither is a String/FloatList PRODUCER — each
// computes ONE float and routes it BOTH into Impl::floatListBuf (1-element host list, the legacy transport
// for debugCookedFloatList readback) AND Node::outCache[0] (the BRIDGE channel evalFloat reads via its
// generalised stateful escape hatch — fork-floatlist-scalar-via-outcache).
//
// Extracted VERBATIM from the cookStringLength / cookHostScalar lambdas that lived inside PointGraph::cook
// (point_graph.cpp) — a zero-behaviour-change move that buys ratchet headroom (point_graph.cpp was at its
// line cap) following the Cut-6 / Cut-4 extraction pattern (point_graph_mesh_cook.cpp /
// point_graph_hostvalue_cook.cpp / point_graph_string_cook.cpp).
//
// THE MECHANISM = thin-lambda → Impl-method delegation (NOT a context struct). Each body moves to a
// PointGraph::Impl method (so it can reach floatListBuf + the gatherStringInputs Impl method). cook()'s
// original lambdas (cookStringLength / cookHostScalar were cook-LOCAL `auto` lambdas — not std::function
// slots, so no closure web recurses them; only the terminal dispatch calls them once) stay AS thin
// forwarding lambdas. Each method takes the minimal shared cook-stack state by reference: the const Graph&
// g, the const EvaluationContext& ctx, and the nodeParams memo (NodeParamsFn&).
//
// THE COUPLING (the slots passed in by-ref, same contract as the String leaf):
//   • gatherStringInputs — the SHARED String-input gather (wire-OR-const), a PointGraph::Impl method
//     defined in point_graph_string_cook.cpp. BOTH cooks here call THE SAME method (NOT a private copy —
//     a second copy would risk a behaviour fork in the parity-load-bearing wire-OR-const gather). It needs
//     the cookStringNode PRODUCER slot, so that slot rides in by-ref and is forwarded to gatherStringInputs.
//   • cookFloatListNode — the FloatList currency slot (Cut 4 host-value). cookFlatHostScalar gathers it for
//     a "FloatList" input port (FloatListLength.List / PickFloatFromList.List).
// The const_cast<Graph&> outCache write is the AudioReaction "external cooker writes each frame" channel
// (transient, not serialized — R-1 resolution (a)); after the move the const_cast still acts on g and the
// Node symbol is visible (graph.h).
//
// PLACEMENT: runtime leaf (depends only on the flat Graph + the host-scalar registry + PointGraph::Impl —
//   all runtime). Defined as methods on PointGraph::Impl; cook() wraps them in forwarding lambdas.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl (floatListBuf / gatherStringInputs) + decls

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"  // Graph/Node/NodeSpec/PortSpec/Connection/pinId/pinNode/findSpec
#include "runtime/host_scalar_op_registry.h"  // HostScalarCookCtx/HostScalarCookFn/findHostScalarOp
#include "runtime/string_op_registry.h"       // stringInjectBug (cookFlatStringLength's RED tooth)
#include "runtime/tixl_point.h"               // EvaluationContext (HostScalarCookCtx::ctx)

namespace sw {

using pgdetail::flatKey;

// StringLength: the FIRST cross-rail consumer (String input → host scalar output). TiXL's Length is a
// Slot<int> (StringLength.cs:16 Length.Value = InputString.GetValue(context).Length); sw dissolves int→
// Float (fork-int-bool-dissolve-to-float, Cut32 convention). It is NOT a string PRODUCER, so it has no
// StringCookFn — it is cooked by THIS method on the flat path, resolving its String input via the shared
// gather, and stores the length as a 1-element host FloatList (Impl::floatListBuf — readback via
// debugCookedFloatList) PLUS Node::outCache[0] (the bridge channel evalFloat reads). (cookStringNode rides
// in by-ref → forwarded to gatherStringInputs for the wired-String recursion.)
void PointGraph::Impl::cookFlatStringLength(
    const Graph& g, int id, const std::function<const std::string*(int)>& cookStringNode) {
  const Node* n = g.node(id);
  const NodeSpec* s = n ? findSpec(n->type) : nullptr;
  if (!s) return;
  std::vector<std::string> inputStrings = gatherStringInputs(g, id, *s, cookStringNode);
  // StringLength has exactly one String input ("InputString") → inputStrings[0].
  size_t len = inputStrings.empty() ? 0 : inputStrings[0].size();
  std::vector<float>& out = floatListBuf[flatKey(id)];
  out.assign(1, (float)len);  // int→Float host scalar
  // Test-only: corrupt the REAL output (drop the host scalar) so the golden's input-drivable RED
  // bites on the actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !out.empty()) out.clear();
  // BRIDGE (list-routing seam, fork-floatlist-scalar-via-outcache): also mirror the host scalar into
  // Node::outCache[0] so a downstream Float INPUT port wired to StringLength.Length reads it via
  // evalFloat's generalised stateful escape hatch (graph.cpp). floatListBuf above stays the legacy
  // transport (debugCookedFloatList readback); outCache is the new channel evalFloat can reach.
  // const_cast: cook takes `const Graph&` but Node::outCache is the AudioReaction "external cooker
  // writes each frame" channel (transient, not serialized) — same precedent (R-1 resolution (a)).
  // The string-rail RED (stringInjectBug clears `out`) carries through: outCache reads the cleared
  // value (0 elements → write 0 vs the real len) so the downstream evalFloat RED still bites.
  if (Node* mn = const_cast<Graph&>(g).node(id))
    mn->outCache[0] = out.empty() ? 0.0f : out[0];
}

// Cook a HOST-SCALAR consumer node (FloatList/String input → ONE host Float): the FloatList→Float BRIDGE
// (list-routing seam, SEAM_COMPLETION_PLAN §2 stage 1). GENERALISES cookFlatStringLength to ANY op
// registered in the host-scalar registry (FloatListLength / PickFloatFromList / …). It gathers the node's
// inputs by spec port dataType — each "FloatList" port via cookFloatListNode (MultiInput → one gathered
// list per wire, wire-declaration order), each "String" port via gatherStringInputs (wire-OR-const) — runs
// the op to compute the scalar, then stores it BOTH in floatListBuf (1-elem, the transport:
// debugCookedFloatList readback) AND in Node::outCache[0] (the BRIDGE: evalFloat reads it). The op's Float
// params are resolved through the value spine (nodeParams), so e.g. PickFloatFromList.Index is drivable.
// Mirrors cookFlatStringLength's const_cast outCache write (R-1). (cookFloatListNode + cookStringNode ride
// in by-ref — the FloatList gather slot + the String-producer slot gatherStringInputs forwards.)
void PointGraph::Impl::cookFlatHostScalar(
    const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
    const std::function<const std::vector<float>*(int)>& cookFloatListNode,
    const std::function<const std::string*(int)>& cookStringNode, int id) {
  const Node* n = g.node(id);
  const NodeSpec* s = n ? findSpec(n->type) : nullptr;
  if (!s) return;
  const HostScalarCookFn* fn = findHostScalarOp(n->type);
  if (!fn || !*fn) return;

  // Gather FloatList inputs (cookFloatListNode per wire; MultiInput → one list per wire) in spec
  // port order, mirroring cookFloatListNode's own FloatList-port gather (wire-declaration order).
  std::vector<std::vector<float>> inputLists;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!(port.isInput && port.dataType == "FloatList")) continue;
    const int inPin = pinId(id, (int)i);
    for (const Connection& c : g.connections) {
      if (c.toPin != inPin) continue;
      const std::vector<float>* up = cookFloatListNode(pinNode(c.fromPin));
      inputLists.push_back(up ? *up : std::vector<float>{});
      if (!port.multiInput) break;  // single-input: first wire only
    }
    // (An UNWIRED FloatList input contributes NO entry → empty → count/pick 0, matching TiXL null→0.)
  }
  // Gather String inputs (wire-OR-const) via the shared gather, in spec port order.
  std::vector<std::string> inputStrings = gatherStringInputs(g, id, *s, cookStringNode);

  float scalar = 0.0f;
  HostScalarCookCtx hc;
  hc.dev = dev; hc.lib = lib; hc.queue = queue;
  hc.ctx = &ctx; hc.nodeId = id;
  hc.inputLists = &inputLists;
  hc.inputStrings = &inputStrings;
  hc.params = nodeParams(id);
  hc.output = &scalar;
  (*fn)(hc);

  // Transport (legacy floatListBuf 1-elem) + BRIDGE (Node::outCache[0], const_cast — same precedent
  // as cookFlatStringLength). A host-scalar op's injectBug corrupts `scalar` IN the cook → both channels
  // carry the corrupted value → the downstream evalFloat RED bites on the real path.
  std::vector<float>& out = floatListBuf[flatKey(id)];
  out.assign(1, scalar);
  if (Node* mn = const_cast<Graph&>(g).node(id)) mn->outCache[0] = scalar;
}

}  // namespace sw
