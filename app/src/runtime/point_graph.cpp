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
#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/pinId/pinNode/findSpec
#include "runtime/image_filter_op_registry.h"  // imageFilterComputeTypes/imageFilterSizeFns (compute leaf seam)
#include "runtime/point_graph_internal.h" // PointGraph::Impl + op registries (shared w/ resident cook)
#include "runtime/tixl_point.h"           // SwPoint (64B) + EvaluationContext (via eval_context.h)

namespace sw {

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

  std::function<MTL::Buffer*(int)> cookNode = [&](int id) -> MTL::Buffer* {
    auto m = cooked.find(id);
    if (m != cooked.end()) return m->second;
    const Node* n = g.node(id);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->type);
    if (!s) return nullptr;

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

  // Cook a command node: resolve its upstream Points bag, then call its cmd fn -> RenderCommand.
  auto cookCommand = [&](int id) -> RenderCommand {
    RenderCommand rc;
    const Node* n = g.node(id);
    const NodeSpec* s = n ? findSpec(n->type) : nullptr;
    if (!n || !s) return rc;
    auto cm = cmdReg().find(n->type);
    if (cm == cmdReg().end() || !cm->second) return rc;
    MTL::Buffer* pts = nullptr;
    uint32_t cnt = 0;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!(port.isInput && port.dataType == "Points")) continue;
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      if (c) { pts = cookNode(pinNode(c->fromPin)); cnt = p_->outCount[flatKey(pinNode(c->fromPin))]; }
      break;
    }
    CmdCookCtx cc;
    cc.ctx = &ctx; cc.graph = &g; cc.reg = reg;
    cc.nodeId = id; cc.points = pts; cc.count = cnt;
    cc.params = nodeParams(id);
    return cm->second(cc);
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
  std::function<MTL::Texture*(int)> cookTexNode = [&](int id) -> MTL::Texture* {
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
      }
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
