// AfterGlow feedback fx — THE feedback × multipass COMPOSITION proving op. It composes two ALREADY
// BUILT seams with NO new driver wiring:
//   (1) the feedback-pair seam (FeedbackCookCtx pairA/pairB + toggle, point_graph.h:235-254) — a
//       cross-frame double buffer that persists between cooks (the trail SURVIVING IS the feature),
//       auto-routed on BOTH flat and resident by construction (R-2; the driver branches at
//       point_graph.cpp:673 / point_graph_resident.cpp:559 need no edit);
//   (2) the in-frame multi-pass executor (cachedScratchTex / cachedTexPSO, tex_op_cache.h) — the
//       separable 2-pass blur idiom from point_ops_blur.cpp, run on the CURRENT image each frame.
//
// TiXL authority (ported, NOT replicated as a node graph): external/tixl Operators/Lib/image/fx/
// feedback/AfterGlow.{cs,t3}. AfterGlow.cs:12-31 = the OUTER public ports; AfterGlow.t3 = the OUTER
// defaults + the internal spine. Cut55 trap respected: the .t3 is a compound whose CombinedLayers
// Command output + DrawQuad/Camera/_BlobOld plumbing are the editor-preview machinery — we DROP all
// of it and port only the decay+blur+composite SPINE (the single Texture2D Output, AfterGlow.t3
// node 82e6547e RenderTarget(NoClear) <- Layer2d add(blur) + DrawQuad black-over(DecayRate)).
//
// ★DECAY MAPPING — backward-trace verdict (AfterGlow.t3, cite lines):
//   The persistent trail buffer is RenderTarget(Clear=false) (.t3:241-269). Each frame the
//   Camera->Execute 9be0c7e3 draws into it:
//     * DrawQuad 4f2d6e89 (.t3:200-215): Color = Vector4(0,0,0, DecayRate) (Vector4.W <- outer
//       DecayRate, .t3:398-401; X/Y/Z unconnected = 0), BlendMode = 0 (normal/over, DrawQuad.t3
//       default) -> a BLACK quad over the buffer = buffer *= (1 - DecayRate). THIS is the trail
//       decay; DecayRate (.t3:9-10 = 0.0157) is the per-frame decay DELTA.
//     * Layer2d 0750217c (.t3:57-78): Texture = Blur output (.t3:368-371), Color = outer Color pin
//       (.t3:13-20 ~0.59 grey; wired .t3:374-378), BlendMode = 1 (ADD) -> buffer += Color.rgb*blur.
//   => collapsed composite:  out = prev*(1 - DecayRate) + Color.rgb * blur(current).
//
//   ★NAMED FORK / verdict divergence (source-grounded, Guards: 查 TiXL 不發明): the work-order's
//   stated verdict was "decay = Color (~0.59), NOT DecayRate". The .t3 says the OPPOSITE — the frame
//   decay is (1 - DecayRate) ≈ 0.984 (DrawQuad black-over) and Color tints the ADDED glow. We follow
//   the SOURCE. Both pins are still honoured (DecayRate -> survival, Color -> glow tint); the
//   divergence is only about WHICH pin is the multiplicative decay.
//
// The Blur sub-op is wired in AfterGlow.t3 (node efe24551, .t3:318-352) with: Size <- BlurAmount
// (.t3:513-515), Offset <- ContrastOffset2 (.t3:507-509), Opacity <- GlowImpact (.t3:525-527),
// Samples = 12 static (.t3:328-331). GlowImpact thus rides Blur.Opacity (= blur_fs Glow2, rgb mul),
// so the glow's overall strength is applied INSIDE the blur; Color is the additive tint on top.
#include <Metal/Metal.hpp>

#include <algorithm>
#include <cmath>

#include "runtime/afterglow_params.h"            // AfterGlowParams, AFTERGLOW_Params
#include "runtime/blur_params.h"                 // BlurParams, BLUR_Params (reuse the Blur shader)
#include "runtime/image_filter_op_registry.h"    // FeedbackOp (spec + selftest + registerFeedbackOp sink)
#include "runtime/point_graph.h"                 // FeedbackCookCtx, cookParam, PointFeedbackFn
#include "runtime/tex_op_cache.h"                // cachedTexPSO / cachedScratchTex

namespace sw {

int runAfterGlowSelfTest(bool injectBug);

// Test-only injection seam (the golden's RED case corrupts the REAL cook path, not the expected
// value): when set, the op SKIPS the toggle flip, so the next frame reads/writes the WRONG buffer
// and the cross-frame trail diverges. Off in production.
bool& afterGlowInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// One blur pass (mirror of blurPass in point_ops_blur.cpp): sample `src` along (dirX,dirY), render
// into `dst`, clear-load. Reuses the existing Blur shader so the blur math is byte-identical to Blur.
void blurPass(MTL::CommandQueue* q, MTL::RenderPipelineState* rps, MTL::SamplerState* samp,
              MTL::Texture* src, MTL::Texture* dst, const BlurParams& p) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(dst);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(src, 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(BlurParams), BLUR_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// cookAfterGlow: the compose. (1) pick read/write pair halves (read one, write the OTHER — never the
// same half). (2) blur the current image into scratch (in-frame multi-pass, fully consumed). (3)
// composite prev*decay + glow into writeTo. (4) route writeTo out + flip toggle EXACTLY once.
void cookAfterGlow(FeedbackCookCtx& c) {
  const MTL::Texture* image = c.inputTextures[0];  // Image = first (only) Texture2D input
  // Degenerate guards: no image wired, or the driver could not size the pair -> nothing to route
  // (mirrors KeepPreviousFrame; the output stays null = downstream black, no crash).
  if (!image || !c.pairA || !c.pairB || !c.toggle || !c.lib) return;

  // Read ONE half, write the OTHER (cross-frame double-buffer; NEVER read-write the same half).
  MTL::Texture* prev = *c.toggle ? c.pairB : c.pairA;     // last frame's trail
  MTL::Texture* writeTo = *c.toggle ? c.pairA : c.pairB;  // this frame's trail (the OTHER half)

  const uint32_t W = (uint32_t)image->width(), H = (uint32_t)image->height();
  if (W == 0 || H == 0) return;
  const MTL::PixelFormat fmt = writeTo->pixelFormat();

  // Outer ports/defaults (AfterGlow.cs:12-31 + AfterGlow.t3 OUTER): DecayRate 0.0157, BlurAmount 0.1,
  // GlowImpact 0.7, ContrastOffset2 -0.76, Color ~0.59 grey. Samples = 12 (Blur static, .t3:328-331).
  const float decayRate = cookParam(c, "DecayRate", 0.015666667f);
  const float blurAmount = cookParam(c, "BlurAmount", 0.1f);
  const float glowImpact = cookParam(c, "GlowImpact", 0.7f);
  const float contrastOffset2 = cookParam(c, "ContrastOffset2", -0.76f);
  const float colorR = cookParam(c, "Color.x", 0.5928854f);
  const float colorG = cookParam(c, "Color.y", 0.5928794f);
  const float colorB = cookParam(c, "Color.z", 0.5928794f);

  // PSOs (cached, device-global): the Blur shader (2-pass) + the AfterGlow composite (2-input).
  MTL::RenderPipelineState* psoBlur = cachedTexPSO(c.dev, c.lib, "blur_vs", "blur_fs", fmt);
  MTL::RenderPipelineState* psoComp =
      cachedTexPSO(c.dev, c.lib, "afterglow_vs", "afterglow_composite_fs", fmt);
  if (!psoBlur || !psoComp) return;

  // Linear clamp sampler (= Blur's fixed clamp fork; same as bloom/blur).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // SCRATCH (in-frame, fully consumed): the blur ping-pong + the blurred result. DISTINCT stable keys
  // (frame-stable, reused; NOT the cross-frame pair — never write the trail into scratch).
  MTL::Texture* blurH = cachedScratchTex(c.dev, fmt, W, H, "afterglow.blur.h");
  MTL::Texture* blurOut = cachedScratchTex(c.dev, fmt, W, H, "afterglow.blur.out");
  if (!blurH || !blurOut) { samp->release(); return; }

  // (2) Blur current image into blurOut (2 passes: H then V). Size <- BlurAmount, Offset <-
  // ContrastOffset2, Opacity(Glow2) <- GlowImpact, Samples = 12 (AfterGlow.t3 Blur node wiring).
  BlurParams bp{};
  bp.Size = blurAmount;
  bp.NumberOfSamples = 12.0f;
  bp.Offset = contrastOffset2;
  bp.Glow2 = glowImpact;
  bp.WidthToHeight = (H > 0) ? (float)W / (float)H : 1.0f;
  bp.DirectionX = 1.0f; bp.DirectionY = 0.0f;  // H pass: image -> blurH
  blurPass(c.queue, psoBlur, samp, const_cast<MTL::Texture*>(image), blurH, bp);
  bp.DirectionX = 0.0f; bp.DirectionY = 1.0f;  // V pass: blurH -> blurOut
  blurPass(c.queue, psoBlur, samp, blurH, blurOut, bp);

  // (3) Composite into writeTo: out = prev*(1 - DecayRate) + Color.rgb * blurOut. One fullscreen pass
  // reading BOTH prev (texture 0) and the blurred current (texture 1). Clear-load (we fully overwrite
  // every texel; reading prev as a sampled SOURCE, not as the render target — no read-write hazard).
  {
    AfterGlowParams ap{};
    ap.Decay = 1.0f - decayRate;  // survival multiplier of the existing trail (~0.984)
    ap.GlowR = colorR; ap.GlowG = colorG; ap.GlowB = colorB;
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(writeTo);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
    enc->setRenderPipelineState(psoComp);
    enc->setFragmentTexture(prev, 0);
    enc->setFragmentTexture(blurOut, 1);
    enc->setFragmentSamplerState(samp, 0);
    enc->setFragmentBytes(&ap, sizeof(AfterGlowParams), AFTERGLOW_Params);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();  // the readback golden + a same-frame downstream sample need it done
  }

  samp->release();  // scratch + PSOs are cache-owned (tex_op_cache); the pair is driver-owned

  // (4) Route output = writeTo (the just-composited trail) + flip the toggle EXACTLY ONCE (the
  // driver's feedbackCooked memo already guards a double pull, so we do NOT flip inside any loop).
  c.outputs[0] = writeTo;
  if (!afterGlowInjectBug()) *c.toggle = !*c.toggle;
}

}  // namespace

// Self-registration. FeedbackOp feeds: registerFeedbackOp (cross-frame ping-pong table) + the spec
// sink (findSpec) + the selftest sink (run_all discovers --selftest-afterglow). needsPair=true with
// RGBA8Unorm (kPointTargetFormat = the engine's frame texture format) — the persistent trail pair.
// One Texture2D input + one Texture2D output, no special multi-output ports -> the driver auto-routes
// outputs[0] on BOTH flat and resident (R-2). Ports 1:1 with AfterGlow.cs:12-31; OUTER defaults from
// AfterGlow.t3 (NOT the internal child literals). Resolution is dropped (named): the trail pair is
// sized to the input image's description by the driver (ensureFeedbackPair), so there is no separate
// output-resolution pick — TiXL's Resolution pin sized the internal RenderTarget(NoClear); here the
// pair tracks the input dims (realloc-on-resize resets the trail, which is acceptable per the
// work-order). Color is a Vec4 widget (rgb tint; W=1 alpha is unused by the composite).
static const FeedbackOp _reg_afterglow{
    {"AfterGlow", "AfterGlow",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // AfterGlow.t3 OUTER defaults: BlurAmount 0.1, GlowImpact 0.7, DecayRate 0.0157,
      // ContrastOffset2 -0.76, Color ~0.59 grey.
      {"BlurAmount", "BlurAmount", "Float", true, 0.1f, 0.0f, 100.0f},
      {"GlowImpact", "GlowImpact", "Float", true, 0.7f, 0.0f, 4.0f},
      {"DecayRate", "DecayRate", "Float", true, 0.015666667f, 0.0f, 1.0f},
      {"ContrastOffset2", "ContrastOffset2", "Float", true, -0.76f, -1.0f, 1.0f},
      {"Color.x", "Color", "Float", true, 0.5928854f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Color.y", "Color.y", "Float", true, 0.5928794f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"Color.z", "Color.z", "Float", true, 0.5928794f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "AfterGlow", cookAfterGlow, /*needsPair=*/true,
    (uint32_t)MTL::PixelFormatRGBA8Unorm, "afterglow", runAfterGlowSelfTest};

}  // namespace sw
