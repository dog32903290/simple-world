// runtime/tex_op_cache — implementation. See tex_op_cache.h for the contract.
#include "runtime/tex_op_cache.h"

#include <map>
#include <tuple>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_graph.h"  // fieldSourceCompiler (the runtime->platform MSL-source leaf seam)

namespace sw {
namespace {

// PSO cache key: (vsName, fsName, pixelFormat). Device-global; PSOs are immutable once built.
using PSOKey = std::tuple<std::string, std::string, uint64_t>;
std::map<PSOKey, MTL::RenderPipelineState*>& psoCache() {
  static std::map<PSOKey, MTL::RenderPipelineState*> c;
  return c;
}

// Additive-blend PSO cache: same (vs,fs,fmt) key but a SEPARATE table so the additive variant never
// collides with the non-additive PSO for the same triple (Bloom upsample-add needs blending ON).
std::map<PSOKey, MTL::RenderPipelineState*>& psoBlendAddCache() {
  static std::map<PSOKey, MTL::RenderPipelineState*> c;
  return c;
}

// Source-PSO cache: keyed by the assembled MSL's srcHash (FNV-1a). Stores the PSO and the backing
// runtime-compiled Library together so both are released on teardown (the Library outlives PSO
// creation only as the function source; we keep it owned by the cache for symmetry/clean release).
struct SourcePSOEntry {
  MTL::RenderPipelineState* pso = nullptr;
  MTL::Library* lib = nullptr;
};
std::map<uint64_t, SourcePSOEntry>& sourcePsoCache() {
  static std::map<uint64_t, SourcePSOEntry> c;
  return c;
}

// Compute PSO cache: keyed by kernel function name (device-global, immutable once built).
std::map<std::string, MTL::ComputePipelineState*>& computePsoCache() {
  static std::map<std::string, MTL::ComputePipelineState*> c;
  return c;
}

// Scratch cache: one entry per logical key; reallocated only when size/format/usage/mip changes.
struct ScratchEntry {
  MTL::Texture* tex = nullptr;
  uint32_t w = 0, h = 0;
  uint64_t fmt = 0;
  bool shaderWrite = false;  // part of the realloc key: a usage change forces a realloc
  bool mipped = false;       // part of the realloc key: a non-mipped tex has no levels to fill
};
std::map<std::string, ScratchEntry>& scratchCache() {
  static std::map<std::string, ScratchEntry> c;
  return c;
}

}  // namespace

MTL::RenderPipelineState* cachedTexPSO(MTL::Device* dev, MTL::Library* lib, const char* vsName,
                                       const char* fsName, TexPixelFormat fmt) {
  if (!dev || !lib) return nullptr;
  PSOKey key{vsName, fsName, (uint64_t)fmt};
  auto& cache = psoCache();
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;  // built before -> reuse

  MTL::Function* vs = lib->newFunction(NS::String::string(vsName, NS::UTF8StringEncoding));
  MTL::Function* fs = lib->newFunction(NS::String::string(fsName, NS::UTF8StringEncoding));
  MTL::RenderPipelineState* rps = nullptr;
  if (vs && fs) {
    MTL::RenderPipelineDescriptor* rpd = MTL::RenderPipelineDescriptor::alloc()->init();
    rpd->setVertexFunction(vs);
    rpd->setFragmentFunction(fs);
    rpd->colorAttachments()->object(0)->setPixelFormat((MTL::PixelFormat)fmt);
    NS::Error* err = nullptr;
    rps = dev->newRenderPipelineState(rpd, &err);
    rpd->release();
  }
  if (vs) vs->release();
  if (fs) fs->release();
  if (rps) cache[key] = rps;  // only cache a successful build (nullptr is retried next call)
  return rps;
}

MTL::RenderPipelineState* cachedTexPSOBlendAdd(MTL::Device* dev, MTL::Library* lib,
                                               const char* vsName, const char* fsName,
                                               TexPixelFormat fmt) {
  if (!dev || !lib) return nullptr;
  PSOKey key{vsName, fsName, (uint64_t)fmt};
  auto& cache = psoBlendAddCache();
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;  // built before -> reuse

  MTL::Function* vs = lib->newFunction(NS::String::string(vsName, NS::UTF8StringEncoding));
  MTL::Function* fs = lib->newFunction(NS::String::string(fsName, NS::UTF8StringEncoding));
  MTL::RenderPipelineState* rps = nullptr;
  if (vs && fs) {
    MTL::RenderPipelineDescriptor* rpd = MTL::RenderPipelineDescriptor::alloc()->init();
    rpd->setVertexFunction(vs);
    rpd->setFragmentFunction(fs);
    auto* ca = rpd->colorAttachments()->object(0);
    ca->setPixelFormat((MTL::PixelFormat)fmt);
    // Additive: dst = src*1 + dst*1 (One,One) for both rgb and alpha — TiXL AdditiveBlendState.
    ca->setBlendingEnabled(true);
    ca->setSourceRGBBlendFactor(MTL::BlendFactorOne);
    ca->setDestinationRGBBlendFactor(MTL::BlendFactorOne);
    ca->setRgbBlendOperation(MTL::BlendOperationAdd);
    ca->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    ca->setDestinationAlphaBlendFactor(MTL::BlendFactorOne);
    ca->setAlphaBlendOperation(MTL::BlendOperationAdd);
    NS::Error* err = nullptr;
    rps = dev->newRenderPipelineState(rpd, &err);
    rpd->release();
  }
  if (vs) vs->release();
  if (fs) fs->release();
  if (rps) cache[key] = rps;  // only cache a successful build (nullptr is retried next call)
  return rps;
}

MTL::RenderPipelineState* cachedSourcePSO(MTL::Device* dev, const char* mslSource, uint64_t srcHash,
                                          const char* vsName, const char* fsName,
                                          TexPixelFormat fmt) {
  if (!dev || !mslSource || !vsName || !fsName) return nullptr;
  auto& cache = sourcePsoCache();
  auto it = cache.find(srcHash);
  if (it != cache.end()) return it->second.pso;  // HIT: same assembled field -> zero recompile

  // MISS: compile the assembled MSL source via the registered field source compiler (runtime->
  // platform leaf seam). The compiler returns an OWNED MTL::Library* as void* (runtime must not name
  // MTL types in field_graph.h). Cast it back here (tex_op_cache.cpp is allowed Metal.hpp).
  SourceCompileFn compile = fieldSourceCompiler();
  if (!compile) return nullptr;  // app never registered one (e.g. a non-field selftest) -> graceful
  MTL::Library* lib = static_cast<MTL::Library*>(compile(dev, mslSource));
  if (!lib) return nullptr;  // source failed to compile (the error face was logged in platform)

  MTL::Function* vs = lib->newFunction(NS::String::string(vsName, NS::UTF8StringEncoding));
  MTL::Function* fs = lib->newFunction(NS::String::string(fsName, NS::UTF8StringEncoding));
  MTL::RenderPipelineState* rps = nullptr;
  if (vs && fs) {
    MTL::RenderPipelineDescriptor* rpd = MTL::RenderPipelineDescriptor::alloc()->init();
    rpd->setVertexFunction(vs);
    rpd->setFragmentFunction(fs);
    rpd->colorAttachments()->object(0)->setPixelFormat((MTL::PixelFormat)fmt);
    NS::Error* err = nullptr;
    rps = dev->newRenderPipelineState(rpd, &err);
    rpd->release();
  }
  if (vs) vs->release();
  if (fs) fs->release();
  if (rps) {
    cache[srcHash] = SourcePSOEntry{rps, lib};  // cache OWNS pso + lib (released in clearTexOpCache)
  } else {
    lib->release();  // pipeline build failed -> don't leak the library; retried next call
  }
  return rps;
}

MTL::ComputePipelineState* cachedComputePSO(MTL::Device* dev, MTL::Library* lib,
                                            const char* fnName) {
  if (!dev || !lib || !fnName) return nullptr;
  auto& cache = computePsoCache();
  std::string key(fnName);
  auto it = cache.find(key);
  if (it != cache.end()) return it->second;  // built before -> reuse

  MTL::Function* fn = lib->newFunction(NS::String::string(fnName, NS::UTF8StringEncoding));
  MTL::ComputePipelineState* pso = nullptr;
  if (fn) {
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
  }
  if (pso) cache[key] = pso;  // only cache a successful build (nullptr is retried next call)
  return pso;
}

MTL::Texture* cachedScratchTex(MTL::Device* dev, TexPixelFormat fmt, uint32_t w, uint32_t h,
                               const std::string& key, bool shaderWrite, bool mipped) {
  if (!dev || w == 0 || h == 0) return nullptr;
  auto& cache = scratchCache();
  ScratchEntry& e = cache[key];
  if (e.tex && e.w == w && e.h == h && e.fmt == (uint64_t)fmt && e.shaderWrite == shaderWrite &&
      e.mipped == mipped)
    return e.tex;  // reuse same-size/usage/mip

  if (e.tex) { e.tex->release(); e.tex = nullptr; }  // size/format/usage/mip changed -> reallocate
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor((MTL::PixelFormat)fmt, w, h, mipped);
  MTL::TextureUsage usage = MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead;
  if (shaderWrite) usage |= MTL::TextureUsageShaderWrite;
  td->setUsage(usage);
  td->setStorageMode(MTL::StorageModeShared);
  e.tex = dev->newTexture(td);
  e.w = w; e.h = h; e.fmt = (uint64_t)fmt;
  e.shaderWrite = shaderWrite; e.mipped = mipped;
  return e.tex;
}

void clearTexOpCache() {
  for (auto& kv : psoCache())
    if (kv.second) kv.second->release();
  psoCache().clear();
  for (auto& kv : psoBlendAddCache())
    if (kv.second) kv.second->release();
  psoBlendAddCache().clear();
  for (auto& kv : computePsoCache())
    if (kv.second) kv.second->release();
  computePsoCache().clear();
  for (auto& kv : sourcePsoCache()) {
    if (kv.second.pso) kv.second.pso->release();
    if (kv.second.lib) kv.second.lib->release();
  }
  sourcePsoCache().clear();
  for (auto& kv : scratchCache())
    if (kv.second.tex) kv.second.tex->release();
  scratchCache().clear();
}

}  // namespace sw
