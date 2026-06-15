// runtime/point_graph_internal — PRIVATE seam between point_graph.cpp (flat cook) and
// point_graph_resident.cpp (resident cook). Not a public API: only those two TUs include it.
// Exists so the resident cook can share PointGraph::Impl (per-node persistent resources) and
// the op registries without point_graph.cpp growing past the ~400-line law.
//
// Resource keys are STRINGS (slice 2b convergence): the resident graph's path-qualified id
// ("5/2") is the natural frame-stable key; the flat cook prefixes its int node id ("#7") so
// the two key spaces can never collide while both cooks are alive (flat dies at the
// production swap; then "#" keys go with it).
#pragma once
#include <cstdint>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"        // PortSpec (isBufferInput)
#include "runtime/point_graph.h"  // PointGraph + op fn types
#include "runtime/tixl_point.h"   // SwPoint (64B) — output buffers are SwPoint bags

namespace sw {
namespace pgdetail {

// --- Operator registries (Metal-side; separate from NodeSpec.evaluate float path) ---
struct OpReg {
  PointCookFn cook = nullptr;
  PointStateNewFn stateNew = nullptr;
  PointStateFreeFn stateFree = nullptr;
  // Optional: map the natural count (sum of Points inputs / Count param) to the count the
  // node's output + state are sized to. ParticleSystem uses it to grow a particle POOL larger
  // than its emit ring (particle_params.h: pool > emit is what lets the cycle buffer recycle).
  // null = identity. Applied in BOTH cooks right before ensureOut/ensureState.
  PointCountFn countTransform = nullptr;
  // Count policy for ops with >1 Points input. Default false = output count is the SUM of all
  // wired Points inputs (CombineBuffers concatenates). true = output count is the FIRST Points
  // input's count only (the op transforms Points1 using the remaining Points inputs as references,
  // not concatenation — e.g. SnapToPoints: out count = Points1 count, Points2 is a snap target).
  // No-op for ops with <=1 Points input (sum == first). Applied in BOTH cooks.
  bool countFromFirstPointsInput = false;
};
std::map<std::string, OpReg>& cookReg();
std::map<std::string, PointDrawFn>& drawReg();
std::map<std::string, PointCmdFn>& cmdReg();
std::map<std::string, PointTexFn>& texReg();

inline bool isBufferInput(const PortSpec& p) {
  return p.isInput && (p.dataType == "Points" || p.dataType == "ParticleForce");
}

// Flat cook's resource key for node id (see header comment on key spaces).
inline std::string flatKey(int id) { return "#" + std::to_string(id); }

}  // namespace pgdetail

constexpr MTL::PixelFormat kPointTargetFormat = MTL::PixelFormatRGBA8Unorm;

struct PointGraph::Impl {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  MTL::Texture* target = nullptr;
  uint32_t width = 0, height = 0;

  // Per-node persistent resources (reused across frames; the RESOURCE_LIFETIME golden:
  // allocate → reuse (count unchanged) → reallocate (count grew)). Keyed by resident path
  // or "#"-prefixed flat id (pgdetail::flatKey).
  std::map<std::string, MTL::Buffer*> outBuf;    // key -> output point buffer
  std::map<std::string, uint32_t> outCap;        // key -> allocated capacity (points)
  std::map<std::string, uint32_t> outCount;      // key -> last cooked count (points)
  std::map<std::string, void*> state;            // key -> stateful-op memory
  std::map<std::string, PointStateFreeFn> stateFree;
  std::map<std::string, uint32_t> stateCap;      // key -> count the state was created for

  // Per-node RenderTarget textures (the Texture2D stream's resources; realloc on resolution
  // change — RESOURCE_LIFETIME). displayTex = the texture target() shows this frame: a tex
  // terminal's own resolution-sized texture, or null -> fall back to the window-sized `target`.
  std::map<std::string, MTL::Texture*> texBuf;
  std::map<std::string, uint32_t> texW, texH;
  MTL::Texture* displayTex = nullptr;

  MTL::Buffer* ensureOut(const std::string& key, uint32_t count) {
    MTL::Buffer*& b = outBuf[key];
    if (!b || outCap[key] < count) {
      if (b) b->release();
      uint32_t cap = count > 0 ? count : 1;  // never alloc zero
      b = dev->newBuffer((NS::UInteger)cap * sizeof(SwPoint), MTL::ResourceStorageModeShared);
      outCap[key] = cap;
    }
    outCount[key] = count;
    return b;
  }

  // The RenderTarget node's own output texture, sized to its resolved resolution. Reused across
  // frames; reallocated only when w/h change (RESOURCE_LIFETIME). Owned (newTexture) -> released
  // on realloc + in the destructor; the descriptor is an autoreleased factory (frame pool owns it).
  // shaderWrite: a COMPUTE leaf (image_filter_op_registry, -cs.hlsl) writes its output via a
  // RWTexture2D, so its output texture needs MTL::TextureUsageShaderWrite on top of the usual
  // RenderTarget|ShaderRead. Pixel ops leave it false (default) -> byte-identical descriptor.
  MTL::Texture* ensureTex(const std::string& key, uint32_t w, uint32_t h, bool shaderWrite = false) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    MTL::Texture*& t = texBuf[key];
    if (!t || texW[key] != w || texH[key] != h) {
      if (t) t->release();
      MTL::TextureDescriptor* td =
          MTL::TextureDescriptor::texture2DDescriptor(kPointTargetFormat, w, h, false);
      MTL::TextureUsage usage = MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead;
      if (shaderWrite) usage |= MTL::TextureUsageShaderWrite;
      td->setUsage(usage);
      td->setStorageMode(MTL::StorageModeShared);
      t = dev->newTexture(td);
      texW[key] = w;
      texH[key] = h;
    }
    return t;
  }

  // Per-node stateful-op memory. Re-created (free + new) when `count` GROWS past what the
  // state was sized for — stateNew sizes internal buffers (e.g. the sim's particle buffer)
  // to the creation-time count, so dispatching a larger count over stale state is a GPU
  // out-of-bounds write (refuter 2b finding 1; mirror of ensureOut's grow rule). Growing
  // resets the sim's continuity — correctness over continuity, same as a buffer realloc.
  void* ensureState(const std::string& key, const std::string& type, uint32_t count) {
    auto it = state.find(key);
    if (it != state.end()) {
      if (!it->second || count <= stateCap[key]) return it->second;
      if (stateFree.count(key) && stateFree[key]) stateFree[key](it->second);  // grew: rebuild
      state.erase(it);
    }
    auto r = pgdetail::cookReg().find(type);
    if (r != pgdetail::cookReg().end() && r->second.stateNew) {
      void* st = r->second.stateNew(dev, lib, count);
      state[key] = st;
      stateFree[key] = r->second.stateFree;
      stateCap[key] = count;
      return st;
    }
    state[key] = nullptr;
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

}  // namespace sw
