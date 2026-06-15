// runtime/tex_op_cache — implementation. See tex_op_cache.h for the contract.
#include "runtime/tex_op_cache.h"

#include <map>
#include <tuple>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

namespace sw {
namespace {

// PSO cache key: (vsName, fsName, pixelFormat). Device-global; PSOs are immutable once built.
using PSOKey = std::tuple<std::string, std::string, uint64_t>;
std::map<PSOKey, MTL::RenderPipelineState*>& psoCache() {
  static std::map<PSOKey, MTL::RenderPipelineState*> c;
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
  for (auto& kv : computePsoCache())
    if (kv.second) kv.second->release();
  computePsoCache().clear();
  for (auto& kv : scratchCache())
    if (kv.second.tex) kv.second.tex->release();
  scratchCache().clear();
}

}  // namespace sw
