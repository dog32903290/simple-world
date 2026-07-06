// KeepInTextureArray feedback op — the FIRST N-slice cross-frame history RING in the engine (every
// prior feedback op = a 2-texture ping-pong pair; this one keeps ArraySize slices). Each cook it blits
// the current Image into slice (WriteIndex mod N) of a persistent Texture2DArray, then blits slice
// (ReadIndex mod N) back out into a single-slice SelectedSlice output. That array SURVIVING across
// frames IS the feature (a rolling N-frame buffer TimeDisplace samples per-pixel).
//
// TiXL authority: external/tixl/Operators/Lib/image/use/KeepInTextureArray.cs (ported; line refs to it)
// + KeepInTextureArray.t3 default mirror:
//   - SelectedSlice = Slot<Texture2D?> (cs:10-11) → our "SelectedSlice" Texture2D OUTPUT (ordinal 0).
//   - TextureArray  = Slot<Texture2D?> (cs:13-14) → the WHOLE N-slice array. sw has NO Texture2DArray
//     WIRE currency (SelectedSlice is a normal Texture2D and IS wireable; the array is not). ★NAMED
//     FORK: we DROP the TextureArray output port here — the ONLY sw consumer of the array (TimeDisplace)
//     owns its OWN internal ring (point_ops_timedisplace.cpp), so no cross-op array wire is needed. If a
//     future op must consume a standalone KeepInTextureArray's array, add a Texture2DArray wire type then.
//   - ArraySize   = InputSlot<int>(0)     (cs:175-176) → clamped [1,256] (cs:31). .t3 default 0 → clamp→1.
//   - SourceTexture = InputSlot<Texture2D> (cs:179-180) → our "SourceTexture" Texture2D INPUT (ordinal 0).
//   - TriggerWrite = InputSlot<bool>       (cs:183-184) → Float Widget::Bool; .t3 default FALSE (.t3:21-22).
//   - WriteIndex   = InputSlot<int>        (cs:186-187) → dstSlice = WriteIndex.Mod(arraySize) (cs:112).
//   - ReadIndex    = InputSlot<int>(0)     (cs:189-190) → readIndex = ReadIndex.Mod(N) (cs:43).
//   - guard: SourceTexture null → return (cs:24-29): outputs keep prior value.
//   - CopyIntoArray only when TriggerWrite (cs:35-38); CopySubresourceRegion src mip0 → dstSlice (cs:120).
//   - UpdateSliceToOutput ALWAYS after (cs:43-44): CopySubresourceRegion arraySlice→_sliceTexture (cs:143).
//
// ★CROSS-FRAME STATE FORK (named, FORCED by TiXL parity): the array RING persists between cooks. The
//   ring lives in the driver's FeedbackStore (feedbackArrayTex, sized via ensureFeedbackArray keyed on
//   w/h/fmt/N — cs:60-72's needsRecreate); the SelectedSlice output texture reuses the driver's feedback
//   PAIR slot (pairA = the persistent _sliceTexture; pairB unused). Both released on realloc + in
//   ~PointGraph → NO per-cook leak, NO UAF. The op does NOT allocate (lifetime stays in one place).
//
// injectBug (keepInTextureArrayInjectBug): CORRUPTS the READ slice selection (reads slice 0 instead of
//   ReadIndex mod N) — a real cook-path corruption of the load-bearing slice math, so the golden's
//   "ReadIndex reaches the right history frame" tooth diverges. Off in production.
#include <Metal/Metal.hpp>

#include <cmath>

#include "runtime/image_filter_op_registry.h"        // FeedbackOp (spec + selftest + registerFeedbackOp)
#include "runtime/point_graph.h"                      // FeedbackCookCtx, cookParam, PointFeedbackFn
#include "runtime/point_graph_feedback_store.h"       // FeedbackStore::ensureFeedbackArray (the N-slice ring)

namespace sw {

int runKeepInTextureArraySelfTest(bool injectBug);

// Test-only injection seam: corrupt the READ slice (force slice 0), so ReadIndex no longer reaches the
// authored history frame → the cross-frame slice tooth diverges. Off in production.
bool& keepInTextureArrayInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// Blit one 2D texture region between (texture, slice) endpoints — the Metal restatement of
// CopySubresourceRegion (cs:120 write, cs:143 read). Level 0, whole (w,h) region.
void blitSlice(MTL::CommandQueue* q, const MTL::Texture* src, uint32_t srcSlice, MTL::Texture* dst,
               uint32_t dstSlice, uint32_t w, uint32_t h) {
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  blit->copyFromTexture(const_cast<MTL::Texture*>(src), srcSlice, 0, MTL::Origin::Make(0, 0, 0),
                        MTL::Size::Make(w, h, 1), dst, dstSlice, 0, MTL::Origin::Make(0, 0, 0));
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();  // the readback golden + a same-frame downstream sample need the copy done
}

// cookKeepInTextureArray: (1) size the persistent N-slice ring + the single-slice output. (2) if
// TriggerWrite: blit Image → ring slice (WriteIndex mod N). (3) ALWAYS blit ring slice (ReadIndex mod N)
// → the output slice texture. (4) route SelectedSlice out.
void cookKeepInTextureArray(FeedbackCookCtx& c) {
  const MTL::Texture* image = c.inputTextures[0];  // SourceTexture = first (only) Texture2D input
  // Degenerate guard (cs:24-29 sourceTexture null → return): output stays null (downstream black, no crash).
  if (!image || !c.store || !c.queue) return;

  const uint32_t w = (uint32_t)image->width(), h = (uint32_t)image->height();
  if (w == 0 || h == 0) return;
  const MTL::PixelFormat fmt = image->pixelFormat();

  // ArraySize clamped [1,256] (cs:31). Params arrive as floats (int in TiXL); round-then-clamp.
  int n = (int)std::lround(cookParam(c, "ArraySize", 1.0f));
  if (n < 1) n = 1;
  if (n > 256) n = 256;

  // (1) Persistent ring (feedbackArrayTex, sized w/h/fmt/N) + persistent single-slice output (pairA).
  MTL::Texture* ring = c.store->ensureFeedbackArray(c.nodeKey, w, h, fmt, (uint32_t)n);
  MTL::Texture* sliceOut = nullptr;
  {
    MTL::Texture* a = nullptr;
    MTL::Texture* b = nullptr;  // pairB unused (the array has no ping-pong; one output texture suffices)
    if (c.store->ensureFeedbackPair(c.nodeKey, w, h, fmt, a, b)) sliceOut = a;
  }
  if (!ring || !sliceOut) return;

  // (2) WRITE: blit Image → ring slice (WriteIndex mod N), only when TriggerWrite (cs:35-38). Modulo is a
  // TRUE mod (cs:112 .Mod handles negatives → non-negative); C++ % on a non-negative index matches here.
  const bool triggerWrite = cookParam(c, "TriggerWrite", 0.0f) > 0.5f;  // .t3 default FALSE
  if (triggerWrite) {
    int wi = (int)std::lround(cookParam(c, "WriteIndex", 0.0f));
    uint32_t dstSlice = (uint32_t)(((wi % n) + n) % n);
    blitSlice(c.queue, image, 0, ring, dstSlice, w, h);
  }

  // (3) READ: blit ring slice (ReadIndex mod N) → sliceOut (cs:43-44 ALWAYS runs, even without a write).
  int ri = (int)std::lround(cookParam(c, "ReadIndex", 0.0f));
  uint32_t readSlice = (uint32_t)(((ri % n) + n) % n);
  if (keepInTextureArrayInjectBug()) readSlice = 0;  // corrupt the load-bearing slice math → tooth bites
  blitSlice(c.queue, ring, readSlice, sliceOut, 0, w, h);

  // (4) Route SelectedSlice (ordinal 0).
  c.outputs[0] = sliceOut;
}

}  // namespace

// Self-registration. FeedbackOp: registerFeedbackOp (cross-frame table) + spec sink + selftest sink.
// needsPair=true with the input's format is UNUSED for pre-sizing here (we size the pair OURSELVES to the
// runtime input dims inside the cook, since the op also owns the array). We still pass needsPair=false —
// the driver's pre-size path assumes a 2-buffer ping-pong the array op does not use; the op sizes both the
// ring AND the slice-output pair itself via c.store. Ports: SourceTexture = Texture2D input; ArraySize/
// WriteIndex/ReadIndex = Float (int widget); TriggerWrite = Float Widget::Bool (.t3 default 0); SelectedSlice
// = Texture2D output. TextureArray output is DROPPED (named fork above — no sw array wire currency).
static const FeedbackOp _reg_keepintexturearray{
    {"KeepInTextureArray", "KeepInTextureArray",
     {{"SourceTexture", "SourceTexture", "Texture2D", true},
      {"ArraySize", "ArraySize", "Float", true, 1.0f, 1.0f, 256.0f},   // clamp [1,256] (cs:31); .t3 0→1
      {"WriteIndex", "WriteIndex", "Float", true, 0.0f, 0.0f, 255.0f},
      {"ReadIndex", "ReadIndex", "Float", true, 0.0f, 0.0f, 255.0f},
      {"TriggerWrite", "TriggerWrite", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},  // .t3 FALSE
      {"SelectedSlice", "SelectedSlice", "Texture2D", false}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "KeepInTextureArray", cookKeepInTextureArray, /*needsPair=*/false,
    /*pairFormat=*/0, "keepintexturearray", runKeepInTextureArraySelfTest};

}  // namespace sw
