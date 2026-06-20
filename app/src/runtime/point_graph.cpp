#include "runtime/point_graph.h"

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
#include "runtime/floatlist_op_registry.h"     // FloatListCookCtx/findFloatListOp (the 5th cook flow = host List<float>)
#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/pinNode/findSpec
#include "runtime/host_scalar_op_registry.h"   // HostScalarCookCtx/findHostScalarOp (FloatList→Float bridge: list-routing seam)
#include "runtime/image_filter_op_registry.h"  // imageFilterComputeTypes/imageFilterSizeFns (compute leaf seam)
#include "runtime/mesh_op_registry.h"     // MeshCookCtx/findMeshOp (the 4th cook flow = MeshBuffers)
#include "runtime/string_op_registry.h"        // StringCookCtx/findStringOp (the 6th cook flow = host std::string)
#include "runtime/pointlist_op_registry.h"     // PointListCookCtx/findPointListOp (the 7th cook flow = host SwPoint list)
#include "runtime/point_graph_internal.h" // PointGraph::Impl + op registries (shared w/ resident cook)
#include "runtime/tixl_point.h"           // SwPoint (64B) + EvaluationContext (via eval_context.h)

namespace sw {

// The ListToBuffer upload-bridge predicate (pointlist_ops_listtobuffer.cpp). The cook driver detects
// this Points-producing op and takes the PointList-gather + memcpy path (the host→GPU crossing) instead
// of the generic Points-input gather. Forward-declared (defined in the leaf) so both cook driver TUs see it.
bool isListToBufferType(const std::string& type);

using pgdetail::cmdReg;
using pgdetail::cookReg;
using pgdetail::drawReg;
using pgdetail::flatKey;
using pgdetail::isBufferInput;
using pgdetail::texReg;

namespace pgdetail {
std::map<std::string, OpReg>& cookReg() {
  static std::map<std::string, OpReg> m;
  return m;
}
std::map<std::string, PointDrawFn>& drawReg() {
  static std::map<std::string, PointDrawFn> m;
  return m;
}
std::map<std::string, PointCmdFn>& cmdReg() {
  static std::map<std::string, PointCmdFn> m;
  return m;
}
std::map<std::string, PointTexFn>& texReg() {
  static std::map<std::string, PointTexFn> m;
  return m;
}
}  // namespace pgdetail

void registerPointOp(const std::string& type, PointCookFn cook, PointStateNewFn stNew,
                     PointStateFreeFn stFree, PointCountFn countTransform,
                     bool countFromFirstPointsInput) {
  cookReg()[type] =
      pgdetail::OpReg{cook, stNew, stFree, countTransform, countFromFirstPointsInput};
}
void registerDrawOp(const std::string& type, PointDrawFn draw) { drawReg()[type] = draw; }
void registerCmdOp(const std::string& type, PointCmdFn cmd) { cmdReg()[type] = cmd; }
void registerTexOp(const std::string& type, PointTexFn tex) { texReg()[type] = tex; }

// OWN-TEXTURE sink (Slice B tex-output fork). Meyers singleton; populated by ValuesToTexture's
// registrar during pre-main dynamic init, read live by the tex-walker. A type in this set takes the
// ownTexHost/W/H staging path (ensureOwnedTex, R32Float) instead of the ensureTex RGBA8 output.
static std::set<std::string>& texOwnsOutputSink() {
  static std::set<std::string> s;
  return s;
}
void registerTexOpOwnsOutput(const std::string& type) { texOwnsOutputSink().insert(type); }
bool texOpOwnsOutput(const std::string& type) { return texOwnsOutputSink().count(type) != 0; }

// registerBuiltinPointOps() is defined in point_ops.cpp (the real operators).

// --- resolved-param accessors (the slice-2b seam; PointCookCtx::params docs) ---
namespace {
float mapParam(const std::map<std::string, float>* m, const char* id, float def) {
  if (!m) return def;
  auto it = m->find(id);
  return it != m->end() ? it->second : def;
}
void mapVecN(const std::map<std::string, float>* m, const char* base, const float* fallback,
             int n, float* out) {
  static const char* kSuffix[4] = {".x", ".y", ".z", ".w"};
  for (int i = 0; i < n && i < 4; ++i)
    out[i] = mapParam(m, (std::string(base) + kSuffix[i]).c_str(), fallback[i]);
}
}  // namespace

float cookParam(const PointCookCtx& c, const char* id, float def) { return mapParam(c.params, id, def); }
float cookParam(const CmdCookCtx& c, const char* id, float def) { return mapParam(c.params, id, def); }
float cookParam(const TexCookCtx& c, const char* id, float def) { return mapParam(c.params, id, def); }
void cookVecN(const PointCookCtx& c, const char* base, const float* fallback, int n, float* out) {
  mapVecN(c.params, base, fallback, n, out);
}
void cookVecN(const TexCookCtx& c, const char* base, const float* fallback, int n, float* out) {
  mapVecN(c.params, base, fallback, n, out);
}
void cookVecN(const CmdCookCtx& c, const char* base, const float* fallback, int n, float* out) {
  mapVecN(c.params, base, fallback, n, out);
}
float cookInputParam(const PointCookCtx& c, int input, const char* id, float def) {
  if (!c.inputParams || input < 0 || input >= c.inputCount) return def;
  return mapParam(c.inputParams[input], id, def);
}

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

uint32_t PointGraph::debugCookedCount(int nodeId) const {
  auto it = p_->outCount.find(flatKey(nodeId));
  return it != p_->outCount.end() ? it->second : 0u;
}

const MTL::Buffer* PointGraph::debugCookedBuffer(int nodeId) const {
  auto it = p_->outBuf.find(flatKey(nodeId));
  return it != p_->outBuf.end() ? it->second : nullptr;
}

bool PointGraph::debugCookedMesh(int nodeId, const MTL::Buffer*& vtx, uint32_t& vtxCount,
                                 const MTL::Buffer*& idx, uint32_t& idxCount) const {
  const std::string key = flatKey(nodeId);
  auto vb = p_->meshVtxBuf.find(key);
  auto ib = p_->meshIdxBuf.find(key);
  if (vb == p_->meshVtxBuf.end() || ib == p_->meshIdxBuf.end() || !vb->second || !ib->second)
    return false;
  vtx = vb->second;
  idx = ib->second;
  vtxCount = p_->meshVtxCount.count(key) ? p_->meshVtxCount[key] : 0u;
  idxCount = p_->meshIdxCount.count(key) ? p_->meshIdxCount[key] : 0u;
  return true;
}

const std::vector<float>* PointGraph::debugCookedFloatList(int nodeId) const {
  auto it = p_->floatListBuf.find(flatKey(nodeId));
  return it != p_->floatListBuf.end() ? &it->second : nullptr;
}

const std::string* PointGraph::debugCookedString(int nodeId) const {
  auto it = p_->stringBuf.find(flatKey(nodeId));
  return it != p_->stringBuf.end() ? &it->second : nullptr;
}

const std::vector<SwPoint>* PointGraph::debugCookedPointList(int nodeId) const {
  auto it = p_->pointListBuf.find(flatKey(nodeId));
  return it != p_->pointListBuf.end() ? &it->second : nullptr;
}

int PointGraph::defaultDrawTarget(const Graph& g) const {
  // The terminal is the most-downstream realizable node: a tex node (RenderTarget/Blur) wins, else
  // a DrawPoints (Command). With image filters (lane I) a graph can hold SEVERAL tex nodes chained
  // (RenderTarget -> Blur -> ...); the live terminal must be the LAST one — the tex node whose own
  // output is not consumed by another node (a sink). Falling back to the first tex node would show
  // the un-filtered RenderTarget and make every image filter invisible in the live app.
  auto outputConsumed = [&](int id) {
    for (const Connection& c : g.connections)
      if (pinNode(c.fromPin) == id) return true;
    return false;
  };
  int firstTex = 0;
  for (const Node& n : g.nodes)
    if (texReg().find(n.type) != texReg().end()) {
      if (!firstTex) firstTex = n.id;
      if (!outputConsumed(n.id)) return n.id;  // a sink tex node = the real terminal
    }
  if (firstTex) return firstTex;  // all tex nodes feed each other (cycle): fall back to the first
  for (const Node& n : g.nodes)
    if (cmdReg().find(n.type) != cmdReg().end()) return n.id;
  // Legacy draw terminal (PointDrawFn, retired in batch 4): production registers none, but
  // self-contained golden selftests register a capture-only draw op as their terminal — keep it
  // discoverable so cook() can dispatch it, until the draw model is fully retired.
  for (const Node& n : g.nodes)
    if (drawReg().find(n.type) != drawReg().end()) return n.id;
  return 0;
}

int PointGraph::defaultDrawTarget(const SymbolLibrary& lib, const std::string& symbolId) const {
  // Same terminal priority as the flat overload, scanning one symbol's children. With chained tex
  // nodes (RenderTarget -> Blur), prefer the SINK — the tex child whose output no connection
  // consumes — so the live viewport shows the last filter, not the un-filtered RenderTarget.
  const Symbol* s = lib.find(symbolId);
  if (!s) return 0;
  auto outputConsumed = [&](int id) {
    for (const SymbolConnection& c : s->connections)
      if (c.srcChild == id) return true;
    return false;
  };
  int firstTex = 0;
  for (const SymbolChild& c : s->children)
    if (texReg().find(c.symbolId) != texReg().end()) {
      if (!firstTex) firstTex = c.id;
      if (!outputConsumed(c.id)) return c.id;
    }
  if (firstTex) return firstTex;
  for (const SymbolChild& c : s->children)
    if (cmdReg().find(c.symbolId) != cmdReg().end()) return c.id;
  for (const SymbolChild& c : s->children)
    if (drawReg().find(c.symbolId) != drawReg().end()) return c.id;
  return 0;
}

void PointGraph::cook(const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
                      int targetNodeId) {
  p_->displayTex = nullptr;  // default: target() shows the window-sized texture (cmd/preview paths)
  const Node* target = g.node(targetNodeId);
  const NodeSpec* ts = target ? findSpec(target->type) : nullptr;
  if (!target || !ts) { p_->clearTarget(); return; }  // no/unknown target -> black, no crash

  std::map<int, MTL::Buffer*> cooked;  // this-frame memo (cook each node once)

  // Per-node resolved Float params (the 2b seam): resolved ONCE per node per cook through the
  // full value spine (override → binding → wire → stored → default, graph.cpp), then handed to
  // the op via PointCookCtx::params. Stored in a node-keyed memo so pointers stay stable for
  // the whole cook (ops + inputParams point into it).
  std::map<int, std::map<std::string, float>> paramsMemo;
  std::function<const std::map<std::string, float>*(int)> nodeParams =
      [&](int id) -> const std::map<std::string, float>* {
    auto it = paramsMemo.find(id);
    if (it != paramsMemo.end()) return &it->second;
    const Node* n = g.node(id);
    if (!n) return nullptr;
    return &(paramsMemo[id] = resolveNodeParams(g, *n, ctx, reg));
  };

  // Forward-declared (body below): the PointList cook flow (7th flow = host std::vector<SwPoint>). A
  // pointlist op (RadialPointsCpu/TransformCpuPoint) gathers its PointList inputs by recursing into
  // cookPointListNode; the ListToBuffer bridge (a Points op, cookNode below) also calls it to gather the
  // host list it memcpys to the GPU. Declared HERE (before cookNode) so cookNode's ListToBuffer branch
  // can call it; std::function breaks the ordering (the body is assigned after cookNode is defined).
  std::function<const std::vector<SwPoint>*(int)> cookPointListNode;

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

    const std::map<std::string, float>* params = nodeParams(id);

    // count: a "Count" Float input (generators) resolved through the value spine (the resolved
    // map already holds wire/stored/default — the 7d4b34e contract), else the sum of all wired
    // Points inputs (modifier passes through, combine concatenates).
    // Default: sum of Points inputs (combine concatenates). Ops that transform a primary bag
    // using extra Points inputs as references (SnapToPoints) opt into first-input-count instead.
    uint32_t count = sumPointsCount;
    if (auto cr = cookReg().find(n->type);
        cr != cookReg().end() && cr->second.countFromFirstPointsInput)
      count = firstPointsCount;
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

  // Forward-declared so cookCommand can gather a Texture2D input (FORK#1) by recursing into the
  // upstream tex node — cookTexNode's body is assigned below (it in turn calls cookCommand for its
  // Command inputs, so the two are mutually recursive; std::function breaks the ordering cycle).
  std::function<MTL::Texture*(int)> cookTexNode;
  // Forward-declared too: cookTexNode (a tex op like ValuesToTexture, the rail-crossing) gathers its
  // FloatList inputs by recursing into cookFloatListNode, whose body is assigned further below. Same
  // std::function ordering-break as cookTexNode itself.
  std::function<const std::vector<float>*(int)> cookFloatListNode;
  // Forward-declared: the String cook flow (6th flow = host std::string). A string op (CombineStrings/
  // StringLength) gathers its String inputs by recursing into cookStringNode (a wired String input);
  // body assigned below. std::function breaks the self-recursion ordering (CombineStrings ← upstream
  // FloatToString ← … all String producers). StringLength produces a Float output (int→Float), so it
  // is NOT a string op — it does NOT register a StringCookFn (see leaf comment); it is cooked by the
  // value-eval path. Only producers of a String output ride cookStringNode.
  std::function<const std::string*(int)> cookStringNode;
  // Forward-declared: cookCommand gathers a Mesh input (DrawMeshUnlit) by cooking the upstream mesh
  // node here, then reading its buffers via debugCookedMesh. Body assigned below (it has no recursion
  // into cookCommand — a mesh generator owns no command inputs — but the ordering break keeps it
  // callable from cookCommand). Returns true + fills the out-params on success.
  std::function<bool(int, const MTL::Buffer*&, uint32_t&, const MTL::Buffer*&, uint32_t&)> cookMeshInto;
  // Forward-declared (mesh-input seam): the MESH cook flow (4th flow = MeshBuffers pair). A mesh op
  // (generator OR consumer) is cooked here; a CONSUMER (TransformMesh/CombineMeshes) gathers its Mesh
  // input(s) by RECURSING into cookMeshNode (via cookMeshInto). std::function so the body can self-
  // recurse during the input gather. Returns true if a mesh op cooked into meshVtxBuf/meshIdxBuf.
  std::function<bool(int)> cookMeshNode;

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
        if (c) inTex = cookTexNode(pinNode(c->fromPin));
      } else if (port.dataType == "Command" && !haveInCmd) {
        // Cut 3: a command op (Camera) can WRAP a Command subtree — recurse into the upstream command
        // node and hand the cooked chain in via cc.inputCommand. Mirrors the Texture2D gather above.
        const Connection* c = g.connectionToInput(pinId(id, (int)i));
        if (c) inCmd = cookCommand(pinNode(c->fromPin));
        haveInCmd = true;
      }
    }
    CmdCookCtx cc;
    cc.ctx = &ctx; cc.graph = &g; cc.reg = reg;
    cc.nodeId = id; cc.points = pts; cc.count = cnt;
    cc.inputTexture = inTex;
    cc.inputCommand = haveInCmd ? &inCmd : nullptr;
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

  // Cook a TEXTURE-flow node (RenderTarget executor OR an image filter like Blur) into its OWN
  // resolution-sized texture (ensureTex, keyed by flat id) and return it. This is the Texture2D
  // gather direct-through (lane I): a filter's Texture2D input is resolved by RECURSIVELY cooking
  // the upstream tex node here — exactly how cookNode resolves a Points input by cooking upstream.
  //   • Command inputs  → concatenated into one chain (RenderTarget executes a draw chain).
  //   • Texture2D input → the first wired one is cooked here and handed in via tc.inputTexture
  //     (an image filter samples it; RenderTarget has no Texture2D input so this stays null).
  // Cycle-safe via the `cooked` Points memo is NOT shared here, so guard with a per-cook visiting
  // set: a Texture2D cycle would otherwise recurse forever (the canvas forbids cycles, but cook
  // must not hang if a malformed graph slips one in).
  std::set<int> texVisiting;
  cookTexNode = [&](int id) -> MTL::Texture* {
    const Node* n = g.node(id);
    const NodeSpec* s = n ? findSpec(n->type) : nullptr;
    if (!n || !s) return nullptr;
    auto tx = texReg().find(n->type);
    if (tx == texReg().end() || !tx->second) return nullptr;
    if (!texVisiting.insert(id).second) return nullptr;  // cycle guard: already on the stack

    const std::map<std::string, float>* tp = nodeParams(id);

    // Gather inputs in spec order FIRST (moved ahead of output sizing): concat Command inputs,
    // recurse EACH Texture2D input (lane D2: Displace needs Image + DisplaceMap; texInputs[] indexes
    // by Texture2D-input port order). Gathering before sizing is harmless for pixel ops (they size
    // off the Resolution pin and ignore the input dims) — done UNCONDITIONALLY so the one code path
    // can hand a compute leaf's SizeFn the cooked INPUT size (Crop's output = input minus margins).
    RenderCommand chain;
    const MTL::Texture* texInputs[TexCookCtx::kMaxTexInputs] = {nullptr, nullptr, nullptr, nullptr};
    int texInputCount = 0;
    // FloatList inputs (Slice B rail-crossing): a tex op with "FloatList" input ports (ValuesToTexture)
    // gathers its upstream host lists here, in spec port order with MultiInput ports expanded into
    // wire-declaration order — the SAME gather contract as cookFloatListNode. Empty for every existing
    // tex op (no FloatList port) → tc.inputLists stays null → byte-identical path.
    std::vector<std::vector<float>> floatListInputs;
    bool hasFloatListInput = false;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!port.isInput) continue;
      if (port.dataType == "Command") {
        const Connection* c = g.connectionToInput(pinId(id, (int)i));
        if (!c) continue;
        RenderCommand up = cookCommand(pinNode(c->fromPin));
        chain.items.insert(chain.items.end(), up.items.begin(), up.items.end());
      } else if (port.dataType == "Texture2D") {
        int slot = texInputCount;  // wired or not, this port occupies the next Texture2D slot
        if (slot < TexCookCtx::kMaxTexInputs) {
          const Connection* c = g.connectionToInput(pinId(id, (int)i));
          texInputs[slot] = c ? cookTexNode(pinNode(c->fromPin)) : nullptr;
          texInputCount = slot + 1;
        }
      } else if (port.dataType == "FloatList") {
        hasFloatListInput = true;
        // Recurse each wired FloatList source. MultiInput → every wire is a separate gathered list, in
        // wire-declaration order (g.connections order); single-input → at most one. Mirrors the
        // cookFloatListNode FloatList-port gather exactly.
        for (const Connection& c : g.connections) {
          if (c.toPin != pinId(id, (int)i)) continue;
          const std::vector<float>* up = cookFloatListNode(pinNode(c.fromPin));
          floatListInputs.push_back(up ? *up : std::vector<float>{});
          if (!port.multiInput) break;
        }
      }
    }

    // OWN-TEXTURE fork (Slice B): a tex op that allocates its OWN data-sized, non-RGBA8 texture
    // (ValuesToTexture: R32Float, dims = sampleCount × listCount) does NOT use ensureTex (RGBA8 +
    // resolution-pinned). The op computes its dims + writes a host float buffer into ownTexHost; the
    // DRIVER then sizes the op-owned texture via ensureOwnedTex (parked in texBuf → released on
    // realloc + in ~PointGraph, no leak) and uploads. This is the FIRST non-RGBA8, non-resolution-
    // pinned texture in the engine. Every other tex op skips this branch entirely (byte-identical).
    if (texOpOwnsOutput(n->type)) {
      std::vector<float> hostOut;
      uint32_t ow = 0, oh = 0;
      TexCookCtx tc;
      tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
      tc.ctx = &ctx; tc.graph = &g; tc.reg = reg;
      tc.nodeId = id; tc.command = &chain;
      tc.inputLists = hasFloatListInput ? &floatListInputs : nullptr;
      tc.ownTexHost = &hostOut; tc.ownTexW = &ow; tc.ownTexH = &oh;
      tc.params = tp;
      tx->second(tc);
      texVisiting.erase(id);
      // The op returned no data (empty lists / pow guard / nothing to upload) → no texture this cook.
      if (ow == 0 || oh == 0 || hostOut.size() < (size_t)ow * oh) return nullptr;
      // R32Float: 4 bytes/texel, one float channel — the op's chosen format (TiXL Format.R32_Float,
      // ValuesToTexture.cs:120). ensureOwnedTex keys realloc on (w,h,fmt).
      MTL::Texture* owned =
          p_->ensureOwnedTex(flatKey(id), ow, oh, MTL::PixelFormatR32Float);
      if (owned)
        owned->replaceRegion(MTL::Region::Make2D(0, 0, ow, oh), 0, hostOut.data(),
                             (NS::UInteger)ow * sizeof(float));
      return owned;
    }

    // Size this node's own output texture. Default = the Resolution pin (the window size is
    // WindowFollow's source; image filters with no Resolution param fall back to WindowFollow).
    // A COMPUTE leaf may override via its SizeFn (Crop: output = inputSize - margins): the output
    // can be SMALLER/larger than the Resolution pin, so we compute it from the cooked input dims.
    RenderResolution res = resolveRenderResolution(
        tp ? *tp : std::map<std::string, float>{}, RenderResolution{p_->width, p_->height});
    auto sizeIt = imageFilterSizeFns().find(n->type);
    if (sizeIt != imageFilterSizeFns().end() && texInputs[0]) {
      res = sizeIt->second(tp ? *tp : std::map<std::string, float>{},
                           RenderResolution{(uint32_t)texInputs[0]->width(),
                                            (uint32_t)texInputs[0]->height()});
    }
    // Compute leaves write via RWTexture2D -> their output needs MTL::TextureUsageShaderWrite.
    bool needsWrite = imageFilterComputeTypes().count(n->type) != 0;
    // Mipped-output ops carry a full mip pyramid on their output (allocated by ensureTex) so a
    // downstream consumer can sample(uv, level(lod)). The mip-WRITE (generateMipmaps blit) happens
    // AFTER the leaf fills level 0 (below).
    bool needsMips = imageFilterMippedOutputTypes().count(n->type) != 0;
    MTL::Texture* tex = p_->ensureTex(flatKey(id), res.w, res.h, needsWrite, needsMips);

    TexCookCtx tc;
    tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
    tc.ctx = &ctx; tc.graph = &g; tc.reg = reg;
    tc.nodeId = id; tc.command = &chain; tc.output = tex;
    for (int k = 0; k < texInputCount; ++k) tc.inputTextures[k] = texInputs[k];
    tc.inputTextureCount = texInputCount;
    tc.inputTexture = texInputs[0];  // back-compat: single-input ops (Blur) read inputTexture
    tc.inputLists = hasFloatListInput ? &floatListInputs : nullptr;  // null for every existing tex op
    // ASSET texture ((E)-seam phase 2): if this op type declared an asset key, decode-and-cache it
    // ONCE (cachedAssetTexture memoizes; NO per-frame decode) and bind via tc.assetTexture. Absent
    // type = tc.assetTexture stays null -> every existing op's path is byte-identical.
    {
      auto ai = imageFilterAssetTextures().find(n->type);
      if (ai != imageFilterAssetTextures().end())
        tc.assetTexture = cachedAssetTexture(p_->dev, ai->second, /*mipped=*/false);
    }
    tc.params = tp;
    tx->second(tc);
    // mip-WRITE: the leaf cook committed+waited internally, so level 0 is ready. Fill levels 1..N
    // by a blit generateMipmaps (NOT a shader — pattern from point_ops_combinebuffers.cpp). A
    // separate command buffer; commit+wait so a same-frame downstream sample(level(lod)) sees mips.
    if (needsMips && tex) {
      MTL::CommandBuffer* mc = p_->queue->commandBuffer();
      MTL::BlitCommandEncoder* blit = mc->blitCommandEncoder();
      blit->generateMipmaps(tex);
      blit->endEncoding();
      mc->commit();
      mc->waitUntilCompleted();
    }
    texVisiting.erase(id);
    return tex;
  };

  // Cook a MESH-flow node (the 4th cook flow = TiXL MeshBuffers). A mesh GENERATOR (NGonMesh/QuadMesh)
  // owns NO Mesh input — it computes its vertex+index counts from its own params. A mesh CONSUMER
  // (TransformMesh in×1 / CombineMeshes MultiInput, the mesh-input seam) first GATHERS its wired Mesh
  // input(s): for each Mesh input port (spec order; MultiInput expands every wire in connection order),
  // recurse cookMeshNode on the source then read its cooked pair via debugCookedMesh into a SwMeshView.
  // The gather mirrors the cookPointListNode walk (input-dependent currency, borrowed single-frame).
  // Then countFn(params, views, n) decides the OUTPUT sizes (generator ignores views; consumer reads
  // them), the driver sizes the OWNED pair (ensureMesh, count-change reuse), and the op writes both
  // buffers via contents(). The currency is a PAIR (SwVertex + SwTriIndex). Returns true if a mesh op
  // cooked (the buffers are then in p_->meshVtxBuf/meshIdxBuf, readable via debugCookedMesh).
  //
  // ★ Source meshes are cooked into meshVtxBuf[srcKey] BEFORE this node's ensureMesh(flatKey(id),...).
  // ensureMesh keys by flat id, so a consumer's own pair never aliases its sources' pairs — the borrowed
  // SwMeshView buffers stay valid across this node's ensureMesh (different map keys, no realloc churn).
  cookMeshNode = [&](int id) -> bool {
    const Node* n = g.node(id);
    if (!n) return false;
    const MeshOpReg* reg = findMeshOp(n->type);
    if (!reg || !reg->cook || !reg->count) return false;

    // Gather upstream Mesh inputs (spec port order; MultiInput → one view per wire, connection order).
    std::vector<SwMeshView> inputMeshes;
    const NodeSpec* s = findSpec(n->type);
    if (s) {
      for (size_t i = 0; i < s->ports.size(); ++i) {
        const PortSpec& port = s->ports[i];
        if (!(port.isInput && port.dataType == "Mesh")) continue;
        const int inPin = pinId(id, (int)i);
        for (const Connection& c : g.connections) {
          if (c.toPin != inPin) continue;
          const int srcId = pinNode(c.fromPin);
          SwMeshView v;
          if (cookMeshNode(srcId))  // cook the source pair into meshVtxBuf/meshIdxBuf[srcKey]
            debugCookedMesh(srcId, v.vtx, v.vtxCount, v.idx, v.faceCount);
          inputMeshes.push_back(v);   // an unwired/non-mesh source pushes an empty view (faithful no-op)
          if (!port.multiInput) break;  // single-input: first wire only
        }
      }
    }

    const std::map<std::string, float>* mp = nodeParams(id);
    uint32_t vtxCount = 0, idxCount = 0;
    // count FIRST: generator ignores the views; consumer reads them (TransformMesh inputs[0]; CombineMeshes Σ).
    reg->count(mp, inputMeshes.data(), (int)inputMeshes.size(), vtxCount, idxCount);

    MTL::Buffer* vb = nullptr;
    MTL::Buffer* ib = nullptr;
    p_->ensureMesh(flatKey(id), vtxCount, idxCount, vb, ib);  // size the owned pair before cook

    MeshCookCtx mc;
    mc.dev = p_->dev; mc.lib = p_->lib; mc.queue = p_->queue;
    mc.ctx = &ctx; mc.nodeId = id;
    mc.vertexCount = vtxCount; mc.indexCount = idxCount;
    mc.output_vertices = vb; mc.output_indices = ib;
    mc.inputMeshes = inputMeshes.data(); mc.inputMeshCount = (int)inputMeshes.size();
    mc.params = mp;
    reg->cook(mc);
    return true;
  };

  // Bridge: cook the upstream mesh node, then read its buffers back via debugCookedMesh (so a Mesh-
  // consuming command op — DrawMeshUnlit — can borrow them). Assigned to the forward-declared
  // std::function so cookCommand (defined ABOVE this point) can call it. Returns false (and leaves the
  // out-params untouched) if the node is not a mesh op / produced nothing.
  cookMeshInto = [&](int id, const MTL::Buffer*& vtx, uint32_t& vtxCount, const MTL::Buffer*& idx,
                     uint32_t& faceCount) -> bool {
    if (!cookMeshNode(id)) return false;  // cook the generator into meshVtxBuf/meshIdxBuf (or no-op)
    return debugCookedMesh(id, vtx, vtxCount, idx, faceCount);
  };

  // Cook a FLOATLIST-flow node (the 5th cook flow = TiXL Slot<List<float>>). The currency is a HOST
  // std::vector<float> living in Impl::floatListBuf (no GPU buffer, no pre-sizing — the op clears +
  // fills it). The walker mirrors cookMeshNode but with an INPUT GATHER (cloned from cookNode's
  // buffer-input loop): for each input port, gather upstream lists into `inputLists` (spec port
  // order), then dispatch the op. Two input-port kinds feed `inputLists`:
  //   • A "FloatList" input port → recurse cookFloatListNode on each wired source. If the port is a
  //     MultiInput, ALL wired sources are gathered as SEPARATE lists, in WIRE-DECLARATION order.
  //   • A scalar "Float" MultiInput port (e.g. FloatsToList.Input) → gather ALL wired scalar sources
  //     into ONE list via evalFloat, in WIRE-DECLARATION order; that single list becomes one
  //     inputLists entry. (Producers like FloatsToList read inputLists[0] = their scalar inputs.)
  // Gather order = the order connections appear in g.connections (the wire-declaration order), which
  // is the SAME ordering the resident flatten uses for extraConns (resident_eval_flatten.cpp:255-268).
  // Returns the cooked host list (nullptr if not a floatlist op / unknown node).
  cookFloatListNode = [&](int id) -> const std::vector<float>* {
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
          const std::vector<float>* up = cookFloatListNode(pinNode(c.fromPin));
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

    std::vector<float>& out = p_->floatListBuf[flatKey(id)];
    FloatListCookCtx fc;
    fc.dev = p_->dev; fc.lib = p_->lib; fc.queue = p_->queue;
    fc.ctx = &ctx; fc.nodeId = id;
    fc.inputLists = &inputLists;
    fc.output = &out;
    fc.params = nodeParams(id);
    (*fn)(fc);
    return &out;
  };

  // Cook a POINTLIST-flow node (the 7th cook flow = TiXL Slot<StructuredList> / StructuredList<Point>).
  // The currency is a HOST std::vector<SwPoint> living in Impl::pointListBuf (no GPU buffer, no pre-
  // sizing — the op clears + fills it). VERBATIM clone of cookFloatListNode (float→SwPoint): for each
  // PointList input port, gather upstream lists (MultiInput → one list per wire, wire-declaration order)
  // into `inputLists`, then dispatch the op. Returns the cooked host list (nullptr if not a pointlist op).
  cookPointListNode = [&](int id) -> const std::vector<SwPoint>* {
    const Node* n = g.node(id);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->type);
    if (!s) return nullptr;
    const PointListCookFn* fn = findPointListOp(n->type);
    if (!fn || !*fn) return nullptr;

    std::vector<std::vector<SwPoint>> inputLists;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!(port.isInput && port.dataType == "PointList")) continue;
      const int inPin = pinId(id, (int)i);
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        const std::vector<SwPoint>* up = cookPointListNode(pinNode(c.fromPin));
        inputLists.push_back(up ? *up : std::vector<SwPoint>{});
        if (!port.multiInput) break;  // single-input: first wire only
      }
    }

    std::vector<SwPoint>& out = p_->pointListBuf[flatKey(id)];
    PointListCookCtx pc;
    pc.dev = p_->dev; pc.lib = p_->lib; pc.queue = p_->queue;
    pc.ctx = &ctx; pc.nodeId = id;
    pc.inputLists = &inputLists;
    pc.output = &out;
    pc.params = nodeParams(id);
    (*fn)(pc);
    return &out;
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
  // Shared string-input gather (used by cookStringNode for string PRODUCERS and by the StringLength
  // branch below, which is a string CONSUMER producing a host scalar — so it does NOT register a
  // StringCookFn). `s` is the consuming node's spec; collects in spec port order.
  std::function<std::vector<std::string>(int, const NodeSpec&)> gatherStringInputs =
      [&](int id, const NodeSpec& s) -> std::vector<std::string> {
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
  };

  cookStringNode = [&](int id) -> const std::string* {
    const Node* n = g.node(id);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->type);
    if (!s) return nullptr;
    const StringCookFn* fn = findStringOp(n->type);
    if (!fn || !*fn) return nullptr;

    std::vector<std::string> inputStrings = gatherStringInputs(id, *s);

    std::string& out = p_->stringBuf[flatKey(id)];
    StringCookCtx sc;
    sc.dev = p_->dev; sc.lib = p_->lib; sc.queue = p_->queue;
    sc.ctx = &ctx; sc.nodeId = id;
    sc.inputStrings = &inputStrings;
    sc.output = &out;
    sc.params = nodeParams(id);
    (*fn)(sc);
    return &out;
  };

  // StringLength: the FIRST cross-rail consumer (String input → host scalar output). TiXL's
  // Length is a Slot<int> (StringLength.cs:16 Length.Value = InputString.GetValue(context).Length);
  // sw dissolves int→Float (fork-int-bool-dissolve-to-float, Cut32 convention). It is NOT a string
  // PRODUCER, so it has no StringCookFn — it is cooked by THIS branch on the flat path, resolving
  // its String input via the shared gather, and stores the length as a 1-element host FloatList
  // (Impl::floatListBuf — the ONLY existing host-scalar channel; readback via debugCookedFloatList).
  // fork-stringlength-host-scalar-via-floatlist: the downstream Float-RAIL bridge (FloatList→Float
  // value) is the separate list-routing seam (SEAM_COMPLETION_PLAN §2 stage 1), deferred — so here
  // we transport the host value, not yet feed it into a Float input port.
  auto cookStringLength = [&](int id) -> void {
    const Node* n = g.node(id);
    const NodeSpec* s = n ? findSpec(n->type) : nullptr;
    if (!s) return;
    std::vector<std::string> inputStrings = gatherStringInputs(id, *s);
    // StringLength has exactly one String input ("InputString") → inputStrings[0].
    size_t len = inputStrings.empty() ? 0 : inputStrings[0].size();
    std::vector<float>& out = p_->floatListBuf[flatKey(id)];
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
  };

  // Cook a HOST-SCALAR consumer node (FloatList/String input → ONE host Float): the FloatList→Float
  // BRIDGE (list-routing seam, SEAM_COMPLETION_PLAN §2 stage 1). GENERALISES cookStringLength to ANY
  // op registered in the host-scalar registry (FloatListLength / PickFloatFromList / …). It gathers
  // the node's inputs by spec port dataType — each "FloatList" port via cookFloatListNode (MultiInput
  // → one gathered list per wire, wire-declaration order), each "String" port via gatherStringInputs
  // (wire-OR-const) — runs the op to compute the scalar, then stores it BOTH in floatListBuf (1-elem,
  // the transport: debugCookedFloatList readback) AND in Node::outCache[0] (the BRIDGE: evalFloat
  // reads it). The op's Float params are resolved through the value spine (nodeParams), so e.g.
  // PickFloatFromList.Index is drivable. Mirrors cookStringLength's const_cast outCache write (R-1).
  auto cookHostScalar = [&](int id) -> void {
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
    std::vector<std::string> inputStrings = gatherStringInputs(id, *s);

    float scalar = 0.0f;
    HostScalarCookCtx hc;
    hc.dev = p_->dev; hc.lib = p_->lib; hc.queue = p_->queue;
    hc.ctx = &ctx; hc.nodeId = id;
    hc.inputLists = &inputLists;
    hc.inputStrings = &inputStrings;
    hc.params = nodeParams(id);
    hc.output = &scalar;
    (*fn)(hc);

    // Transport (legacy floatListBuf 1-elem) + BRIDGE (Node::outCache[0], const_cast — same precedent
    // as cookStringLength). A host-scalar op's injectBug corrupts `scalar` IN the cook → both channels
    // carry the corrupted value → the downstream evalFloat RED bites on the real path.
    std::vector<float>& out = p_->floatListBuf[flatKey(id)];
    out.assign(1, scalar);
    if (Node* mn = const_cast<Graph&>(g).node(id)) mn->outCache[0] = scalar;
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
  if (texIt != texReg().end() && texIt->second) {
    // Texture terminal (RenderTarget OR an image filter like Blur): cook it (and its upstream
    // tex/command chain) into its OWN resolution-sized texture via the recursive tex walker, and
    // show that. p_->target stays the window-sized fallback for the cmd/preview terminals.
    MTL::Texture* tex = cookTexNode(target->id);
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
