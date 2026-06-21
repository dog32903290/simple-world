// KeepPreviousFrame feedback op (the cross-frame ping-pong flow — the FIRST cross-frame texture STATE
// in the engine). It hands back the PREVIOUS frame's image while writing the CURRENT one into the
// other half of a double-buffer pair, flipping a toggle each frame. Named point_ops_*.cpp so the CMake
// glob (SW_POINT_OP_SRCS) picks it up — same convention as every other op leaf.
//
// TiXL authority: external/tixl/Operators/Lib/image/use/KeepPreviousFrame.cs (ported VERBATIM below —
// every line ref is to that file) + KeepPreviousFrame.t3 default mirror:
//   - PreviousFrame = Slot<Texture2D>  (cs:8-9)  → our "PreviousFrame" Texture2D OUTPUT (spec port idx 2).
//   - CurrentFrame  = Slot<Texture2D>  (cs:11-12) → our "CurrentFrame" Texture2D OUTPUT (spec port idx 3).
//   - ImageA        = InputSlot<Texture2D> (cs:85-86) → our "ImageA" Texture2D INPUT; .t3 default null.
//   - Keep          = InputSlot<bool>      (cs:88-89) → Float Widget::Bool; .t3 DefaultValue = TRUE (.t3:6).
//   - guard: if !ImageA.HasInputConnections || !Keep → return (cs:24-27): outputs keep their prior value.
//   - formatChanged → recreate BOTH _prevTextureA/_prevTextureB sized to ImageA.Description (cs:35-54);
//     our driver handles this realloc via Impl::ensureFeedbackPair (keyed on input w/h/fmt).
//   - CopyResource(texture, _bufferToggle ? _prevTextureA : _prevTextureB) (cs:56) → blit copyFromTexture
//     into the toggle-selected buffer.
//   - CurrentFrame  = _bufferToggle ? _prevTextureA : _prevTextureB (cs:63) — the JUST-written buffer.
//   - PreviousFrame = _bufferToggle ? _prevTextureB : _prevTextureA (cs:64) — the OTHER (last frame's).
//   - _bufferToggle = !_bufferToggle (cs:68) — flip once per frame.
//
// ★CROSS-FRAME STATE FORK (named, FORCED by TiXL parity — not a taste call): every other tex op is
//   single-frame (re-derived each cook). KeepPreviousFrame's pair PERSISTS between cooks ON PURPOSE
//   (that survival IS the feature). The pair + toggle live in Impl (feedbackTexBuf/feedbackToggle),
//   sized via ensureFeedbackPair (RGBA8Unorm = kPointTargetFormat, the engine's frame texture format),
//   released on realloc + in ~PointGraph → NO per-cook leak, NO UAF. The op does NOT allocate the
//   pair (the driver does, so the lifetime stays in one place); the op only blits + routes.
//
// FORK (named): TiXL keys formatChanged on the FULL Texture2DDescription (mip levels, sample count,
//   option flags — cs:35-42); our engine's frame textures are always non-mipped single-sample
//   RGBA8Unorm, so ensureFeedbackPair keys on (w,h,fmt) only — the remaining description fields are
//   constant in this engine (a downstream mipped/MSAA feedback would need them, parked). Byte-identical
//   for every realizable input here (RenderTarget / image-filter output = non-mipped RGBA8Unorm).
#include <Metal/Metal.hpp>

#include "runtime/image_filter_op_registry.h"  // FeedbackOp (spec + selftest + registerFeedbackOp sink)
#include "runtime/point_graph.h"                // FeedbackCookCtx, cookParam, PointFeedbackFn

namespace sw {

int runKeepPreviousFrameSelfTest(bool injectBug);

// Test-only injection seam (the golden's RED case corrupts the REAL cook path, not the expected value):
// when set, the op SKIPS the toggle flip so the next frame reads the wrong buffer (= "toggle wrong" /
// "漏 copy"-class regression the refuter names). Off in production.
bool& keepPreviousFrameInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// cookKeepPreviousFrame: blit ImageA into the toggle-selected pair buffer, route CurrentFrame/PreviousFrame
// to the just-written / other buffer, then flip the toggle. The driver has already sized the pair to
// ImageA's description (ensureFeedbackPair) and supplied the persistent toggle.
void cookKeepPreviousFrame(FeedbackCookCtx& c) {
  const MTL::Texture* image = c.inputTextures[0];  // ImageA = first (only) Texture2D input
  const bool keep = cookParam(c, "Keep", 1.0f) > 0.5f;  // .t3 default TRUE (cs:88-89 / .t3:6)

  // Degenerate guard: no ImageA wired OR the driver could not size the pair → nothing to route
  // (cs:24 !HasInputConnections → return). Both outputs stay null (a downstream sees black, no crash).
  if (!image || !c.pairA || !c.pairB || !c.toggle) return;

  // Keep-off guard (cs:23-27 !keep → return BEFORE the cs:56 copy and cs:68 flip): route the EXISTING
  // pair by the current (un-advanced) toggle WITHOUT copying or flipping, so the previous frame's
  // content stays addressable and the toggle does not advance (TiXL leaves the Slot values intact).
  if (!keep) {
    const bool tog = *c.toggle;
    c.outputs[0] = tog ? c.pairB : c.pairA;  // PreviousFrame (cs:64)
    c.outputs[1] = tog ? c.pairA : c.pairB;  // CurrentFrame  (cs:63)
    return;
  }

  const bool tog = *c.toggle;
  MTL::Texture* writeTo = tog ? c.pairA : c.pairB;  // cs:56 (_bufferToggle ? A : B)
  MTL::Texture* current = writeTo;                  // cs:63 CurrentFrame = the just-written buffer
  MTL::Texture* previous = tog ? c.pairB : c.pairA; // cs:64 PreviousFrame = the OTHER buffer

  // CopyResource(texture, writeTo) (cs:56) → Metal blit copyFromTexture (level 0, whole 2D region).
  const uint32_t w = (uint32_t)image->width();
  const uint32_t h = (uint32_t)image->height();
  {
    MTL::CommandBuffer* mc = c.queue->commandBuffer();
    MTL::BlitCommandEncoder* blit = mc->blitCommandEncoder();
    blit->copyFromTexture(const_cast<MTL::Texture*>(image), 0, 0, MTL::Origin::Make(0, 0, 0),
                          MTL::Size::Make(w, h, 1), writeTo, 0, 0, MTL::Origin::Make(0, 0, 0));
    blit->endEncoding();
    mc->commit();
    mc->waitUntilCompleted();  // the readback golden + a same-frame downstream sample need the copy done
  }

  // Route outputs by OUTPUT-port-relative ordinal: 0 = PreviousFrame (spec port idx 2), 1 = CurrentFrame
  // (spec port idx 3). The driver returns outputs[ordinal] for the wire's output port.
  c.outputs[0] = previous;  // PreviousFrame (cs:64)
  c.outputs[1] = current;   // CurrentFrame  (cs:63)

  // Flip the persistent toggle (cs:68) — UNLESS injectBug suppresses it (then next frame reads the
  // wrong buffer → the PreviousFrame readback diverges → the RED tooth bites).
  if (!keepPreviousFrameInjectBug()) *c.toggle = !*c.toggle;
}

}  // namespace

// Self-registration. FeedbackOp feeds: registerFeedbackOp (the cross-frame ping-pong table, NOT
// texReg) + the spec sink (so findSpec sees it) + the selftest sink (so run_all discovers
// --selftest-keeppreviousframe). needsPair=true with RGBA8Unorm (kPointTargetFormat = the engine's
// frame texture format) — the persistent double-buffer pair.
//   Ports: ImageA = Texture2D input; Keep = Float Widget::Bool (.t3 default 1=TRUE); PreviousFrame +
//          CurrentFrame = Texture2D outputs (PreviousFrame FIRST, matching cs:8-12 declaration order).
static const FeedbackOp _reg_keeppreviousframe{
    {"KeepPreviousFrame", "KeepPreviousFrame",
     {{"ImageA", "ImageA", "Texture2D", true},
      {"Keep", "Keep", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},  // .t3 DefaultValue = true (.t3:6)
      {"PreviousFrame", "PreviousFrame", "Texture2D", false},
      {"CurrentFrame", "CurrentFrame", "Texture2D", false}},
     /*evaluate=*/nullptr},  // Texture2D outputs cannot ride NodeSpec::evaluate (returns ONE float)
    "KeepPreviousFrame", cookKeepPreviousFrame, /*needsPair=*/true,
    (uint32_t)MTL::PixelFormatRGBA8Unorm, "keeppreviousframe", runKeepPreviousFrameSelfTest};

}  // namespace sw
