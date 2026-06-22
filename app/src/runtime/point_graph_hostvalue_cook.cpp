// runtime/point_graph_hostvalue_cook — the FLAT HOST-VALUE cooks: the non-GPU value flows that ride
// between same-typed ports as self-sizing host containers (NO Metal allocation, NO pre-sizing — the op
// clears + fills the driver-owned container). FloatList (5th flow = TiXL Slot<List<float>>), ColorList
// (vec4-list = Slot<List<Vector4>>), Gradient (8th = Slot<Gradient>), PointList (7th = Slot<List<Point>>).
//
// Extracted VERBATIM from the cookFloatListNode / cookColorListNode / cookGradientNode / cookPointListNode
// lambdas that lived inside PointGraph::cook (point_graph.cpp) — a zero-behaviour-change move that buys
// ratchet headroom (point_graph.cpp was at its line cap) following the Cut-6 pilot extraction pattern
// (point_graph_mesh_cook.cpp).
//
// THE MECHANISM = thin-lambda → Impl-method delegation (NOT a context struct). Each flow's body moves to
// a PointGraph::Impl method (so it can name the private nested Impl + reach floatListBuf / colorListBuf /
// gradientBuf / pointListBuf / colorListState / outCount). cook()'s original std::function slots stay AS
// thin forwarding lambdas (so the closure web — every caller that recurses these slots, e.g. cookNode's
// PointList/Gradient gather, cookCommand's FloatList/Gradient gather, cookStringNode's FloatList gather,
// the host-scalar FloatList gather — keeps its one-call invocation untouched). Each method takes the
// minimal shared cook-stack state by reference: the const Graph& g, the const EvaluationContext& ctx,
// and the nodeParams memo (NodeParamsFn&, by-ref so the single-resolve-per-node memo is not copied).
//
// THE COUPLING SPLIT (why ColorList's signature is wider):
//  • FloatList / Gradient / PointList are CLOSED sub-graphs — like the mesh flow, they only ever recurse
//    into THEMSELVES (floatlist→floatlist, gradient→gradient, pointlist→pointlist). FloatList also reads
//    scalar Float MultiInput sources via evalFloat (a free graph.cpp recursion, no shared cook state).
//    So the only shared state these three need is g / ctx / nodeParams.
//  • ColorList CROSSES two extra boundaries: (1) ReadPointColors reads a Points bag back — it recurses
//    cookNode (the GPU Points cook) + reads p_->outCount; (2) a per-frame memo colorListCooked stops a
//    stateful colorlist op (KeepColors) from double-running under fan-out. cookNode is the giant cook()
//    closure web, so it rides in by-ref as a std::function<MTL::Buffer*(int)>&; colorListCooked rides in
//    by-ref as the cook-local memo map. Same minimal-shared-state contract as nodeParams — pass the slot,
//    not the whole closure. Its cross-frame KeepColors accumulator (colorListState) + outBuf/outCount are
//    Impl members → reached directly. NO *Inline twin is needed for ANY of the four: unlike the mesh flow
//    (which read back through PointGraph::debugCookedMesh, an OWNER method Impl can't call), every
//    host-value flow reads/writes its OWN Impl map directly, so there is nothing to inline.
//
// PLACEMENT: runtime leaf (depends only on the flat Graph + the four value-op registries + PointGraph::
//   Impl — all runtime). Defined as methods on PointGraph::Impl; cook() wraps them in forwarding lambdas.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl (floatListBuf/colorListBuf/...) + decls

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>
#include <simd/simd.h>

#include "runtime/colorlist_op_registry.h"  // ColorListCookCtx / findColorListOp
#include "runtime/floatlist_op_registry.h"  // FloatListCookCtx / findFloatListOp
#include "runtime/gradient_op_registry.h"   // GradientCookCtx / findGradientOp
#include "runtime/graph.h"  // Graph/Node/NodeSpec/PortSpec/Connection/pinId/pinNode/findSpec/evalFloat
#include "runtime/pointlist_op_registry.h"  // PointListCookCtx / findPointListOp
#include "runtime/sw_gradient.h"            // SwGradient (the 8th flow's host value)
#include "runtime/tixl_point.h"             // SwPoint (the 7th flow's element) + EvaluationContext

namespace sw {

using pgdetail::flatKey;

// Cook a FLOATLIST-flow node (the 5th cook flow = TiXL Slot<List<float>>). The currency is a HOST
// std::vector<float> living in Impl::floatListBuf (no GPU buffer, no pre-sizing — the op clears +
// fills it). The walker mirrors cookMeshNode but with an INPUT GATHER (cloned from cookNode's
// buffer-input loop): for each input port, gather upstream lists into `inputLists` (spec port
// order), then dispatch the op. Two input-port kinds feed `inputLists`:
//   • A "FloatList" input port → recurse cookFlatFloatList on each wired source. If the port is a
//     MultiInput, ALL wired sources are gathered as SEPARATE lists, in WIRE-DECLARATION order.
//   • A scalar "Float" MultiInput port (e.g. FloatsToList.Input) → gather ALL wired scalar sources
//     into ONE list via evalFloat, in WIRE-DECLARATION order; that single list becomes one
//     inputLists entry. (Producers like FloatsToList read inputLists[0] = their scalar inputs.)
// Gather order = the order connections appear in g.connections (the wire-declaration order), which
// is the SAME ordering the resident flatten uses for extraConns (resident_eval_flatten.cpp:255-268).
// Returns the cooked host list (nullptr if not a floatlist op / unknown node).
const std::vector<float>* PointGraph::Impl::cookFlatFloatList(const Graph& g,
                                                             const EvaluationContext& ctx,
                                                             const NodeParamsFn& nodeParams, int id) {
  const Node* n = g.node(id);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return nullptr;
  const FloatListCookFn* fn = findFloatListOp(n->type);
  if (!fn || !*fn) return nullptr;

  // Gather inputs in spec port order. Each entry is one upstream host list (FloatList source) or one
  // aggregated list of scalar Float sources (a scalar Float MultiInput port). MultiInput ports admit
  // MULTIPLE connections to the SAME pin → collect them in g.connections (wire-declaration) order.
  std::vector<std::vector<float>> inputLists;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!port.isInput) continue;
    const int inPin = pinId(id, (int)i);
    if (port.dataType == "FloatList") {
      // Recurse each wired FloatList source. MultiInput → every wire is a separate gathered list,
      // in wire-declaration order; single-input → at most one.
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        const std::vector<float>* up = cookFlatFloatList(g, ctx, nodeParams, pinNode(c.fromPin));
        inputLists.push_back(up ? *up : std::vector<float>{});
        if (!port.multiInput) break;  // single-input: first wire only
      }
    } else if (port.dataType == "Float" && port.multiInput) {
      // Aggregate all wired scalar Float sources into ONE list (the scalar MultiInput → list seam;
      // FloatsToList consumes this as inputLists[0]). Wire-declaration order. An unwired port
      // contributes an empty list (FloatsToList -> empty output, faithful to GetCollectedTypedInputs).
      std::vector<float> scalars;
      for (const Connection& c : g.connections)
        if (c.toPin == inPin) scalars.push_back(evalFloat(g, c.fromPin, ctx));
      inputLists.push_back(std::move(scalars));
    }
    // (Single scalar Float inputs / other dataTypes are read via resolved params, not gathered.)
  }

  std::vector<float>& out = floatListBuf[flatKey(id)];
  FloatListCookCtx fc;
  fc.dev = dev; fc.lib = lib; fc.queue = queue;
  fc.ctx = &ctx; fc.nodeId = id;
  fc.inputLists = &inputLists;
  fc.output = &out;
  fc.params = nodeParams(id);
  (*fn)(fc);
  return &out;
}

// Cook a COLORLIST-flow node (the vec4-list cook flow = TiXL Slot<List<Vector4>>). The currency is a
// HOST std::vector<simd::float4> living in Impl::colorListBuf (no GPU buffer, no pre-sizing — the op
// clears + fills it). Mirror of cookFlatFloatList over float4 with one extra gather kind for the
// vec4-as-4-floats MultiInput fork:
//   • A "ColorList" input port → recurse cookFlatColorList on each wired source (MultiInput → one list
//     per wire, wire-declaration order). (No ColorList-input op ships in this seam, but the gather is
//     here so a future CombineColorLists consumes it on the same driver.)
//   • The 4 PARALLEL scalar "Float" MultiInput component ports (Colors.x/.y/.z/.w) → gather each
//     port's wired scalar sources into its channel via evalFloat, in WIRE-DECLARATION order. The four
//     channels ride colorScalars[0..3]; ColorsToList zips index i across them into one output color.
// Gather order = g.connections order (wire-declaration), the same as cookFlatFloatList.
// (cookNode = the GPU Points cook slot, colorListCooked = the per-frame memo: both ride in by-ref from
// cook() so the Points-bag readback + the fan-out cycle guard keep parity — see the leaf header comment.)
const std::vector<simd::float4>* PointGraph::Impl::cookFlatColorList(
    const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
    const std::function<MTL::Buffer*(int)>& cookNode,
    std::map<int, const std::vector<simd::float4>*>& colorListCooked, int id) {
  const Node* n = g.node(id);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return nullptr;
  const ColorListCookFn* fn = findColorListOp(n->type);
  if (!fn || !*fn) return nullptr;
  // Per-frame memo: if this node already cooked this frame, return the cached output (do NOT re-run the
  // op — re-running a stateful op like KeepColors would double its cross-frame Insert under fan-out).
  if (auto it = colorListCooked.find(id); it != colorListCooked.end()) return it->second;

  std::vector<std::vector<simd::float4>> inputLists;       // upstream ColorList sources (combiner future)
  std::array<std::vector<float>, 4> colorScalars;          // the 4 parallel vec4-MultiInput channels
  int compChannel = 0;  // which of x/y/z/w the next Float MultiInput port fills (component order)
  const MTL::Buffer* pointsBag = nullptr;  // POINTS-bag input (ReadPointColors point-readback crossing)
  uint32_t pointsCount = 0;                 // its point count
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!port.isInput) continue;
    const int inPin = pinId(id, (int)i);
    if (port.dataType == "ColorList") {
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        const std::vector<simd::float4>* up =
            cookFlatColorList(g, ctx, nodeParams, cookNode, colorListCooked, pinNode(c.fromPin));
        inputLists.push_back(up ? *up : std::vector<simd::float4>{});
        if (!port.multiInput) break;  // single-input: first wire only
      }
    } else if (port.dataType == "Points" && !pointsBag) {
      // POINTS-bag gather (ReadPointColors point-readback rail-crossing): cook the upstream Points op
      // and borrow its cooked GPU bag + count (same gather as cookCommand's Points branch). The bag is
      // StorageModeShared and the upstream op committed+waited during its cook, so contents() is valid
      // CPU-side by the time ReadPointColors reads .Color from it. Single-input: first wire only.
      const Connection* c = g.connectionToInput(inPin);
      if (c) { pointsBag = cookNode(pinNode(c->fromPin)); pointsCount = outCount[flatKey(pinNode(c->fromPin))]; }
    } else if (port.dataType == "Float" && port.multiInput && compChannel < 4) {
      // One component channel of the vec4 MultiInput (Colors.x then .y then .z then .w). Aggregate all
      // wired scalar sources into this channel, wire-declaration order. An unwired channel stays empty
      // (faithful to GetCollectedTypedInputs: connected inputs only → that color slot reads 0 there).
      std::vector<float>& chan = colorScalars[compChannel++];
      for (const Connection& c : g.connections)
        if (c.toPin == inPin) chan.push_back(evalFloat(g, c.fromPin, ctx));
    }
  }

  std::vector<simd::float4>& out = colorListBuf[flatKey(id)];
  ColorListCookCtx cc;
  cc.dev = dev; cc.lib = lib; cc.queue = queue;
  cc.ctx = &ctx; cc.nodeId = id;
  cc.inputLists = &inputLists;
  cc.inputColorScalars = &colorScalars;
  cc.inputPointsBag = pointsBag;
  cc.inputPointsCount = pointsCount;
  cc.output = &out;
  cc.params = nodeParams(id);
  // Per-node CROSS-FRAME state slot (KeepColors's accumulator): the SAME PointGraph instance is reused
  // across frames, so colorListState[flatKey(id)] persists between cook() calls (the flat twin of the
  // resident s_colorListState). A stateless colorlist op ignores it. operator[] default-creates the
  // empty list on first cook (matches TiXL's `_list = []` field default, KeepColors.cs:46).
  cc.state = &colorListState[flatKey(id)];
  colorListCooked[id] = &out;  // memo BEFORE the op runs (cycle guard parity w/ Points `cooked`; out is stable)
  (*fn)(cc);
  return &out;
}

// Cook a GRADIENT-flow node (the 8th cook flow = TiXL Slot<Gradient>). The currency is a HOST
// SwGradient living in Impl::gradientBuf (no GPU buffer, no pre-sizing — the op writes its steps).
// VERBATIM clone of cookFlatFloatList (std::vector<float> → SwGradient): for each Gradient input
// port, gather upstream gradients (MultiInput → one per wire, wire-declaration order) into
// `inputGradients`, then dispatch the op. A pure producer (DefineGradient) has no Gradient input.
// Returns the cooked host gradient (nullptr if not a gradient op / unknown node).
const SwGradient* PointGraph::Impl::cookFlatGradient(const Graph& g, const EvaluationContext& ctx,
                                                    const NodeParamsFn& nodeParams, int id) {
  const Node* n = g.node(id);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return nullptr;
  const GradientCookFn* fn = findGradientOp(n->type);
  if (!fn || !*fn) return nullptr;

  std::vector<SwGradient> inputGradients;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!(port.isInput && port.dataType == "Gradient")) continue;
    const int inPin = pinId(id, (int)i);
    for (const Connection& c : g.connections) {
      if (c.toPin != inPin) continue;
      const SwGradient* up = cookFlatGradient(g, ctx, nodeParams, pinNode(c.fromPin));
      inputGradients.push_back(up ? *up : SwGradient{});
      if (!port.multiInput) break;  // single-input: first wire only
    }
  }

  SwGradient& out = gradientBuf[flatKey(id)];
  GradientCookCtx gc;
  gc.dev = dev; gc.lib = lib; gc.queue = queue;
  gc.ctx = &ctx; gc.nodeId = id;
  gc.inputGradients = &inputGradients;
  gc.output = &out;
  gc.params = nodeParams(id);
  (*fn)(gc);
  return &out;
}

// Cook a POINTLIST-flow node (the 7th cook flow = TiXL Slot<StructuredList> / StructuredList<Point>).
// The currency is a HOST std::vector<SwPoint> living in Impl::pointListBuf (no GPU buffer, no pre-
// sizing — the op clears + fills it). Clone of cookFlatFloatList (float→SwPoint): for each PointList
// input port, gather upstream lists (MultiInput → one list per wire, wire-declaration order) into
// `inputLists`, then dispatch the op. PointList CROSSES one boundary (it is no longer a closed sub-graph):
// PointsToCPU reads a GPU Points bag BACK (the DOWNLOAD mirror of ListToBuffer's host→GPU upload), so a
// "Points" input port is gathered by recursing cookNode (the GPU Points cook slot) + reading p_->outCount
// — the SAME Points-bag gather cookFlatColorList does for ReadPointColors (this leaf, lines above). The
// bag is StorageModeShared and the upstream op committed+waited during its cook, so contents() is valid
// CPU-side by the time PointsToCPU copies whole SwPoints out of it. Returns the cooked host list
// (nullptr if not a pointlist op / unknown node).
const std::vector<SwPoint>* PointGraph::Impl::cookFlatPointList(
    const Graph& g, const EvaluationContext& ctx, const NodeParamsFn& nodeParams,
    const std::function<MTL::Buffer*(int)>& cookNode, int id) {
  const Node* n = g.node(id);
  if (!n) return nullptr;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return nullptr;
  const PointListCookFn* fn = findPointListOp(n->type);
  if (!fn || !*fn) return nullptr;

  std::vector<std::vector<SwPoint>> inputLists;
  const MTL::Buffer* pointsBag = nullptr;  // POINTS-bag input (PointsToCPU GPU→host readback crossing)
  uint32_t pointsCount = 0;                 // its point count
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!port.isInput) continue;
    const int inPin = pinId(id, (int)i);
    if (port.dataType == "PointList") {
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        const std::vector<SwPoint>* up = cookFlatPointList(g, ctx, nodeParams, cookNode, pinNode(c.fromPin));
        inputLists.push_back(up ? *up : std::vector<SwPoint>{});
        if (!port.multiInput) break;  // single-input: first wire only
      }
    } else if (port.dataType == "Points" && !pointsBag) {
      // POINTS-bag gather (PointsToCPU GPU→host readback crossing): cook the upstream Points op and
      // borrow its cooked GPU bag + count (the SAME gather cookFlatColorList does for ReadPointColors).
      // The bag is StorageModeShared and the upstream op committed+waited during its cook, so contents()
      // is valid CPU-side by the time PointsToCPU copies SwPoints out of it. Single-input: first wire.
      const Connection* c = g.connectionToInput(inPin);
      if (c) { pointsBag = cookNode(pinNode(c->fromPin)); pointsCount = outCount[flatKey(pinNode(c->fromPin))]; }
    }
  }

  std::vector<SwPoint>& out = pointListBuf[flatKey(id)];
  PointListCookCtx pc;
  pc.dev = dev; pc.lib = lib; pc.queue = queue;
  pc.ctx = &ctx; pc.nodeId = id;
  pc.inputLists = &inputLists;
  pc.inputPointsBag = pointsBag;
  pc.inputPointsCount = pointsCount;
  pc.output = &out;
  pc.params = nodeParams(id);
  (*fn)(pc);
  return &out;
}

}  // namespace sw
