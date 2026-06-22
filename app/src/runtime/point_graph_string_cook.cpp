// runtime/point_graph_string_cook — the FLAT STRING cooks: the host-string value flows that ride between
// same-typed ports as self-sizing host std::strings (NO Metal allocation, NO pre-sizing — the op assigns
// the driver-owned container). String (6th flow = TiXL Slot<string>) + StringList (Slot<List<string>>,
// Sub-seam A). Plus gatherStringInputs — the SHARED String-input gather (wire-OR-const dual identity) that
// the string PRODUCERS here AND the host-scalar consumers (point_graph_hostscalar_cook.cpp) both call.
//
// Extracted VERBATIM from the cookStringNode / cookStringListNode lambdas + the gatherStringInputs lambda
// that lived inside PointGraph::cook (point_graph.cpp) — a zero-behaviour-change move that buys ratchet
// headroom (point_graph.cpp was at its line cap) following the Cut-6 / Cut-4 extraction pattern
// (point_graph_mesh_cook.cpp / point_graph_hostvalue_cook.cpp).
//
// THE MECHANISM = thin-lambda → Impl-method delegation (NOT a context struct). Each flow's body moves to a
// PointGraph::Impl method (so it can name the private nested Impl + reach stringBuf / stringListBuf /
// floatListBuf / stringState). cook()'s original std::function slots stay AS thin forwarding lambdas (so
// the closure web — every caller that recurses these slots: cookNode's nothing here, but the string
// PRODUCERS recurse cookStringNode/cookStringListNode through each other, the host-scalar branch + the
// StringLength branch call gatherStringInputs, cookStringNode reaches cookFloatListNode/cookStringListNode
// for the Sub-seam A bridge — keeps its one-call invocation untouched). Each method takes the minimal
// shared cook-stack state by reference: the const Graph& g, the const EvaluationContext& ctx, and the
// nodeParams memo (NodeParamsFn&, by-ref so the single-resolve-per-node memo is not copied).
//
// THE COUPLING (why the String signatures carry slots that the mesh/hostvalue ones did not): the String
// flow is NOT a closed sub-graph. A String op crosses into THREE other slots, so each rides in by-ref as a
// std::function (same minimal-shared-state contract as ColorList's cookNode/colorListCooked in Cut 4):
//   • cookStringNode      — the String PRODUCER slot. gatherStringInputs recurses it per wired String
//                           input; cookFlatStringListNode recurses it (via gatherStringInputs). The slot
//                           is self-recursive (a String producer feeding a String producer), so it must
//                           ride in even into cookFlatStringNode (which IS that slot's body) — the body
//                           reaches the SLOT, not itself, so the closure web is the single source.
//   • cookFloatListNode   — the FloatList currency slot (Cut 4 host-value). cookFlatStringNode gathers it
//                           for the FloatListToString.Value bridge (Sub-seam A).
//   • cookStringListNode  — the StringList currency slot. cookFlatStringNode gathers it for
//                           JoinStringList.Input (Sub-seam A); cookFlatStringListNode self-recurses it.
// stringState (HasStringChanged's `_lastString` cross-frame slot) + stringBuf / stringListBuf / floatListBuf
// are Impl members → reached directly (the SAME PointGraph instance is reused across frames, so the state
// map persists between cook() calls — the flat twin of frame_cook's s_stringState). NO *Inline twin is
// needed: every flow reads/writes its OWN Impl map directly.
//
// PLACEMENT: runtime leaf (depends only on the flat Graph + the string/stringlist registries +
//   PointGraph::Impl — all runtime). Defined as methods on PointGraph::Impl; cook() wraps them in
//   forwarding lambdas.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl (stringBuf / stringState / ...) + the decls

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"  // Graph/Node/NodeSpec/PortSpec/Connection/pinId/pinNode/findSpec
#include "runtime/string_op_registry.h"      // StringCookCtx/StringCookFn/StringState/findStringOp/stringInjectBug
#include "runtime/stringlist_op_registry.h"  // StringListCookCtx/StringListCookFn/findStringListOp
#include "runtime/tixl_point.h"              // EvaluationContext (StringCookCtx::ctx)

namespace sw {

using pgdetail::flatKey;

// Shared String-input gather (used by cookFlatStringNode for string PRODUCERS, by cookFlatStringListNode,
// and by the host-scalar consumers cookFlatStringLength / cookFlatHostScalar — a string CONSUMER produces
// a host scalar, so it does NOT register a StringCookFn). Cook a STRING-flow node's String inputs with the
// DUAL IDENTITY: for each String input port —
//   • WIRED   → recurse cookStringNode on the upstream source (its cooked string).
//   • UNWIRED → fall back to the strDef CONST (Node::strParams[id] if the node carries a stored override,
//               else the spec's PortSpec.strDef). fork-string-port-becomes-drivable: the wire-OR-const.
// A MultiInput String port yields one gathered entry PER WIRE (wire-declaration order), and contributes
// NOTHING when unwired (faithful to GetCollectedTypedInputs — CombineStrings). A single String port always
// yields exactly one entry (wired value or strDef const). `s` is the consuming node's spec; collects in
// spec port order. (cookStringNode = the String PRODUCER slot; rides in by-ref so the recursion follows the
// closure web — see the leaf header comment.)
std::vector<std::string> PointGraph::Impl::gatherStringInputs(
    const Graph& g, int id, const NodeSpec& s,
    const std::function<const std::string*(int)>& cookStringNode) {
  const Node* n = g.node(id);
  std::vector<std::string> inputStrings;
  for (size_t i = 0; i < s.ports.size(); ++i) {
    const PortSpec& port = s.ports[i];
    if (!(port.isInput && port.dataType == "String")) continue;
    const int inPin = pinId(id, (int)i);
    bool wired = false;
    for (const Connection& c : g.connections) {
      if (c.toPin != inPin) continue;
      wired = true;
      const std::string* up = cookStringNode(pinNode(c.fromPin));
      inputStrings.push_back(up ? *up : std::string{});
      if (!port.multiInput) break;  // single-input: first wire only
    }
    if (!wired && !port.multiInput) {
      // Unwired single String port → the strDef const (stored strParams override, else spec strDef).
      // fork-string-port-becomes-drivable: this is the wire-OR-const fallback. (A MultiInput String
      // port that is unwired contributes nothing — faithful to GetCollectedTypedInputs.)
      std::string def = port.strDef;
      if (n) {
        auto it = n->strParams.find(port.id);
        if (it != n->strParams.end()) def = it->second;
      }
      inputStrings.push_back(def);
    }
  }
  return inputStrings;
}

// Cook a STRING-flow node (the 6th cook flow = TiXL Slot<string>). The currency is a HOST std::string
// living in Impl::stringBuf (no GPU buffer, no pre-sizing — the op assigns it). The walker gathers String
// inputs via gatherStringInputs (wire-OR-const dual identity), then — Sub-seam A — gathers FloatList +
// StringList inputs (the bridge + the StringList currency) in spec port order. FloatListToString.Value
// rides inputFloatLists (via cookFloatListNode); JoinStringList.Input rides inputStringLists (via
// cookStringListNode). An UNWIRED list port contributes no entry (→ empty → ""); a MultiInput port yields
// one entry per wire. Returns the cooked host string (nullptr if not a string op / unknown node).
const std::string* PointGraph::Impl::cookFlatStringNode(
    const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
    const std::function<const std::vector<float>*(int)>& cookFloatListNode,
    const std::function<const std::vector<std::string>*(int)>& cookStringListNode,
    const std::function<const std::string*(int)>& cookStringNode, int id) {
  const Node* n = g.node(id);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return nullptr;
  const StringCookFn* fn = findStringOp(n->type);
  if (!fn || !*fn) return nullptr;

  std::vector<std::string> inputStrings = gatherStringInputs(g, id, *s, cookStringNode);

  // Sub-seam A: gather FloatList + StringList inputs (the bridge + the StringList currency) in spec port
  // order, mirroring cookHostScalar / cookColorListNode. FloatListToString.Value rides inputFloatLists
  // (via cookFloatListNode); JoinStringList.Input rides inputStringLists (via cookStringListNode). An
  // UNWIRED list port contributes no entry (→ empty → ""); a MultiInput port yields one entry per wire.
  std::vector<std::vector<float>> inputFloatLists;
  std::vector<std::vector<std::string>> inputStringLists;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!port.isInput) continue;
    const int inPin = pinId(id, (int)i);
    if (port.dataType == "FloatList") {
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        const std::vector<float>* up = cookFloatListNode(pinNode(c.fromPin));
        inputFloatLists.push_back(up ? *up : std::vector<float>{});
        if (!port.multiInput) break;
      }
    } else if (port.dataType == "StringList") {
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        const std::vector<std::string>* up = cookStringListNode(pinNode(c.fromPin));
        inputStringLists.push_back(up ? *up : std::vector<std::string>{});
        if (!port.multiInput) break;
      }
    }
  }

  std::string& out = stringBuf[flatKey(id)];
  // MULTI-OUTPUT (Sub-seam B): a multi-output op (PickStringPart, FilePathParts) fills these EXTRA
  // sinks keyed by its spec output-port index — port-0 single-output stays BYTE-IDENTICAL (incumbents
  // leave both empty). Extra strings ride flatKey(id)+":"+portIdx (debugCookedStringPort); scalar
  // outputs ride Node::outCache[portIdx] (the flat host-scalar/bridge channel, downstream-readable).
  std::map<int, std::string> extraStr;
  std::map<int, float> scalarOut;
  StringCookCtx sc;
  sc.dev = dev; sc.lib = lib; sc.queue = queue;
  sc.ctx = &ctx; sc.nodeId = id;
  sc.inputStrings = &inputStrings;
  sc.inputFloatLists = &inputFloatLists;    // Sub-seam A: FloatListToString.Value (the bridge)
  sc.inputStringLists = &inputStringLists;  // Sub-seam A: JoinStringList.Input (the StringList currency)
  sc.output = &out;
  sc.params = nodeParams(id);
  sc.extraStrOutputs = &extraStr;
  sc.scalarOutputs = &scalarOut;
  // Per-node CROSS-FRAME state (HasStringChanged's `_lastString`): this PointGraph persists across cooks
  // so stringState[flatKey(id)] survives frame→frame (flat twin of s_stringState; stateless ops ignore it).
  sc.state = &stringState[flatKey(id)];
  (*fn)(sc);
  // Distribute the EXTRA outputs (no-ops for single-output incumbents → byte-identical port-0 path).
  for (auto& kv : extraStr) stringBuf[flatKey(id) + ":" + std::to_string(kv.first)] = kv.second;
  if (!scalarOut.empty()) {
    // Node::outCache is float[3] (the flat host-scalar/bridge channel). Guard the bound: a scalar
    // output at port idx>=3 (FilePathParts.FileExists @ port 3) has no flat outCache slot — it rides
    // ONLY the resident extOut[8] channel (production), and the FilePathParts flat golden asserts only
    // the path-string outputs (hermetic), not FileExists. PickStringPart.TotalCount @ port 1 fits.
    if (Node* mn = const_cast<Graph&>(g).node(id))
      for (auto& kv : scalarOut)
        if (kv.first >= 0 && kv.first < (int)(sizeof(mn->outCache) / sizeof(mn->outCache[0])))
          mn->outCache[kv.first] = kv.second;
  }
  return &out;
}

// STRINGLIST cook flow (host List<string> = TiXL Slot<List<string>>, Sub-seam A). Mirror of
// cookColorListNode over std::string: gather this op's String inputs (via gatherStringInputs, wire-OR-const)
// and StringList inputs (recurse cookStringListNode), then dispatch the op into Impl::stringListBuf.
// SplitString is the first producer (String input → list). Returns the cooked host list (nullptr if not a
// stringlist op / unknown node). (cookStringNode + cookStringListNode ride in by-ref so gatherStringInputs
// recurses the String producers + the StringList self-recursion follows the closure web.)
const std::vector<std::string>* PointGraph::Impl::cookFlatStringListNode(
    const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
    const std::function<const std::string*(int)>& cookStringNode,
    const std::function<const std::vector<std::string>*(int)>& cookStringListNode, int id) {
  const Node* n = g.node(id);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return nullptr;
  const StringListCookFn* fn = findStringListOp(n->type);
  if (!fn || !*fn) return nullptr;

  std::vector<std::string> inputStrings = gatherStringInputs(g, id, *s, cookStringNode);  // SplitString.String / .Split
  std::vector<std::vector<std::string>> inputLists;                    // upstream StringList (future combiner)
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!(port.isInput && port.dataType == "StringList")) continue;
    const int inPin = pinId(id, (int)i);
    for (const Connection& c : g.connections) {
      if (c.toPin != inPin) continue;
      const std::vector<std::string>* up = cookStringListNode(pinNode(c.fromPin));
      inputLists.push_back(up ? *up : std::vector<std::string>{});
      if (!port.multiInput) break;
    }
  }

  std::vector<std::string>& out = stringListBuf[flatKey(id)];
  StringListCookCtx slc;
  slc.dev = dev; slc.lib = lib; slc.queue = queue;
  slc.ctx = &ctx; slc.nodeId = id;
  slc.inputStrings = &inputStrings;
  slc.inputLists = &inputLists;
  slc.output = &out;
  slc.params = nodeParams(id);
  (*fn)(slc);
  return &out;
}

}  // namespace sw
