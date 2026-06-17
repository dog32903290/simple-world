// runtime/tex_op_cache — shared PSO + scratch-texture cache for image-filter TEXTURE ops (lane D2).
//
// Why it exists (perf debt from 批次12): the first image filters (Blur, then Displace) rebuilt their
// render pipeline state AND allocated a scratch intermediate texture on EVERY cook (every frame).
// PSO compilation and texture allocation are both heavy; doing them per-frame is wasteful. This is
// ESSENTIAL complexity (GPU object lifetime) packed behind a clean seam: ops call two functions and
// never touch the cache internals.
//
// Seam shape: a TEXTURE op gets dev/lib/queue (TexCookCtx) but NOT a PointGraph handle, so the cache
// is a process-global keyed store (like a Metal pipeline cache) rather than a PointGraph member. PSOs
// are device-global and immutable once built (key = vs+fs+pixelFormat); scratch textures are sized
// per (key,w,h,format) and reused across frames, reallocated only when the size changes — the same
// RESOURCE_LIFETIME rule PointGraph::ensureTex follows for op output textures.
//
// Blur and Displace share this ONE cache seam (the task's "Blur/Displace 共用同一套快取縫").
#pragma once

#include <cstdint>
#include <string>

namespace MTL {
class Device;
class Library;
class RenderPipelineState;
class ComputePipelineState;
class Texture;
}  // namespace MTL

namespace sw {

// Pixel format is passed as its raw enum value (NS::UInteger underlying) so this header stays free of
// the heavy Metal.hpp include and its _MTL_ENUM forward-decl constraints. Callers pass (uint64_t)fmt;
// the .cpp casts back to MTL::PixelFormat. (kept a typedef so call sites read clearly)
using TexPixelFormat = uint64_t;

// Cached render PSO for a (vertexFn, fragmentFn, colorPixelFormat) triple. Built once on first
// request and reused forever (device-global). Returns nullptr if either function is missing or the
// pipeline fails to compile. The cache OWNS the PSO (no caller release).
MTL::RenderPipelineState* cachedTexPSO(MTL::Device* dev, MTL::Library* lib, const char* vsName,
                                       const char* fsName, TexPixelFormat fmt);

// Cached render PSO for a RUNTIME-COMPILED MSL SOURCE STRING (the field shader-graph island — the
// one path whose MSL does not exist until assembleFieldMSL builds it, so it cannot be precompiled).
// Keyed by the source's FNV-1a hash (srcHash from runtime::assembleFieldMSL) so the SAME assembled
// field never recompiles: miss -> compile the source into a Library via the registered field source
// compiler (sw::fieldSourceCompiler, wired by app to platform::compileLibraryFromSource), build the
// (vs,fs) render PSO from it, and store it under srcHash; hit -> return the stored PSO with ZERO
// per-frame compilation. Returns nullptr if no compiler is registered, the source fails to compile,
// either function is missing, or the pipeline fails. The cache OWNS the PSO and the backing Library
// (no caller release). Dropped by clearTexOpCache().
//
// srcHash (not the source text) is the key: two identical assembled fields share one PSO; a field
// whose params changed but whose CODE is identical keeps the same srcHash (params live in the buffer,
// not the source) and correctly reuses the PSO — exactly TiXL's ChangedFlags.Code gate.
MTL::RenderPipelineState* cachedSourcePSO(MTL::Device* dev, const char* mslSource, uint64_t srcHash,
                                          const char* vsName, const char* fsName, TexPixelFormat fmt);

// Cached COMPUTE PSO for a kernel function (the -cs.hlsl path: a compute image-filter leaf such as
// Crop dispatches a compute kernel instead of a fullscreen draw). Built once on first request and
// reused forever (device-global), keyed by function name. Returns nullptr if the function is missing
// or the pipeline fails to compile. The cache OWNS the PSO (no caller release). Dropped by
// clearTexOpCache() alongside the render PSOs, so per-run-device selftests get a clean table.
MTL::ComputePipelineState* cachedComputePSO(MTL::Device* dev, MTL::Library* lib, const char* fnName);

// A reusable scratch render-target texture (RenderTarget|ShaderRead, Shared storage) of the given
// size/format, keyed by `key`. Reallocated only when the requested size/format differs from the
// cached one for that key (RESOURCE_LIFETIME). The cache OWNS the texture (no caller release). Use a
// distinct `key` per logical scratch slot (e.g. "blur.h", "displace.scratch") so two ops don't fight
// over one texture. Returns nullptr if w==0 or h==0.
//
// shaderWrite (default false): OR MTL::TextureUsageShaderWrite into the usage so a MULTI-PASS COMPUTE
// leaf (FastBlur) can use this scratch as BOTH a compute-read source and a compute-write target
// across passes. mipped (default false): allocate a full mip chain. BOTH flags are part of the
// realloc key — a usage/mip change for the same key forces a realloc (mirrors ensureTex's texMipped
// fold). Default-false keeps the existing Blur/Displace call sites byte-identical (same
// zero-regression discipline as the ensureTex shaderWrite/mipped back-port).
MTL::Texture* cachedScratchTex(MTL::Device* dev, TexPixelFormat fmt, uint32_t w, uint32_t h,
                               const std::string& key, bool shaderWrite = false,
                               bool mipped = false);

// Drop every cached PSO and scratch texture (test isolation / device teardown). Selftests that
// create a fresh MTL::Device per run call this so a stale PSO built on a released device is never
// reused. Safe to call when empty.
void clearTexOpCache();

}  // namespace sw
