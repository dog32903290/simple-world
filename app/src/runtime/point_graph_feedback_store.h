// runtime/point_graph_feedback_store — the cross-frame FEEDBACK resource pool (the persistent
// texture-pair / buffer-pair / N-slice texture-array machine), peeled out of point_graph_internal.h.
//
// WHY A SEPARATE LEAF (structural, zero behaviour change): PointGraph::Impl carries every per-node
// resource map + its ensure* helper inline. The feedback block (KeepPreviousFrame's double-buffer
// pair, KeepPreviousPointBuffer's buffer pair, and now KeepInTextureArray/TimeDisplace's N-slice
// history RING) is a self-contained sub-machine — its own realloc-keyed lifetime, its own release
// loop. It was pushing point_graph_internal.h against its line-count ratchet (docs/agent/CLONE_MAP
// "KeepInTextureArray BLOCKED 診斷 2026-07-06": adding the array machine there would blow the cap).
// Split it into THIS base struct; Impl inherits it, so every existing call site (ensureFeedbackPair,
// feedbackTexBuf, feedbackToggle, …) resolves UNCHANGED through inheritance (no qualifier churn — the
// eaa678b registry-split discipline: verbatim move, identical symbols).
//
// `dev` lives here too (the ensure* allocators need it); Impl inherits the SAME `dev` pointer it used
// to declare directly, and every `p_->dev` / `dev->newTexture` in the derived methods still binds it.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>

#include <Metal/Metal.hpp>

namespace sw {

// The cross-frame feedback resource pool. Inherited by PointGraph::Impl (point_graph_internal.h).
// Every map is keyed by the flat cook's "#id" or the resident path (the same key spaces Impl uses).
struct FeedbackStore {
  MTL::Device* dev = nullptr;  // set by PointGraph::create (p_->dev = dev->retain()); shared by Impl.

  // ── Per-node CROSS-FRAME texture PAIR (the feedback / ping-pong flow = TiXL KeepPreviousFrame). A
  // feedback op owns TWO textures (texA+texB) that PERSIST across frames + a toggle bit (flips each
  // frame) so it hands back the PREVIOUS frame while writing the current into the other buffer.
  // Realloc-keyed like ensureOwnedTex (w/h/fmt → release the OLD pair; both released in ~PointGraph →
  // NO leak/UAF). Keyed flat id / resident path. feedbackToggle defaults false per node (TiXL
  // `_bufferToggle`, KeepPreviousFrame.cs:80).
  struct FeedbackPair { MTL::Texture* a = nullptr; MTL::Texture* b = nullptr; };
  std::map<std::string, FeedbackPair> feedbackTexBuf;   // key -> {texA, texB}
  std::map<std::string, uint32_t> feedbackW, feedbackH; // realloc key (w/h)
  std::map<std::string, uint32_t> feedbackFmt;          // realloc key (op-chosen pixel format)
  std::map<std::string, bool> feedbackToggle;           // key -> _bufferToggle (flips each cook)
  // Last-resolved OUTPUT textures by feedback node, indexed by Texture2D-output ordinal. Borrowed
  // pointers into feedbackTexBuf's pair / an upstream input (SwapTextures), NOT owned. Both cooks
  // write it at a feedback cook's end so debugCookedFeedbackOutput can read Current/PreviousFrame.
  static constexpr int kMaxFeedbackOut = 4;
  std::map<std::string, std::array<MTL::Texture*, kMaxFeedbackOut>> feedbackOut;

  // ── Per-node CROSS-FRAME BUFFER PAIR (the Points/Buffer-rail feedback = TiXL KeepPreviousPointBuffer,
  // the structured-buffer twin of KeepPreviousFrame above). Mirror of FeedbackPair over MTL::Buffer:
  // two GPU buffers (bufA+bufB) PERSIST across frames + a toggle; the op copies the input into the
  // toggle-selected buffer and exposes BufferA/BufferB. Realloc-keyed on byteSize
  // (KeepPreviousPointBuffer.cs:45 compares SizeInBytes+StructureByteStride). Released on realloc + in
  // ~PointGraph → NO leak/UAF. Keyed flat id.
  struct BufferFeedbackPair { MTL::Buffer* a = nullptr; MTL::Buffer* b = nullptr; };
  std::map<std::string, BufferFeedbackPair> feedbackBufBuf;  // key -> {bufA, bufB}
  std::map<std::string, uint32_t> feedbackBufSize;           // realloc key (byte size)
  std::map<std::string, uint32_t> feedbackBufStride;         // realloc key (element stride)
  std::map<std::string, bool> feedbackBufToggle;             // key -> _toggle (flips each cook)
  // Dual SwBuffer OUTPUTS a feedback-buffer node routed last cook — the SwBuffer views live in Impl's
  // feedbackBufOut map (needs SwBuffer's full def, which this leaf must not pull in); only the resource
  // machine lives here.

  // ── Per-node CROSS-FRAME N-SLICE TEXTURE-ARRAY RING (the persistent-accumulator history machine =
  // TiXL KeepInTextureArray + TimeDisplace). ONE MTL::Texture of TextureType2DArray with arrayLength =
  // N slices that PERSISTS across frames. Each frame the op blits the current source into slice
  // (writeIndex mod N) and reads slice (readIndex mod N) back out — an N-frame history buffer, NOT a
  // 2-buffer ping-pong (the first op in the engine that needs > 2 slices; every existing feedback op
  // uses the pair above). Realloc-keyed on (w,h,fmt,N) — KeepInTextureArray.cs:60-72 recreates the
  // whole array when any of Width/Height/Format/ArraySize changes. Released on realloc + in
  // ~PointGraph → NO leak/UAF. Keyed flat id / resident path.
  std::map<std::string, MTL::Texture*> feedbackArrayTex;  // key -> the N-slice Texture2DArray
  std::map<std::string, uint32_t> feedbackArrayW, feedbackArrayH;  // realloc key (w/h)
  std::map<std::string, uint32_t> feedbackArrayFmt;               // realloc key (pixel format)
  std::map<std::string, uint32_t> feedbackArrayN;                 // realloc key (slice count = N)

  // CROSS-FRAME texture PAIR for a feedback op (KeepPreviousFrame). Sizes BOTH textures (texA + texB) to
  // (w,h,fmt), reallocating BOTH only on a description change (RESOURCE_LIFETIME on a PAIR; TiXL
  // KeepPreviousFrame.cs:46-54 disposes+recreates both on formatChanged). Usage = RenderTarget|ShaderRead
  // (blit copyFromTexture writes + a downstream op/readback shader-reads); StorageMode Shared (getBytes
  // golden). Parked in feedbackTexBuf → released here on realloc AND in ~PointGraph (NO leak, NO UAF).
  // Returns false if either alloc failed.
  bool ensureFeedbackPair(const std::string& key, uint32_t w, uint32_t h, MTL::PixelFormat fmt,
                          MTL::Texture*& outA, MTL::Texture*& outB) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    FeedbackPair& fp = feedbackTexBuf[key];
    const bool changed = !fp.a || !fp.b || feedbackW[key] != w || feedbackH[key] != h ||
                         feedbackFmt[key] != (uint32_t)fmt;
    if (changed) {
      if (fp.a) { fp.a->release(); fp.a = nullptr; }
      if (fp.b) { fp.b->release(); fp.b = nullptr; }
      auto makeTex = [&]() -> MTL::Texture* {
        MTL::TextureDescriptor* td =
            MTL::TextureDescriptor::texture2DDescriptor(fmt, w, h, /*mipped=*/false);
        td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
        td->setStorageMode(MTL::StorageModeShared);
        return dev->newTexture(td);
      };
      fp.a = makeTex();
      fp.b = makeTex();
      feedbackW[key] = w;
      feedbackH[key] = h;
      feedbackFmt[key] = (uint32_t)fmt;
    }
    outA = fp.a;
    outB = fp.b;
    return fp.a && fp.b;
  }

  // CROSS-FRAME BUFFER PAIR for a feedback-buffer op (KeepPreviousPointBuffer). The MTL::Buffer twin of
  // ensureFeedbackPair: sizes BOTH buffers (bufA + bufB) to byteSize, reallocating BOTH only on a
  // size/stride change (RESOURCE_LIFETIME on a PAIR; KeepPreviousPointBuffer.cs:40-46 recreates both when
  // SizeInBytes or StructureByteStride differs). StorageModeShared (a getBytes golden reads it; the blit
  // copyFromBuffer writes it). Parked in feedbackBufBuf → released here on realloc AND in ~PointGraph (NO
  // leak, NO UAF). Returns false if either alloc failed / byteSize is 0.
  bool ensureBufferFeedbackPair(const std::string& key, uint32_t byteSize, uint32_t stride,
                                MTL::Buffer*& outA, MTL::Buffer*& outB) {
    if (byteSize == 0) return false;
    BufferFeedbackPair& fp = feedbackBufBuf[key];
    const bool changed = !fp.a || !fp.b || feedbackBufSize[key] != byteSize ||
                         feedbackBufStride[key] != stride;
    if (changed) {
      if (fp.a) { fp.a->release(); fp.a = nullptr; }
      if (fp.b) { fp.b->release(); fp.b = nullptr; }
      fp.a = dev->newBuffer(byteSize, MTL::ResourceStorageModeShared);
      fp.b = dev->newBuffer(byteSize, MTL::ResourceStorageModeShared);
      feedbackBufSize[key] = byteSize;
      feedbackBufStride[key] = stride;
    }
    outA = fp.a;
    outB = fp.b;
    return fp.a && fp.b;
  }

  // CROSS-FRAME N-SLICE TEXTURE-ARRAY for a history op (KeepInTextureArray / TimeDisplace). Allocates ONE
  // MTL::Texture of TextureType2DArray with arrayLength = N (clamped ≥ 1), sized (w,h,fmt), reallocating
  // only on a (w,h,fmt,N) change (RESOURCE_LIFETIME; KeepInTextureArray.cs:60-72 recreates the whole
  // array when Width/Height/Format/ArraySize differs). Usage = RenderTarget|ShaderRead (a blit
  // copyFromTexture writes a slice; the TimeDisplace shader samples the array). StorageMode Shared
  // (getBytes golden reads any slice). Parked in feedbackArrayTex → released here on realloc AND in
  // ~PointGraph (NO leak, NO UAF). Returns the array texture, or null on alloc failure.
  MTL::Texture* ensureFeedbackArray(const std::string& key, uint32_t w, uint32_t h, MTL::PixelFormat fmt,
                                    uint32_t n) {
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    if (n == 0) n = 1;
    MTL::Texture*& t = feedbackArrayTex[key];
    const bool changed = !t || feedbackArrayW[key] != w || feedbackArrayH[key] != h ||
                         feedbackArrayFmt[key] != (uint32_t)fmt || feedbackArrayN[key] != n;
    if (changed) {
      if (t) { t->release(); t = nullptr; }
      MTL::TextureDescriptor* td = MTL::TextureDescriptor::alloc()->init();
      td->setTextureType(MTL::TextureType2DArray);
      td->setPixelFormat(fmt);
      td->setWidth(w);
      td->setHeight(h);
      td->setArrayLength(n);
      td->setMipmapLevelCount(1);
      td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
      td->setStorageMode(MTL::StorageModeShared);
      t = dev->newTexture(td);
      td->release();
      feedbackArrayW[key] = w;
      feedbackArrayH[key] = h;
      feedbackArrayFmt[key] = (uint32_t)fmt;
      feedbackArrayN[key] = n;
    }
    return t;
  }

  // Release every feedback resource. Called from ~PointGraph (the sole owner) — one place, mirrors the
  // realloc-release rule so there is NO leak on graph teardown. NOT a destructor here (Impl owns the
  // lifetime + orders it against its other maps).
  void releaseFeedbackResources() {
    for (auto& kv : feedbackTexBuf) {  // cross-frame PAIR (KeepPreviousFrame): release BOTH
      if (kv.second.a) kv.second.a->release();
      if (kv.second.b) kv.second.b->release();
    }
    for (auto& kv : feedbackBufBuf) {  // cross-frame BUFFER PAIR (KeepPreviousPointBuffer): release BOTH
      if (kv.second.a) kv.second.a->release();
      if (kv.second.b) kv.second.b->release();
    }
    for (auto& kv : feedbackArrayTex)  // cross-frame N-SLICE ARRAY (KeepInTextureArray/TimeDisplace)
      if (kv.second) kv.second->release();
  }
};

}  // namespace sw
