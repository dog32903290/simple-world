#include "runtime/point_graph.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary/Symbol/SymbolChild (lib defaultDrawTarget)
#include "runtime/curve.h"                // sw::Curve (bake-into-point seam: PointCookCtx::inputCurves complete type)
#include "runtime/colorlist_op_registry.h"     // ColorListCookCtx/findColorListOp (vec4-list cook flow = host List<Vector4>)
#include "runtime/floatlist_op_registry.h"     // FloatListCookCtx/findFloatListOp (the 5th cook flow = host List<float>)
#include "runtime/gradient_op_registry.h"      // GradientCookCtx/findGradientOp (the 8th cook flow = host Gradient)
#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/pinNode/findSpec
#include "runtime/host_scalar_op_registry.h"   // HostScalarCookCtx/findHostScalarOp (FloatList→Float bridge: list-routing seam)
#include "runtime/image_filter_op_registry.h"  // imageFilterComputeTypes/imageFilterSizeFns (compute leaf seam)
#include "runtime/mesh_op_registry.h"     // MeshCookCtx/findMeshOp (the 4th cook flow = MeshBuffers)
#include "runtime/string_op_registry.h"        // StringCookCtx/findStringOp (the 6th cook flow = host std::string)
#include "runtime/stringlist_op_registry.h"    // StringListCookCtx/findStringListOp (host List<string> = Sub-seam A)
#include "runtime/pointlist_op_registry.h"     // PointListCookCtx/findPointListOp (the 7th cook flow = host SwPoint list)
#include "runtime/point_graph_internal.h" // PointGraph::Impl + op registries (shared w/ resident cook)
#include "runtime/point_ops_setvarcmd.h"  // S3a: cmdVarPush/cmdVarRestore/isCmdContextVarWriter/setVarBugSkipWrite
#include "runtime/tixl_point.h"           // SwPoint (64B) + EvaluationContext (via eval_context.h)

namespace sw {

// The ListToBuffer upload-bridge predicate (pointlist_ops_listtobuffer.cpp). The cook driver detects
// this Points-producing op and takes the PointList-gather + memcpy path (the host→GPU crossing) instead
// of the generic Points-input gather. Forward-declared (defined in the leaf) so both cook driver TUs see it.
bool isListToBufferType(const std::string& type);

using pgdetail::cmdReg;
using pgdetail::cookReg;
using pgdetail::drawReg;
using pgdetail::fillPointCamera;
using pgdetail::flatKey;
using pgdetail::isBufferInput;
using pgdetail::texReg;

// registerBuiltinPointOps() is defined in point_ops.cpp (the real operators).
// Registry/sink spine (cookReg/drawReg/cmdReg/texReg Meyers singletons + registrars +
// texOwnsOutput / texOwnFormat / feedback sinks) extracted to point_graph_registry.cpp.

// --- resolved-param accessors (the slice-2b seam; PointCookCtx::params docs) ---
// The resolved-param accessor spine (cookParam×4 / cookVecN×3 / cookInputParam) and the feedback
// multi-tex-output helper (pgdetail::texOutputOrdinal) live in point_graph_params.cpp (extracted to
// keep this file under its line-count cap — pure relocation; cookParam/cookVecN decls in point_graph.h,
// texOutputOrdinal decl in point_graph_internal.h). The one raw-map read this file still needs (the
// per-input Count fallback below) keeps a tiny local mapParam — the only non-ctx caller.
namespace {
float mapParam(const std::map<std::string, float>* m, const char* id, float def) {
  if (!m) return def;
  auto it = m->find(id);
  return it != m->end() ? it->second : def;
}
}  // namespace

// ---------------------------------------------------------------------------

PointGraph::PointGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* queue, uint32_t width,
                       uint32_t height)
    : p_(new Impl) {
  p_->dev = dev->retain();
  p_->lib = lib ? lib->retain() : nullptr;
  p_->queue = queue->retain();
  p_->width = width;
  p_->height = height;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(kPointTargetFormat, width, height, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  p_->target = dev->newTexture(td);
}

PointGraph::~PointGraph() {
  for (auto& kv : p_->state)
    if (kv.second && p_->stateFree.count(kv.first) && p_->stateFree[kv.first])
      p_->stateFree[kv.first](kv.second);
  for (auto& kv : p_->outBuf)
    if (kv.second) kv.second->release();
  for (auto& kv : p_->texBuf)
    if (kv.second) kv.second->release();
  for (auto& kv : p_->meshVtxBuf)
    if (kv.second) kv.second->release();
  for (auto& kv : p_->meshIdxBuf)
    if (kv.second) kv.second->release();
  for (auto& kv : p_->feedbackTexBuf) {  // cross-frame PAIR (KeepPreviousFrame): release BOTH
    if (kv.second.a) kv.second.a->release();
    if (kv.second.b) kv.second.b->release();
  }
  if (p_->target) p_->target->release();
  if (p_->queue) p_->queue->release();
  if (p_->lib) p_->lib->release();
  if (p_->dev) p_->dev->release();
  delete p_;
}

bool PointGraph::valid() const { return p_->dev && p_->queue && p_->target; }
// The texture the viewport shows: a RenderTarget terminal's own resolution-sized texture when one
// cooked this frame, else the window-sized fallback. Consumers (OutputWindow ImGui::Image, eye
// readback) re-read this each frame and handle arbitrary size — so a tex terminal can be any res.
MTL::Texture* PointGraph::target() const { return p_->displayTex ? p_->displayTex : p_->target; }

// The PointGraph::debugCooked* test-support readback accessors live in point_graph_debug.cpp (extracted
// to keep this file at-or-below its line-count cap, ARCHITECTURE.md rule 4 ratchet). Sub-seam A added
// debugCookedStringList there alongside the existing per-flow readbacks.

// PointGraph::defaultDrawTarget (both overloads) live in point_graph_debug.cpp (extracted to keep this
// file at-or-below its line-count cap, ARCHITECTURE.md rule 4 ratchet).

void PointGraph::cook(const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
                      int targetNodeId, ContextVarMap* ctxVars) {
  p_->displayTex = nullptr;  // default: target() shows the window-sized texture (cmd/preview paths)
  // S1 seam: seed RequestedResolution to the window (TiXL OutputWindow.cs:411-414 seeding before eval).
  p_->requestedResolution = RenderResolution{p_->width, p_->height};
  const Node* target = g.node(targetNodeId);
  const NodeSpec* ts = target ? findSpec(target->type) : nullptr;
  if (!target || !ts) { p_->clearTarget(); return; }  // no/unknown target -> black, no crash

  std::map<int, MTL::Buffer*> cooked;  // this-frame memo (cook each node once)

  // FEEDBACK per-frame memo (the cross-frame ping-pong flow): a feedback op (KeepPreviousFrame) MUST
  // run its blit + toggle EXACTLY ONCE per frame — but tex nodes have no per-frame memo (every output
  // pull re-cooks). If both PreviousFrame AND CurrentFrame are wired, the node would otherwise cook
  // twice → double toggle → wrong frame returned. This memo caches the resolved OUTPUT textures (by
  // output ordinal) the first time the node cooks this frame; the second output pull reads the cache.
  std::map<int, std::array<MTL::Texture*, FeedbackCookCtx::kMaxTexOutputs>> feedbackCooked;

  // Per-node resolved Float params (the 2b seam): resolved ONCE per node per cook through the
  // full value spine (override → binding → wire → stored → default, graph.cpp), then handed to
  // the op via PointCookCtx::params. Stored in a node-keyed memo so pointers stay stable for
  // the whole cook (ops + inputParams point into it).
  std::map<int, std::map<std::string, float>> paramsMemo;
  // S3b LIVE-READ memo trap: a node whose Float param is driven by a value-rail GetFloatVar resolves to a
  // DIFFERENT value inside vs outside a SetVarCmd scope (the live var vs its fallback). The memo keys only on
  // node id — so a value cached under one scope-state must NOT be served under the other. While a live scope is
  // active (liveCtxVars()!=nullptr) we resolve FRESH and DO NOT cache (the scoped resolution is ambient-dependent,
  // not a property of the graph). Off-scope the memo behaves exactly as before — every existing cook is unchanged
  // (the live branch is reachable only inside a SetVarCmd SubGraph cook, which ~243 golden callers never enter).
  std::map<int, std::map<std::string, float>> scopedScratch;  // per-cook owner of fresh scoped param maps
  std::function<const std::map<std::string, float>*(int)> nodeParams =
      [&](int id) -> const std::map<std::string, float>* {
    const Node* n = g.node(id);
    if (!n) return nullptr;
    if (liveCtxVars()) return &(scopedScratch[id] = resolveNodeParams(g, *n, ctx, reg));  // fresh, uncached
    auto it = paramsMemo.find(id);
    if (it != paramsMemo.end()) return &it->second;
    return &(paramsMemo[id] = resolveNodeParams(g, *n, ctx, reg));
  };

  // Forward-declared (body below): the PointList cook flow (7th flow = host std::vector<SwPoint>). A
  // pointlist op (RadialPointsCpu/TransformCpuPoint) gathers its PointList inputs by recursing into
  // cookPointListNode; the ListToBuffer bridge (a Points op, cookNode below) also calls it to gather the
  // host list it memcpys to the GPU. Declared HERE (before cookNode) so cookNode's ListToBuffer branch
  // can call it; std::function breaks the ordering (the body is assigned after cookNode is defined).
  std::function<const std::vector<SwPoint>*(int)> cookPointListNode;

  // Forward-declared HERE (above cookNode, same trick as cookPointListNode) so cookNode's Texture2D
  // gather (the texture-into-points seam) can recurse into the upstream tex op: a Points op with a
  // Texture2D input (SamplePointColorAttributes) cooks each wired Texture2D input via cookTexNode into
  // PointCookCtx::inputTextures. cookTexNode's body is assigned far below (it's mutually recursive with
  // cookCommand); std::function breaks the ordering. `outAbsPort` = the ABSOLUTE output port index the
  // consumer wired to (single-output ops ignore it; -1 = the node's terminal/first output).
  std::function<MTL::Texture*(int, int)> cookTexNode;

  // Forward-declared HERE (above cookNode, same trick as cookTexNode) so cookNode's Gradient gather (the
  // bake-into-point seam) can recurse into an upstream host-Gradient op: a Points op with a Gradient
  // input (MapPointAttributes) gathers each wired Gradient via cookGradientNode into
  // PointCookCtx::inputGradients. The body is assigned far below (same std::function ordering-break).
  std::function<const SwGradient*(int)> cookGradientNode;

  // Forward-declared HERE (above cookNode, same trick as cookGradientNode) so cookNode's Mesh gather
  // (the mesh-into-points seam) can call the bridge: a Points op with a Mesh input (MeshVerticesToPoints)
  // cooks its upstream mesh node + borrows its buffers via cookMeshInto. cookMeshInto cooks cookMeshNode
  // then reads back via debugCookedMesh; both bodies are assigned far below (cookMeshNode self-recurses
  // for mesh consumers, cookMeshInto has no command recursion). Returns true + fills the out-params.
  std::function<bool(int, const MTL::Buffer*&, uint32_t&, const MTL::Buffer*&, uint32_t&)> cookMeshInto;
  std::function<bool(int)> cookMeshNode;

  std::function<MTL::Buffer*(int)> cookNode = [&](int id) -> MTL::Buffer* {
    auto m = cooked.find(id);
    if (m != cooked.end()) return m->second;
    const Node* n = g.node(id);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->type);
    if (!s) return nullptr;

    // PointList → GPU UPLOAD BRIDGE (ListToBuffer): a Points-producing op whose INPUT is a host
    // PointList (not a GPU Points buffer). Gather its PointList input(s) via cookPointListNode
    // (MultiInput-expanded, wire-declaration order = TiXL Lists.CollectedInputs), concatenate into one
    // host SwPoint vector, size the output buffer to the total count (ensureOut), and memcpy the host
    // points into contents() (StorageModeShared). After this, the output is a normal GPU point bag the
    // downstream DrawPoints path consumes unchanged. ListToBuffer.cs: empty inputs → null/0 (here:
    // ensureOut(0) → a 1-cap buffer with outCount 0, the engine's "empty bag" convention).
    if (isListToBufferType(n->type)) {
      std::vector<SwPoint> all;
      for (size_t i = 0; i < s->ports.size(); ++i) {
        const PortSpec& port = s->ports[i];
        if (!(port.isInput && port.dataType == "PointList")) continue;
        const int inPin = pinId(id, (int)i);
        for (const Connection& c : g.connections) {
          if (c.toPin != inPin) continue;
          const std::vector<SwPoint>* up = cookPointListNode(pinNode(c.fromPin));
          if (up) all.insert(all.end(), up->begin(), up->end());
          if (!port.multiInput) break;  // single-input: first wire only
        }
      }
      uint32_t count = (uint32_t)all.size();
      MTL::Buffer* out = p_->ensureOut(flatKey(id), count);  // sizes + records outCount[key]=count
      if (out && count > 0)
        std::memcpy(out->contents(), all.data(), (size_t)count * sizeof(SwPoint));  // host→GPU memcpy
      cooked[id] = out;
      return out;
    }

    // Gather buffer inputs (Points + ParticleForce input ports, in spec order) + their counts
    // + the feeding node's resolved params (force ops read these via cookInputParam).
    // sumPointsCount = total over ALL wired Points inputs (combine concatenates; modifier/
    // generator have <=1 so it equals the old first-input behavior).
    std::vector<const MTL::Buffer*> ins;
    std::vector<uint32_t> insCounts;
    std::vector<const std::map<std::string, float>*> insParams;
    uint32_t sumPointsCount = 0;
    uint32_t firstPointsCount = 0;
    bool haveFirstPoints = false;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!isBufferInput(port)) continue;
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      MTL::Buffer* ub = c ? cookNode(pinNode(c->fromPin)) : nullptr;
      uint32_t inCount = (c && ub) ? p_->outCount[flatKey(pinNode(c->fromPin))] : 0u;
      ins.push_back(ub);
      insCounts.push_back(inCount);
      insParams.push_back(c ? nodeParams(pinNode(c->fromPin)) : nullptr);
      if (port.dataType == "Points") {
        sumPointsCount += inCount;
        if (!haveFirstPoints) { firstPointsCount = inCount; haveFirstPoints = true; }
      }
    }

    // TEXTURE2D gather (the texture-into-points seam): a Points op with "Texture2D" input ports
    // (SamplePointColorAttributes) cooks each wired upstream tex op into PointCookCtx::inputTextures,
    // in spec port order. Cloned from the tex-flow gather in cookTexNode (the Texture2D branch below):
    // each Texture2D port occupies the next slot (wired or not); the source's output port ordinal is
    // (fromPin-1)%100. Empty for every existing Points op (no Texture2D port) → texInputs all null,
    // texInputCount 0 → byte-identical path. isBufferInput() skips Texture2D ports above, so this loop
    // is the ONLY consumer of them — no double-count of Points.
    const MTL::Texture* texInputs[PointCookCtx::kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
    int texInputCount = 0;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!port.isInput || port.dataType != "Texture2D") continue;
      int slot = texInputCount;  // each Texture2D port occupies the next slot (wired or not)
      if (slot < PointCookCtx::kMaxTexInputs) {
        const Connection* c = g.connectionToInput(pinId(id, (int)i));
        texInputs[slot] = c ? cookTexNode(pinNode(c->fromPin), (c->fromPin - 1) % 100) : nullptr;
        texInputCount = slot + 1;
      }
    }

    // MESH gather (the mesh-into-points seam): a Points op with a "Mesh" input port (MeshVerticesToPoints)
    // cooks its FIRST wired upstream Mesh op (QuadMesh/NGonMesh) via cookMeshInto (the existing bridge:
    // cookMeshNode → debugCookedMesh) and borrows its vertex+index buffers + counts into PointCookCtx.
    // Single Mesh input (not an array) — mirror of the Command-flow Mesh gather (cookCommand's Mesh
    // branch) which DrawMeshUnlit consumes. Empty for every existing Points op (no Mesh port) →
    // meshVtx null / counts 0 → byte-identical path. isBufferInput() skips Mesh ports above.
    const MTL::Buffer* meshVtx = nullptr;
    const MTL::Buffer* meshIdx = nullptr;
    uint32_t meshVtxCount = 0, meshFaceCount = 0;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!port.isInput || port.dataType != "Mesh") continue;
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      if (c) cookMeshInto(pinNode(c->fromPin), meshVtx, meshVtxCount, meshIdx, meshFaceCount);
      break;  // single Mesh input
    }

    // GRADIENT + CURVE gather (the bake-into-point seam): a Points op with "Gradient"/"Curve" host-value
    // input ports (MapPointAttributes) gathers each wired upstream host value here, in spec port order,
    // into PointCookCtx::inputGradients/inputCurves. Same gather contract as cookTexNode's Gradient
    // branch: a wired Gradient recurses cookGradientNode; there is NO Curve PRODUCER op yet (curve.h),
    // so a wired Curve is never gathered (the loop only DETECTS the "Curve" port). UNWIRED → empty → the
    // op bakes its embedded .t3 defaults (flat-1.0 curve, white→white gradient). Empty for every existing
    // Points op (no Gradient/Curve port) → tc.inputGradients/inputCurves stay null → byte-identical.
    std::vector<SwGradient> gradientInputs;
    bool hasGradientInput = false;
    std::vector<Curve> curveInputs;  // stays empty: no Curve producer (see above)
    bool hasCurveInput = false;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!port.isInput) continue;
      if (port.dataType == "Curve") {
        hasCurveInput = true;  // DETECT only (no producer to gather)
      } else if (port.dataType == "Gradient") {
        hasGradientInput = true;
        const Connection* c = g.connectionToInput(pinId(id, (int)i));
        if (c) {
          const SwGradient* up = cookGradientNode(pinNode(c->fromPin));
          gradientInputs.push_back(up ? *up : SwGradient{});
          // MapPointAttributes' Gradient is single-input; no MultiInput expansion needed here.
        }
      }
    }

    const std::map<std::string, float>* params = nodeParams(id);

    // count: a "Count" Float input (generators) resolved through the value spine (the resolved
    // map already holds wire/stored/default — the 7d4b34e contract), else the sum of all wired
    // Points inputs (modifier passes through, combine concatenates).
    // Default: sum of Points inputs (combine concatenates). Ops that transform a primary bag
    // using extra Points inputs as references (SnapToPoints) opt into first-input-count instead.
    uint32_t count = sumPointsCount;
    if (auto cr = cookReg().find(n->type); cr != cookReg().end()) {
      if (cr->second.countFromFirstPointsInput) count = firstPointsCount;
      // mesh-into-points fork: one Point per gathered mesh vertex (MeshVerticesToPoints).
      if (cr->second.countFromMeshVtx) count = meshVtxCount;
    }
    for (const PortSpec& port : s->ports)
      if (port.isInput && port.dataType == "Float" && port.id == "Count") {
        float v = mapParam(params, "Count", port.def);
        count = v > 0.0f ? (uint32_t)(v + 0.5f) : 0u;
        break;
      }

    // Op may remap count (ParticleSystem grows a pool > its emit ring; emit count stays
    // available to the op as inputCounts[0]). Output + state size to the remapped count.
    if (auto rr = cookReg().find(n->type); rr != cookReg().end() && rr->second.countTransform)
      count = rr->second.countTransform(count);

    MTL::Buffer* out = p_->ensureOut(flatKey(id), count);
    void* st = p_->ensureState(flatKey(id), n->type, count);

    PointCookCtx cc;
    cc.dev = p_->dev; cc.lib = p_->lib; cc.queue = p_->queue;
    cc.ctx = &ctx; cc.graph = &g; cc.reg = reg;
    cc.nodeId = id; cc.count = count;
    cc.inputs = ins.data(); cc.inputCounts = insCounts.data(); cc.inputCount = (int)ins.size();
    cc.output = out; cc.state = st;
    cc.params = params; cc.inputParams = insParams.data();
    for (int k = 0; k < texInputCount; ++k) cc.inputTextures[k] = texInputs[k];
    cc.inputTextureCount = texInputCount;  // texture-into-points seam (0 for ops with no Texture2D input)
    cc.inputGradients = hasGradientInput ? &gradientInputs : nullptr;  // bake-into-point seam (null otherwise)
    cc.inputCurves = hasCurveInput ? &curveInputs : nullptr;  // empty in production (no Curve producer)
    cc.meshVtx = meshVtx; cc.meshVtxCount = meshVtxCount;  // mesh-into-points seam (null/0 if no Mesh input)
    cc.meshIdx = meshIdx; cc.meshFaceCount = meshFaceCount;
    // Camera aspect = ACTIVE RequestedResolution (S1 seam, EvaluationContext.cs:78,94), not raw window.
    const RenderResolution& rrC = p_->requestedResolution;  // camera seam
    fillPointCamera(cc, *s, (rrC.h > 0) ? (float)rrC.w / (float)rrC.h : 1.0f);
    auto r = cookReg().find(n->type);
    if (r != cookReg().end() && r->second.cook) r->second.cook(cc);
    cooked[id] = out;
    return out;
  };

  // Realize the terminal into target() via the Command→Texture executor (the render-target
  // pivot's three-flow cook). DrawPoints is now a COMMAND op and RenderTarget a TEXTURE op
  // (point_ops.cpp); the legacy "buffer→pixels" draw flow is gone. The terminal is one of:
  //   • a texture op (RenderTarget): gather its upstream Command chain, execute into target;
  //   • a command op (DrawPoints): cook its 1-item Command, execute into target;
  //   • a Points-producing op (preview pin, view⊥graph §5): synthesize a 1-item Command from
  //     the cooked bag, execute it — reuses the same executor, no separate draw path.
  // All paths run through a registered texture op = the single executor that owns the render
  // pass (render_command.h: the executor owns Prepare/Restore, the op only carries data).

  // (cookTexNode is forward-declared ABOVE cookNode — moved there so cookNode's Texture2D gather can
  // call it. It is mutually recursive with cookCommand: cookTexNode calls cookCommand for its Command
  // inputs, cookCommand calls cookTexNode for a Texture2D input (FORK#1); std::function breaks the
  // ordering cycle. A feedback op (KeepPreviousFrame) returns PreviousFrame vs CurrentFrame by outAbsPort.)
  // Forward-declared too: cookTexNode (a tex op like ValuesToTexture, the rail-crossing) gathers its
  // FloatList inputs by recursing into cookFloatListNode, whose body is assigned further below. Same
  // std::function ordering-break as cookTexNode itself.
  std::function<const std::vector<float>*(int)> cookFloatListNode;
  // Forward-declared: the COLORLIST cook flow (vec4-list = host List<Vector4>). A colorlist op
  // (ColorsToList) gathers its inputs (the 4 parallel scalar Float MultiInput component channels for
  // ColorsToList; or recursively a ColorList input for a future combiner) — body assigned below.
  // std::function breaks the self-recursion ordering (a ColorList consumer ← upstream ColorList
  // producer). Mirror of cookFloatListNode over simd::float4.
  std::function<const std::vector<simd::float4>*(int)> cookColorListNode;
  // Per-frame COLORLIST memo (cook each colorlist node at most ONCE per frame) — the flat twin of the
  // Points-path `cooked` memo (this file:348) and the resident path's top-loop-vs-recursion split
  // (resident_colorlist_cook.cpp:75,80 passes state=nullptr on the recursive ColorList gather so only
  // the top loop runs the stateful cook). WHY: a stateful colorlist op (KeepColors, the only one) feeding
  // a fan-out/diamond (e.g. CombineColorLists MultiInput on two wires) was re-cooked once PER CONSUMING
  // WIRE → its cross-frame Insert ran N times/frame → accumulator grew N/frame instead of 1. The memo
  // caches the cooked output pointer the first time a node cooks this frame; later consumers read the
  // cache (no second stateful Insert). A stateless op is byte-identical either way (it never touches
  // state); this only changes the stateful-op-under-fan-out case the golden's diamond leg pins.
  std::map<int, const std::vector<simd::float4>*> colorListCooked;
  // (cookGradientNode is forward-declared ABOVE cookNode now — moved up so cookNode's Gradient gather
  // for the bake-into-point seam can call it. cookTexNode's GradientsToTexture rail-crossing uses the
  // same variable; its body is assigned further below.)
  // Forward-declared: the String cook flow (6th flow = host std::string). A string op (CombineStrings/
  // StringLength) gathers its String inputs by recursing into cookStringNode (a wired String input);
  // body assigned below. std::function breaks the self-recursion ordering (CombineStrings ← upstream
  // FloatToString ← … all String producers). StringLength produces a Float output (int→Float), so it
  // is NOT a string op — it does NOT register a StringCookFn (see leaf comment); it is cooked by the
  // value-eval path. Only producers of a String output ride cookStringNode.
  std::function<const std::string*(int)> cookStringNode;
  // Forward-declared: the STRINGLIST cook flow (host List<string> = TiXL Slot<List<string>>, Sub-seam A).
  // A stringlist op (SplitString) gathers its String inputs (via cookStringNode) into a host list; a
  // future StringList combiner recurses cookStringListNode on a StringList input. Body assigned below.
  // std::function breaks the self-recursion ordering. JoinStringList CONSUMES this list (its StringList
  // input gather, in cookStringNode below) — the StringList-into-String half of Sub-seam A.
  std::function<const std::vector<std::string>*(int)> cookStringListNode;
  // (cookMeshInto / cookMeshNode forward-declared ABOVE cookNode — moved up so cookNode's Mesh gather
  // for the mesh-into-points seam can call cookMeshInto. cookCommand's Mesh branch uses the same vars;
  // bodies assigned far below. cookMeshNode self-recurses for mesh consumers, cookMeshInto reads back.)

  // Cook a command node: resolve its upstream Points bag (+ first wired Texture2D input, FORK#1, +
  // first wired Command subtree for the Camera op, Cut 3), then call its cmd fn -> RenderCommand.
  // std::function (not auto) so it can recurse into an upstream command node (Camera wraps Command).
  std::set<int> cmdVisiting;  // cycle guard for Command→Command recursion (Camera wraps Command)
  std::function<RenderCommand(int)> cookCommand = [&](int id) -> RenderCommand {
    RenderCommand rc;
    const Node* n = g.node(id);
    const NodeSpec* s = n ? findSpec(n->type) : nullptr;
    if (!n || !s) return rc;
    auto cm = cmdReg().find(n->type);
    if (cm == cmdReg().end() || !cm->second) return rc;
    if (!cmdVisiting.insert(id).second) return rc;  // already on the command stack → break the cycle
    MTL::Buffer* pts = nullptr;
    uint32_t cnt = 0;
    const MTL::Texture* inTex = nullptr;
    const MTL::Buffer* inMeshVtx = nullptr;  // first wired Mesh input (DrawMeshUnlit, Cut 99)
    const MTL::Buffer* inMeshIdx = nullptr;
    uint32_t inMeshFaces = 0;
    bool haveMesh = false;
    RenderCommand inCmd;          // Camera op's Command subtree (Cut 3); empty unless a Command input wired
    bool haveInCmd = false;
    bool havePts = false;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!port.isInput) continue;
      if (port.dataType == "Points" && !havePts) {
        const Connection* c = g.connectionToInput(pinId(id, (int)i));
        if (c) { pts = cookNode(pinNode(c->fromPin)); cnt = p_->outCount[flatKey(pinNode(c->fromPin))]; }
        havePts = true;
      } else if (port.dataType == "Mesh" && !haveMesh) {
        // DrawMeshUnlit's Mesh input: cook the upstream mesh generator (NGonMesh/QuadMesh) and borrow
        // its vertex+index buffers + face count (mirrors the Texture2D gather; single-frame lifetime).
        const Connection* c = g.connectionToInput(pinId(id, (int)i));
        if (c) {
          uint32_t vtxCount = 0;  // unused here (the VS draws faces×3, not vertices)
          cookMeshInto(pinNode(c->fromPin), inMeshVtx, vtxCount, inMeshIdx, inMeshFaces);
        }
        haveMesh = true;
      } else if (port.dataType == "Texture2D" && !inTex) {
        // FORK#1: a command op (DrawScreenQuad) can take a Texture2D input — recurse into the
        // upstream tex node (same cook-upstream-on-demand as Points). Borrowed, single-frame.
        const Connection* c = g.connectionToInput(pinId(id, (int)i));
        if (c) inTex = cookTexNode(pinNode(c->fromPin), (c->fromPin - 1) % 100);
      } else if (port.dataType == "Command" && !haveInCmd) {
        // S2a KEYSTONE — MultiInput Command collector (TiXL Execute.cs CollectedInputs; full doc in
        // point_ops_execute.cpp): concat N wired chains in wire order → the render island composes. S1:
        // SetRequestedResolution pushes requestedResolution around the subtree (save/restore stops a leak).
        const RenderResolution savedReq = p_->requestedResolution;
        if (n->type == "SetRequestedResolution")
          p_->requestedResolution = resolveSetRequestedResolution(*nodeParams(id), savedReq);
        // S3a context-var scope (TiXL SetFloatVar.cs:26-45 SubGraph branch): push the scoped var into
        // ctxVars BEFORE cooking the subtree, restore AFTER — identical shape to the savedReq guard above.
        // varName comes off the flat Node::strParams String channel (NOT smuggled through a float). When
        // ctxVars is null (~243 golden callers), the node isn't a writer, or -bug skips the write, the
        // scope is inactive (no-op restore).
        CmdVarScope varScope;
        if (!setVarBugSkipWrite() && isCmdContextVarWriter(n->type)) {
          std::string varName;
          auto vit = n->strParams.find("VariableName");
          if (vit != n->strParams.end()) varName = vit->second;
          varScope = cmdVarPush(n->type, *nodeParams(id), varName, ctxVars);
        }
        {
          // S3b LIVE-READ scope: make ctxVars the ambient live map WHILE cooking the SubGraph, so a value-rail
          // GetFloatVar driving a param of a node inside the SubGraph re-resolves LIVE (closes S3a's hollow).
          // Engages only when varScope is active (a real writer push happened); else no-op (leaves outer scope).
          LiveCtxVarScope liveScope(varScope.active ? ctxVars : nullptr);
          // S3b Switch SUB-SELECT (TiXL flow/Switch.cs): gather the wired source node ids in WIRE ORDER first,
          // then cook ONLY the index-th (wrap/negative-safe), -2=all, -1/empty=none. The selection is a
          // cook-core hook here in the SAME collector branch — Execute concats ALL, Switch sub-selects. Non-
          // Switch ops keep the verbatim concat-all (single-input/-bug collapse via the break) below.
          if (n->type == "Switch") {
            std::vector<int> srcIds;  // wired Command sources, wire-declaration order (== Switch.cs CollectedInputs)
            for (const Connection& c : g.connections) {
              if (c.toPin != pinId(id, (int)i)) continue;
              srcIds.push_back(pinNode(c.fromPin));
            }
            const int rawIndex = (int)mapParam(nodeParams(id), "Index", 0.0f);  // C# (int) cast = trunc-toward-0
            // -bug: switchIgnoreIndexForTest() forces the cook-all path (selection lost) — the §3 resident tooth.
            int sel = switchIgnoreIndexForTest() ? kSwitchSelectAll
                                                 : switchSelectIndex(rawIndex, (int)srcIds.size());
            if (sel == kSwitchSelectAll) {
              for (int sid : srcIds) {  // -2: cook ALL (== Execute), wire order
                RenderCommand sub = cookCommand(sid);
                inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
              }
            } else if (sel != kSwitchSelectNone) {  // -1/empty: cook NOTHING (inCmd stays empty)
              RenderCommand sub = cookCommand(srcIds[(size_t)sel]);  // cook ONLY the selected wire
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
            }
          } else {
            for (const Connection& c : g.connections) {  // g.connections = wire order (ListToBuffer :202)
              if (c.toPin != pinId(id, (int)i)) continue;
              RenderCommand sub = cookCommand(pinNode(c.fromPin));
              inCmd.items.insert(inCmd.items.end(), sub.items.begin(), sub.items.end());
              if (!port.multiInput || executeCollectFirstOnlyForTest()) break;  // single-input / -bug collapse
            }
          }
        }
        cmdVarRestore(varScope, ctxVars);    // S3a restore (SetFloatVar.cs:33-40)
        p_->requestedResolution = savedReq;  // restore (SetRequestedResolution.cs:28)
        haveInCmd = true;
      }
    }
    CmdCookCtx cc;
    cc.ctx = &ctx; cc.graph = &g; cc.reg = reg;
    cc.nodeId = id; cc.points = pts; cc.count = cnt;
    cc.inputTexture = inTex;
    cc.inputCommand = haveInCmd ? &inCmd : nullptr;
    cc.ctxVars = ctxVars;  // S3a: a Command op cooked in a SubGraph reads the scoped var off this
    cc.meshVtx = inMeshVtx; cc.meshIdx = inMeshIdx; cc.meshFaceCount = inMeshFaces;
    cc.params = nodeParams(id);
    RenderCommand out = cm->second(cc);
    cmdVisiting.erase(id);  // pop: this node is no longer on the command stack
    return out;
  };

  // Execute a Command chain into target() via a named texture executor. The live target IS the
  // display texture, so the op draws straight into it (batch 3 adds resolution-pinned
  // RenderTarget textures + the Command/Texture2D node ports). Missing executor -> black, no crash.
  auto execIntoTarget = [&](const RenderCommand& chain, const std::string& execType, int nodeId) {
    auto tx = texReg().find(execType);
    if (tx == texReg().end() || !tx->second) { p_->clearTarget(); return; }
    TexCookCtx tc;
    tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
    tc.ctx = &ctx; tc.graph = &g; tc.reg = reg;
    tc.nodeId = nodeId; tc.command = &chain; tc.output = p_->target;
    tc.params = nodeParams(nodeId);
    tx->second(tc);
  };

  // Cook a TEXTURE-flow node (RenderTarget / image-filter / own-tex / cross-frame FEEDBACK) into its OWN
  // resolution-sized texture (ensureTex, keyed by flat id) and return it — the Texture2D gather direct-
  // through (lane I): a filter's Texture2D input is resolved by RECURSIVELY cooking the upstream tex node,
  // exactly how cookNode resolves a Points input by cooking upstream.
  //
  // The body is extracted VERBATIM into PointGraph::Impl::cookFlatTexNode (point_graph_tex_cook.cpp, the
  // Cut-5 extraction — the biggest single flat flow). This cookTexNode std::function slot stays a THIN
  // forwarding lambda so the closure web is untouched: cookNode's Texture2D-into-points gather, cookCommand's
  // Texture2D (FORK#1) gather, the feedback-input recursion, and the terminal dispatch keep calling through
  // the slot. Tex is the MOST cross-wired flat flow (NOT closed), so the method takes the shared cook-stack
  // state by reference: cookCommand (mutual recursion), cookTexNode (the SELF slot the method's own Texture2D
  // gather recurses — ONE closure source, lambda→method→slot→lambda), cookFloatListNode / cookGradientNode
  // (the rail-crossings), the texVisiting cycle-guard, and the feedbackCooked per-frame memo (so the feedback
  // blit + toggle run EXACTLY ONCE per node per frame — see the leaf for the ping-pong toggle parity note).
  std::set<int> texVisiting;  // per-cook cycle guard (a malformed Texture2D cycle must not hang); by-ref
  cookTexNode = [&](int id, int outAbsPort) -> MTL::Texture* {
    return p_->cookFlatTexNode(g, ctx, reg, nodeParams, cookCommand, cookTexNode, cookFloatListNode,
                               cookGradientNode, texVisiting, feedbackCooked, id, outAbsPort);
  };

  // MESH cook flow (4th cook flow = TiXL MeshBuffers). The bodies were extracted VERBATIM into the
  // PointGraph::Impl methods cookFlatMeshNode / cookFlatMeshInto (point_graph_mesh_cook.cpp) — the Cut-6
  // extraction pilot. These cookMeshNode / cookMeshInto std::function slots stay as THIN forwarding
  // lambdas so the closure web is untouched: cookNode (Mesh-into-points seam) + cookCommand (DrawMeshUnlit)
  // keep calling cookMeshInto through the slot. The methods take the minimal shared cook-stack state by
  // ref — the flat graph g, the eval ctx, and the nodeParams memo — because the mesh flow is a CLOSED
  // sub-graph (mesh→mesh only); see point_graph_mesh_cook.cpp for the full doc + verbatim body.
  cookMeshNode = [&](int id) -> bool { return p_->cookFlatMeshNode(g, ctx, nodeParams, id); };
  cookMeshInto = [&](int id, const MTL::Buffer*& vtx, uint32_t& vtxCount, const MTL::Buffer*& idx,
                     uint32_t& faceCount) -> bool {
    return p_->cookFlatMeshInto(g, ctx, nodeParams, id, vtx, vtxCount, idx, faceCount);
  };

  // Cook a FLOATLIST-flow node (5th cook flow = TiXL Slot<List<float>>). Currency = a HOST
  // std::vector<float> in Impl::floatListBuf (no GPU buffer; the op clears + fills it). The walker gathers
  // upstream lists into `inputLists` in spec port order, then dispatches the op. Two input-port kinds feed
  // it: a "FloatList" port → recurse cookFloatListNode per wired source (MultiInput = separate lists, wire-
  // declaration order); a scalar "Float" MultiInput port (FloatsToList.Input) → ALL wired scalars gathered
  // into ONE list via evalFloat (wire order) as one inputLists entry. Gather order = g.connections order
  // (== the resident flatten's extraConns order, resident_eval_flatten.cpp:255-268). Returns the host list
  // (nullptr if not a floatlist op). Body extracted to Impl::cookFlatFloatList (point_graph_hostvalue_cook
  // .cpp, Cut-4); this thin forwarding lambda keeps the closure web intact (FloatList = a CLOSED sub-graph).
  cookFloatListNode = [&](int id) -> const std::vector<float>* {
    return p_->cookFlatFloatList(g, ctx, nodeParams, id);
  };

  // Cook a COLORLIST-flow node (the vec4-list cook flow = TiXL Slot<List<Vector4>>). The currency is a
  // HOST std::vector<simd::float4> living in Impl::colorListBuf (no GPU buffer, no pre-sizing — the op
  // clears + fills it). Mirror of cookFloatListNode over float4 with one extra gather kind for the
  // vec4-as-4-floats MultiInput fork:
  //   • A "ColorList" input port → recurse cookColorListNode on each wired source (MultiInput → one list
  //     per wire, wire-declaration order). (No ColorList-input op ships in this seam, but the gather is
  //     here so a future CombineColorLists consumes it on the same driver.)
  //   • The 4 PARALLEL scalar "Float" MultiInput component ports (Colors.x/.y/.z/.w) → gather each
  //     port's wired scalar sources into its channel via evalFloat, in WIRE-DECLARATION order. The four
  //     channels ride colorScalars[0..3]; ColorsToList zips index i across them into one output color.
  // Gather order = g.connections order (wire-declaration), the same as cookFloatListNode.
  // (Body extracted to PointGraph::Impl::cookFlatColorList in point_graph_hostvalue_cook.cpp — the Cut-4
  // host-value extraction. This slot stays a thin forwarding lambda so the closure web is untouched.
  // ColorList is the ONE non-closed host-value flow: ReadPointColors reads a Points bag back (it recurses
  // cookNode + reads p_->outCount) and a per-frame memo (colorListCooked) stops KeepColors double-running
  // under fan-out. So cookNode (the GPU Points cook slot) + colorListCooked (the cook-local memo) ride in
  // by-ref alongside g / ctx / nodeParams — the same minimal-shared-state contract as nodeParams.)
  cookColorListNode = [&](int id) -> const std::vector<simd::float4>* {
    return p_->cookFlatColorList(g, ctx, nodeParams, cookNode, colorListCooked, id);
  };

  // Cook a GRADIENT-flow node (the 8th cook flow = TiXL Slot<Gradient>). The currency is a HOST
  // SwGradient living in Impl::gradientBuf (no GPU buffer, no pre-sizing — the op writes its steps).
  // VERBATIM clone of cookFloatListNode (std::vector<float> → SwGradient): for each Gradient input
  // port, gather upstream gradients (MultiInput → one per wire, wire-declaration order) into
  // `inputGradients`, then dispatch the op. A pure producer (DefineGradient) has no Gradient input.
  // Returns the cooked host gradient (nullptr if not a gradient op / unknown node).
  // (Body extracted to PointGraph::Impl::cookFlatGradient in point_graph_hostvalue_cook.cpp — the Cut-4
  // host-value extraction. Thin forwarding lambda so cookNode's bake-into-point Gradient gather +
  // cookCommand's Gradient gather + this flow's self-recursion keep calling through the slot. Gradient
  // is a CLOSED sub-graph → minimal shared state: g / ctx / nodeParams by reference.)
  cookGradientNode = [&](int id) -> const SwGradient* {
    return p_->cookFlatGradient(g, ctx, nodeParams, id);
  };

  // Cook a POINTLIST-flow node (the 7th cook flow = TiXL Slot<StructuredList> / StructuredList<Point>).
  // The currency is a HOST std::vector<SwPoint> living in Impl::pointListBuf (no GPU buffer, no pre-
  // sizing — the op clears + fills it). VERBATIM clone of cookFloatListNode (float→SwPoint): for each
  // PointList input port, gather upstream lists (MultiInput → one list per wire, wire-declaration order)
  // into `inputLists`, then dispatch the op. Returns the cooked host list (nullptr if not a pointlist op).
  // (Body extracted to PointGraph::Impl::cookFlatPointList in point_graph_hostvalue_cook.cpp — the Cut-4
  // host-value extraction. Thin forwarding lambda so cookNode's ListToBuffer host→GPU memcpy gather +
  // this flow's self-recursion keep calling through the slot. PointList CROSSES one boundary: PointsToCPU
  // reads a Points bag back, so cookNode rides in by-ref alongside g / ctx / nodeParams.)
  cookPointListNode = [&](int id) -> const std::vector<SwPoint>* {
    return p_->cookFlatPointList(g, ctx, nodeParams, cookNode, id);
  };

  // Cook a STRING-flow node (the 6th cook flow = TiXL Slot<string>). The currency is a HOST
  // std::string living in Impl::stringBuf (no GPU buffer, no pre-sizing — the op assigns it). The
  // walker mirrors cookFloatListNode but the gather is over STRING inputs with the DUAL IDENTITY
  // (the new fork): for each String input port —
  //   • WIRED   → recurse cookStringNode on the upstream source (its cooked string).
  //   • UNWIRED → fall back to the strDef CONST (Node::strParams[id] if the node carries a stored
  //               override, else the spec's PortSpec.strDef). This is the upgrade: a String input
  //               port was a non-wireable const (context-var); here it becomes WIRE-OR-CONST.
  // A MultiInput String port yields one gathered entry PER WIRE (wire-declaration order), and
  // contributes NOTHING when unwired (faithful to GetCollectedTypedInputs — CombineStrings). A
  // single String port always yields exactly one entry (wired value or strDef const).
  // Returns the cooked host string (nullptr if not a string op / unknown node).
  //
  // The FLAT STRING cooks (String + StringList) extracted to PointGraph::Impl methods cookFlatStringNode /
  // cookFlatStringListNode + the SHARED gatherStringInputs (point_graph_string_cook.cpp) — the Cut-2
  // extraction (Cut-4/6 pattern). These cookStringNode / cookStringListNode std::function slots stay as
  // THIN forwarding lambdas so the closure web (gatherStringInputs recurses cookStringNode; the host-scalar
  // branch + StringLength call cookFlatStringNode's gather; cookFlatStringNode reaches cookFloatListNode /
  // cookStringListNode for Sub-seam A) is untouched. The String flow is NOT closed (it crosses into the
  // cookStringNode producer slot, cookFloatListNode, and cookStringListNode), so each rides in by-ref.
  cookStringNode = [&](int id) -> const std::string* {
    return p_->cookFlatStringNode(g, ctx, nodeParams, cookFloatListNode, cookStringListNode, cookStringNode,
                                  id);
  };
  cookStringListNode = [&](int id) -> const std::vector<std::string>* {
    return p_->cookFlatStringListNode(g, ctx, nodeParams, cookStringNode, cookStringListNode, id);
  };

  // The FLAT HOST-SCALAR cooks (cookStringLength = String → host scalar; cookHostScalar = the generalised
  // FloatList/String → Float bridge) extracted to PointGraph::Impl methods cookFlatStringLength /
  // cookFlatHostScalar (point_graph_hostscalar_cook.cpp) — the Cut-3 extraction. cook()-LOCAL forwarding
  // lambdas (only the terminal dispatch below calls them once — no closure web recurses them). Both share
  // the SAME gatherStringInputs Impl method (cookStringNode rides in by-ref → forwarded to the gather);
  // cookFlatHostScalar also takes cookFloatListNode for its FloatList-port gather. (full doc in the leaf.)
  auto cookStringLength = [&](int id) -> void {
    p_->cookFlatStringLength(g, id, cookStringNode);
  };
  auto cookHostScalar = [&](int id) -> void {
    p_->cookFlatHostScalar(g, ctx, nodeParams, cookFloatListNode, cookStringNode, id);
  };

  // FloatList terminal (preview pin on a FloatList-producing op): cook the host list so a test can read
  // it back (debugCookedFloatList) — there is no FloatList VISUALIZER (Slice B = ValuesToTexture turns
  // it into a texture). Clear the viewport (no pixels) so no stale frame, no crash (§5).
  // NOTE: StringLength stores its host scalar in floatListBuf but is NOT a FloatList op — its branch
  // is checked FIRST (below) so it cooks via cookStringLength, not the FloatList walker.
  if (target->type == "StringLength") {
    cookStringLength(target->id);
    p_->clearTarget();
    return;
  }
  // Host-scalar terminal (preview pin on a host-scalar consumer: FloatListLength/PickFloatFromList):
  // cook the scalar (→ floatListBuf 1-elem for debugCookedFloatList readback + Node::outCache for the
  // downstream bridge). No host-scalar VISUALIZER → clear the viewport (§5). Checked BEFORE the
  // FloatList branch (a host-scalar op CONSUMES a FloatList but is NOT a FloatList producer, so it is
  // not in findFloatListOp — this guard is belt-and-suspenders for the explicit terminal path).
  if (findHostScalarOp(target->type)) {
    cookHostScalar(target->id);
    p_->clearTarget();
    return;
  }
  if (findStringOp(target->type)) {
    // String terminal (preview pin on a String-producing op): cook the host string so a test can read
    // it back (debugCookedString). No String VISUALIZER. Clear the viewport (§5).
    cookStringNode(target->id);
    p_->clearTarget();
    return;
  }
  if (findFloatListOp(target->type)) {
    cookFloatListNode(target->id);
    p_->clearTarget();
    return;
  }
  if (findColorListOp(target->type)) {
    // ColorList terminal (preview pin on a ColorList-producing op: ColorsToList): cook the host color
    // list so a test can read it back (debugCookedColorList). No ColorList VISUALIZER (the vec4-list
    // must cross to a texture / point colors to be drawn — out of this seam). Clear the viewport (§5).
    cookColorListNode(target->id);
    p_->clearTarget();
    return;
  }
  if (findStringListOp(target->type)) {
    // StringList terminal (preview pin on a StringList-producing op: SplitString): cook the host list so
    // a test can read it back (debugCookedStringList). No StringList VISUALIZER. Clear the viewport (§5).
    cookStringListNode(target->id);
    p_->clearTarget();
    return;
  }

  // Gradient terminal (preview pin on a Gradient-producing op: DefineGradient): cook the host gradient
  // so a test can read it back (debugCookedGradient). There is no Gradient VISUALIZER (the host
  // gradient must cross to a texture via GradientsToTexture to be drawn). Clear the viewport (no pixels)
  // so no stale frame, no crash (OUTPUT_PIN_VIEWER_CONTRACT §5). Checked here (parallel to FloatList):
  // a gradient op has no cookReg/cmd/tex entry → it would otherwise fall through to the draw/3-flow.
  if (findGradientOp(target->type)) {
    cookGradientNode(target->id);
    p_->clearTarget();
    return;
  }

  // PointList terminal (preview pin on a PointList-producing op: RadialPointsCpu/TransformCpuPoint):
  // cook the host list so a test can read it back (debugCookedPointList). There is no PointList
  // VISUALIZER (the host list must cross to the GPU via ListToBuffer → DrawPoints to be drawn). Clear
  // the viewport (no pixels) so no stale frame, no crash (OUTPUT_PIN_VIEWER_CONTRACT §5). Checked AFTER
  // the Points cook flow below would NOT match (a pointlist op has no cookReg/cmd/tex entry → it falls
  // here); ListToBuffer is a Points op (cmd-consumable), so it does NOT reach this branch.
  if (findPointListOp(target->type)) {
    cookPointListNode(target->id);
    p_->clearTarget();
    return;
  }

  // Mesh terminal (preview pin on a Mesh-producing op): cook the pair so a test can read it back
  // (debugCookedMesh) — there is no Mesh VISUALIZER yet (all Draw*Mesh DEFERRED: camera3d+Layer2d).
  // Clear the viewport (no pixels) so no stale frame, no crash (OUTPUT_PIN_VIEWER_CONTRACT §5).
  if (findMeshOp(target->type)) {
    cookMeshNode(target->id);
    p_->clearTarget();
    return;
  }

  // Legacy draw terminal (PointDrawFn, retired in batch 4): if a draw op is registered for this
  // terminal type, render via it and stop. Production registers NONE — DrawPoints is a cmd op now
  // (point_ops.cpp) — so this branch is dead in the live app (drawReg empty -> falls through to the
  // three-flow below, zero regression). It survives only for golden selftests that capture the
  // final cooked bag by registering a capture-only draw op; the two models coexist until batch 4.
  auto drawIt = drawReg().find(target->type);
  if (drawIt != drawReg().end() && drawIt->second) {
    MTL::Buffer* pts = nullptr;
    uint32_t drawCount = 0;
    for (size_t i = 0; i < ts->ports.size(); ++i) {
      const PortSpec& port = ts->ports[i];
      if (!(port.isInput && port.dataType == "Points")) continue;
      const Connection* c = g.connectionToInput(pinId(target->id, (int)i));
      if (c) {
        pts = cookNode(pinNode(c->fromPin));
        drawCount = p_->outCount[flatKey(pinNode(c->fromPin))];
      }
      break;
    }
    PointCookCtx dc;
    dc.dev = p_->dev; dc.lib = p_->lib; dc.queue = p_->queue;
    dc.ctx = &ctx; dc.graph = &g; dc.reg = reg;
    dc.nodeId = target->id; dc.count = drawCount;
    dc.inputs = nullptr; dc.inputCount = 0; dc.output = nullptr; dc.state = nullptr;
    dc.params = nodeParams(target->id);
    drawIt->second(dc, p_->target, pts);
    return;
  }

  auto texIt = texReg().find(target->type);
  if ((texIt != texReg().end() && texIt->second) || isFeedbackOp(target->type)) {
    // Texture terminal (RenderTarget OR an image filter like Blur OR a feedback op as terminal):
    // cook it (and its upstream tex/command chain) into its OWN texture via the recursive tex
    // walker, and show that. A feedback terminal shows its first Texture2D output (outAbsPort=-1 →
    // ordinal 0). p_->target stays the window-sized fallback for the cmd/preview terminals.
    MTL::Texture* tex = cookTexNode(target->id, -1);
    if (tex) p_->displayTex = tex;  // viewport shows the resolution-sized texture
    else p_->clearTarget();
  } else if (cmdReg().find(target->type) != cmdReg().end()) {
    // Command terminal (DrawPoints): cook its 1-item chain, run the RenderTarget executor.
    RenderCommand chain = cookCommand(target->id);
    execIntoTarget(chain, "RenderTarget", target->id);
  } else {
    // Points-producing terminal (preview pin): synthesize a 1-item chain from the cooked bag.
    // Other output types (ParticleForce/Float) have no visualizer yet -> black, no crash, no
    // stale frame (OUTPUT_PIN_VIEWER_CONTRACT §5).
    MTL::Buffer* out = cookNode(targetNodeId);
    const PortSpec* outPort = nullptr;
    for (const PortSpec& port : ts->ports)
      if (!port.isInput) { outPort = &port; break; }
    if (out && outPort && outPort->dataType == "Points") {
      RenderCommand chain;
      chain.items.push_back(RenderDrawItem{out, p_->outCount[flatKey(targetNodeId)], 3.5f});
      execIntoTarget(chain, "RenderTarget", targetNodeId);
    } else {
      p_->clearTarget();  // no visualizer for this output type yet (§5)
    }
  }
}

// cookResident lives in point_graph_resident.cpp (same Impl via point_graph_internal.h).

}  // namespace sw
