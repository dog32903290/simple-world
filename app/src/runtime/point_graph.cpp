#include "runtime/point_graph.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"        // Graph/Node/NodeSpec/PortSpec/pinId/pinNode/findSpec
#include "runtime/tixl_point.h"   // SwPoint (64B) + EvaluationContext (via eval_context.h)

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

bool isBufferInput(const PortSpec& p) {
  return p.isInput && (p.dataType == "Points" || p.dataType == "ParticleForce");
}

// A node's output point count: a generator's explicit "Count" Float param, else
// (modifier) the count inherited from its first wired Points input.
uint32_t nodeCount(const Node& n, const NodeSpec& s, uint32_t firstPointsCount) {
  for (const PortSpec& p : s.ports) {
    if (p.isInput && p.dataType == "Float" && p.id == "Count") {
      auto it = n.params.find("Count");
      float v = (it != n.params.end()) ? it->second : p.def;
      return v > 0.0f ? (uint32_t)(v + 0.5f) : 0u;
    }
  }
  return firstPointsCount;
}

}  // namespace

void registerPointOp(const std::string& type, PointCookFn cook, PointStateNewFn stNew,
                     PointStateFreeFn stFree) {
  cookReg()[type] = OpReg{cook, stNew, stFree};
}
void registerDrawOp(const std::string& type, PointDrawFn draw) { drawReg()[type] = draw; }

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
  if (p_->target) p_->target->release();
  if (p_->queue) p_->queue->release();
  if (p_->lib) p_->lib->release();
  if (p_->dev) p_->dev->release();
  delete p_;
}

bool PointGraph::valid() const { return p_->dev && p_->queue && p_->target; }
MTL::Texture* PointGraph::target() const { return p_->target; }

void PointGraph::cook(const Graph& g, const EvaluationContext& ctx, const SourceRegistry* reg) {
  // Find the draw node (first node whose type has a registered draw fn).
  const Node* drawNode = nullptr;
  PointDrawFn drawFn = nullptr;
  for (const Node& n : g.nodes) {
    auto it = drawReg().find(n.type);
    if (it != drawReg().end()) { drawNode = &n; drawFn = it->second; break; }
  }
  if (!drawNode) { p_->clearTarget(); return; }  // nothing to draw -> black, no crash

  std::map<int, MTL::Buffer*> cooked;  // this-frame memo (cook each node once)
  std::function<MTL::Buffer*(int)> cookNode = [&](int id) -> MTL::Buffer* {
    auto m = cooked.find(id);
    if (m != cooked.end()) return m->second;
    const Node* n = g.node(id);
    if (!n) return nullptr;
    const NodeSpec* s = findSpec(n->type);
    if (!s) return nullptr;

    // Gather buffer inputs (Points + ParticleForce input ports, in spec order).
    std::vector<const MTL::Buffer*> ins;
    uint32_t firstPointsCount = 0;
    bool haveFirstPoints = false;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!isBufferInput(port)) continue;
      const Connection* c = g.connectionToInput(pinId(id, (int)i));
      MTL::Buffer* ub = c ? cookNode(pinNode(c->fromPin)) : nullptr;
      ins.push_back(ub);
      if (port.dataType == "Points" && !haveFirstPoints) {
        haveFirstPoints = true;
        firstPointsCount = (c && ub) ? p_->outCount[pinNode(c->fromPin)] : 0u;
      }
    }

    uint32_t count = nodeCount(*n, *s, firstPointsCount);
    MTL::Buffer* out = p_->ensureOut(id, count);
    void* st = p_->ensureState(id, n->type, count);

    PointCookCtx cc;
    cc.dev = p_->dev; cc.lib = p_->lib; cc.queue = p_->queue;
    cc.ctx = &ctx; cc.graph = &g; cc.reg = reg;
    cc.nodeId = id; cc.count = count;
    cc.inputs = ins.data(); cc.inputCount = (int)ins.size();
    cc.output = out; cc.state = st;
    auto r = cookReg().find(n->type);
    if (r != cookReg().end() && r->second.cook) r->second.cook(cc);
    cooked[id] = out;
    return out;
  };

  // Cook the draw node's Points input, then render it into target.
  const NodeSpec* ds = findSpec(drawNode->type);
  MTL::Buffer* pts = nullptr;
  uint32_t drawCount = 0;
  if (ds) {
    for (size_t i = 0; i < ds->ports.size(); ++i) {
      const PortSpec& port = ds->ports[i];
      if (!(port.isInput && port.dataType == "Points")) continue;
      const Connection* c = g.connectionToInput(pinId(drawNode->id, (int)i));
      if (c) { pts = cookNode(pinNode(c->fromPin)); drawCount = p_->outCount[pinNode(c->fromPin)]; }
      break;
    }
  }
  PointCookCtx dc;
  dc.dev = p_->dev; dc.lib = p_->lib; dc.queue = p_->queue;
  dc.ctx = &ctx; dc.graph = &g; dc.reg = reg;
  dc.nodeId = drawNode->id; dc.count = drawCount;
  dc.inputs = nullptr; dc.inputCount = 0; dc.output = nullptr; dc.state = nullptr;
  drawFn(dc, p_->target, pts);
}

// ---------------------------------------------------------------------------
// Headless proof of the cook MACHINERY (not any real kernel). CPU-fill stub ops are
// registered under real type names; the chain RadialPoints→ParticleSystem→DrawPoints
// must thread the generated bag through the middle op to the draw. injectBug makes the
// middle op ignore its input so the (x==2) assertion fails.
namespace {

bool g_injectBug = false;
std::vector<SwPoint>* g_capture = nullptr;

// Generator: fill `count` points, Position.x = 1.
void stubGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = SwPoint{};
    dst[i].Position = {1.0f, 0.0f, 0.0f};
  }
}
// Modifier: copy input[0] -> output, Position.x *= 2 (proves input threading).
// Bug variant ignores the input and writes x = 0 so the x==2 assertion fails.
void stubMul(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  const MTL::Buffer* in0 = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const SwPoint* src = in0 ? (const SwPoint*)const_cast<MTL::Buffer*>(in0)->contents() : nullptr;
  for (uint32_t i = 0; i < c.count; ++i) {
    if (g_injectBug || !src) { dst[i] = SwPoint{}; dst[i].Position = {0.0f, 0.0f, 0.0f}; continue; }
    dst[i] = src[i];
    dst[i].Position.x = src[i].Position.x * 2.0f;
  }
}
// Draw: capture the final bag for assertion (no real rendering).
void stubDraw(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* points) {
  if (!g_capture || !points || c.count == 0) return;
  g_capture->assign(c.count, SwPoint{});
  std::memcpy(g_capture->data(), const_cast<MTL::Buffer*>(points)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

int runPointGraphSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  g_injectBug = injectBug;
  std::vector<SwPoint> captured;
  g_capture = &captured;

  // Register CPU-fill stubs under real type names (clean: builtin ops aren't registered
  // during a --selftest run — main dispatches selftests before app init).
  registerPointOp("RadialPoints", stubGen);
  registerPointOp("ParticleSystem", stubMul);
  registerDrawOp("DrawPoints", stubDraw);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  // Build RadialPoints(Count=8) -> ParticleSystem -> DrawPoints.
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints"; gen.params["Count"] = 8.0f; g.nodes.push_back(gen);
  Node mid; mid.id = 2; mid.type = "ParticleSystem"; g.nodes.push_back(mid);
  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  // RadialPoints.points(port0) -> ParticleSystem.emit(port0)
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  // ParticleSystem.result(port2) -> DrawPoints.points(port0)
  g.connections.push_back({102, pinId(2, 2), pinId(3, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr);
  pg.cook(g, ctx, nullptr);  // second cook: exercise buffer reuse (no realloc, same result)

  bool ok = captured.size() == 8;
  for (const SwPoint& p : captured) ok = ok && (p.Position.x == 2.0f);

  printf("[selftest-pointgraph] cooked=%zu (want 8) x[0]=%.1f (want 2.0) -> %s\n", captured.size(),
         captured.empty() ? -1.0f : captured[0].Position.x, ok ? "PASS" : "FAIL");

  g_capture = nullptr;
  q->release();
  dev->release();
  pool->release();
  return ok ? 0 : 1;
}

}  // namespace sw
