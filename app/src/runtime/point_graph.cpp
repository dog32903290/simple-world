#include "runtime/point_graph.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"               // Graph/Node/NodeSpec/PortSpec/pinId/pinNode/findSpec
#include "runtime/resident_eval_graph.h" // ResidentEvalGraph / ResidentNode / ResidentInput
#include "runtime/tixl_point.h"         // SwPoint (64B) + EvaluationContext (via eval_context.h)

namespace sw {
namespace {

constexpr MTL::PixelFormat kTargetFormat = MTL::PixelFormatRGBA8Unorm;

// --- Operator registries (Metal-side; separate from NodeSpec.evaluate float path) ---
struct OpReg {
  PointCookFn cook = nullptr;
  PointStateNewFn stateNew = nullptr;
  PointStateFreeFn stateFree = nullptr;
};
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

bool isBufferInput(const PortSpec& p) {
  return p.isInput && (p.dataType == "Points" || p.dataType == "ParticleForce");
}

// A node's output point count: a generator's explicit "Count" Float param, else the SUM of
// all wired Points-input counts. The sum generalizes all three op shapes: generator (0 Points
// inputs -> falls to "Count"), modifier (1 Points input -> that input's count, unchanged), and
// combine (N Points inputs -> the concatenated total). A node with a "Count" param wins
// regardless (generators only).
uint32_t nodeCount(const Node& n, const NodeSpec& s, uint32_t sumPointsCount) {
  for (const PortSpec& p : s.ports) {
    if (p.isInput && p.dataType == "Float" && p.id == "Count") {
      auto it = n.params.find("Count");
      float v = (it != n.params.end()) ? it->second : p.def;
      return v > 0.0f ? (uint32_t)(v + 0.5f) : 0u;
    }
  }
  return sumPointsCount;
}

}  // namespace

void registerPointOp(const std::string& type, PointCookFn cook, PointStateNewFn stNew,
                     PointStateFreeFn stFree) {
  cookReg()[type] = OpReg{cook, stNew, stFree};
}
void registerDrawOp(const std::string& type, PointDrawFn draw) { drawReg()[type] = draw; }
void registerCmdOp(const std::string& type, PointCmdFn cmd) { cmdReg()[type] = cmd; }
void registerTexOp(const std::string& type, PointTexFn tex) { texReg()[type] = tex; }

// registerBuiltinPointOps() is defined in point_ops.cpp (the real operators).

// ---------------------------------------------------------------------------

struct PointGraph::Impl {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  MTL::Texture* target = nullptr;
  uint32_t width = 0, height = 0;

  // Per-node persistent resources (reused across frames; the RESOURCE_LIFETIME golden:
  // allocate → reuse (count unchanged) → reallocate (count grew)).
  std::map<int, MTL::Buffer*> outBuf;    // node id -> output point buffer
  std::map<int, uint32_t> outCap;        // node id -> allocated capacity (points)
  std::map<int, uint32_t> outCount;      // node id -> last cooked count (points)
  std::map<int, void*> state;            // node id -> stateful-op memory
  std::map<int, PointStateFreeFn> stateFree;

  // Per-node RenderTarget textures (the Texture2D stream's resources; realloc on resolution
  // change — RESOURCE_LIFETIME). displayTex = the texture target() shows this frame: a tex
  // terminal's own resolution-sized texture, or null -> fall back to the window-sized `target`.
  std::map<int, MTL::Texture*> texBuf;
  std::map<int, uint32_t> texW, texH;
  MTL::Texture* displayTex = nullptr;

  MTL::Buffer* ensureOut(int id, uint32_t count) {
    MTL::Buffer*& b = outBuf[id];
    if (!b || outCap[id] < count) {
      if (b) b->release();
      uint32_t cap = count > 0 ? count : 1;  // never alloc zero
      b = dev->newBuffer((NS::UInteger)cap * sizeof(SwPoint), MTL::ResourceStorageModeShared);
      outCap[id] = cap;
    }
    outCount[id] = count;
    return b;
  }

  // The RenderTarget node's own output texture, sized to its resolved resolution. Reused across
  // frames; reallocated only when w/h change (RESOURCE_LIFETIME). Owned (newTexture) -> released
  // on realloc + in the destructor; the descriptor is an autoreleased factory (frame pool owns it).
  MTL::Texture* ensureTex(int id, uint32_t w, uint32_t h) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    MTL::Texture*& t = texBuf[id];
    if (!t || texW[id] != w || texH[id] != h) {
      if (t) t->release();
      MTL::TextureDescriptor* td =
          MTL::TextureDescriptor::texture2DDescriptor(kTargetFormat, w, h, false);
      td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
      td->setStorageMode(MTL::StorageModeShared);
      t = dev->newTexture(td);
      texW[id] = w;
      texH[id] = h;
    }
    return t;
  }

  void* ensureState(int id, const std::string& type, uint32_t count) {
    auto it = state.find(id);
    if (it != state.end()) return it->second;
    auto r = cookReg().find(type);
    if (r != cookReg().end() && r->second.stateNew) {
      void* st = r->second.stateNew(dev, lib, count);
      state[id] = st;
      stateFree[id] = r->second.stateFree;
      return st;
    }
    state[id] = nullptr;
    return nullptr;
  }

  void clearTarget() {
    MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = rpd->colorAttachments()->object(0);
    ca->setTexture(target);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    cmd->renderCommandEncoder(rpd)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }
};

PointGraph::PointGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* queue, uint32_t width,
                       uint32_t height)
    : p_(new Impl) {
  p_->dev = dev->retain();
  p_->lib = lib ? lib->retain() : nullptr;
  p_->queue = queue->retain();
  p_->width = width;
  p_->height = height;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(kTargetFormat, width, height, false);
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

int PointGraph::defaultDrawTarget(const Graph& g) const {
  // The terminal is the most-downstream realizable node: a RenderTarget (Texture2D) wins, else
  // a DrawPoints (Command). Both replaced the old drawReg draw node in production.
  for (const Node& n : g.nodes)
    if (texReg().find(n.type) != texReg().end()) return n.id;
  for (const Node& n : g.nodes)
    if (cmdReg().find(n.type) != cmdReg().end()) return n.id;
  // Legacy draw terminal (PointDrawFn, retired in batch 4): production registers none, but
  // self-contained golden selftests register a capture-only draw op as their terminal — keep it
  // discoverable so cook() can dispatch it, until the draw model is fully retired.
  for (const Node& n : g.nodes)
    if (drawReg().find(n.type) != drawReg().end()) return n.id;
  return 0;
}

void PointGraph::cook(const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg,
                      int targetNodeId) {
  p_->displayTex = nullptr;  // default: target() shows the window-sized texture (cmd/preview paths)
  const Node* target = g.node(targetNodeId);
  const NodeSpec* ts = target ? findSpec(target->type) : nullptr;
  if (!target || !ts) { p_->clearTarget(); return; }  // no/unknown target -> black, no crash

  std::map<int, MTL::Buffer*> cooked;  // this-frame memo (cook each node once)
  std::function<MTL::Buffer*(int)> cookNode = [&](int id) -> MTL::Buffer* {
    auto m = cooked.find(id);
    if (m != cooked.end()) return m->second;
    const Node* n = g.node(id);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->type);
    if (!s) return nullptr;

    // Gather buffer inputs (Points + ParticleForce input ports, in spec order) + their counts.
    // sumPointsCount = total over ALL wired Points inputs (combine concatenates; modifier/
    // generator have <=1 so it equals the old first-input behavior).
    std::vector<const MTL::Buffer*> ins;
    std::vector<uint32_t> insCounts;
    uint32_t sumPointsCount = 0;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!isBufferInput(port)) continue;
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      MTL::Buffer* ub = c ? cookNode(pinNode(c->fromPin)) : nullptr;
      uint32_t inCount = (c && ub) ? p_->outCount[pinNode(c->fromPin)] : 0u;
      ins.push_back(ub);
      insCounts.push_back(inCount);
      if (port.dataType == "Points") sumPointsCount += inCount;
    }

    uint32_t count = nodeCount(*n, *s, sumPointsCount);
    MTL::Buffer* out = p_->ensureOut(id, count);
    void* st = p_->ensureState(id, n->type, count);

    PointCookCtx cc;
    cc.dev = p_->dev; cc.lib = p_->lib; cc.queue = p_->queue;
    cc.ctx = &ctx; cc.graph = &g; cc.reg = reg;
    cc.nodeId = id; cc.count = count;
    cc.inputs = ins.data(); cc.inputCounts = insCounts.data(); cc.inputCount = (int)ins.size();
    cc.output = out; cc.state = st;
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
      if (c) { pts = cookNode(pinNode(c->fromPin)); cnt = p_->outCount[pinNode(c->fromPin)]; }
      break;
    }
    CmdCookCtx cc;
    cc.ctx = &ctx; cc.graph = &g; cc.reg = reg;
    cc.nodeId = id; cc.points = pts; cc.count = cnt;
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
    tx->second(tc);
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
      if (c) { pts = cookNode(pinNode(c->fromPin)); drawCount = p_->outCount[pinNode(c->fromPin)]; }
      break;
    }
    PointCookCtx dc;
    dc.dev = p_->dev; dc.lib = p_->lib; dc.queue = p_->queue;
    dc.ctx = &ctx; dc.graph = &g; dc.reg = reg;
    dc.nodeId = target->id; dc.count = drawCount;
    dc.inputs = nullptr; dc.inputCount = 0; dc.output = nullptr; dc.state = nullptr;
    drawIt->second(dc, p_->target, pts);
    return;
  }

  auto texIt = texReg().find(target->type);
  if (texIt != texReg().end() && texIt->second) {
    // Texture terminal (RenderTarget): size its OWN texture from the Resolution pin (the live
    // window size is WindowFollow's source), concat all upstream Command inputs, run its tex op
    // into that texture, and show it. p_->target stays the window-sized fallback for cmd/preview.
    RenderResolution res = resolveRenderResolution(target, RenderResolution{p_->width, p_->height});
    MTL::Texture* tex = p_->ensureTex(target->id, res.w, res.h);
    RenderCommand chain;
    for (size_t i = 0; i < ts->ports.size(); ++i) {
      const PortSpec& port = ts->ports[i];
      if (!(port.isInput && port.dataType == "Command")) continue;
      const Connection* c = g.connectionToInput(pinId(target->id, (int)i));
      if (!c) continue;
      RenderCommand up = cookCommand(pinNode(c->fromPin));
      chain.items.insert(chain.items.end(), up.items.begin(), up.items.end());
    }
    TexCookCtx tc;
    tc.dev = p_->dev; tc.lib = p_->lib; tc.queue = p_->queue;
    tc.ctx = &ctx; tc.graph = &g; tc.reg = reg;
    tc.nodeId = target->id; tc.command = &chain; tc.output = tex;
    texIt->second(tc);
    p_->displayTex = tex;  // viewport shows the resolution-sized texture
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
      chain.items.push_back(RenderDrawItem{out, p_->outCount[targetNodeId], 3.5f});
      execIntoTarget(chain, "RenderTarget", targetNodeId);
    } else {
      p_->clearTarget();  // no visualizer for this output type yet (§5)
    }
  }
}

void PointGraph::cookResident(const ResidentEvalGraph&, const EvaluationContext&,
                              const SourceRegistry*, const std::string&) {
  // STUB (Task 2 implements the buffer-flow walk). Empty -> the golden's resident bag stays
  // empty -> RED until Task 2.
}

}  // namespace sw
