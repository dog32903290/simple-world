// AfterGlow2 feedback fx — the TWO-COLOR sibling of AfterGlow. Same feedback × multipass compose,
// reusing the SAME two already-built seams with NO new driver wiring:
//   (1) the feedback-pair seam (FeedbackCookCtx pairA/pairB + toggle) — the cross-frame trail that
//       survives between cooks, auto-routed on BOTH flat and resident (R-2);
//   (2) the in-frame multi-pass blur executor (cachedScratchTex / cachedTexPSO) — the separable
//       2-pass blur idiom from point_ops_blur.cpp, run on the CURRENT image each frame.
//
// TiXL authority (ported, NOT replicated as a node graph): external/tixl Operators/Lib/image/fx/
// feedback/AfterGlow2.{cs,t3}. AfterGlow2.cs:13-29 = the OUTER public ports; AfterGlow2.t3 = the
// OUTER defaults + the internal spine. Cut55: the .t3 CombinedLayers Command output + Execute/Camera/
// DrawScreenQuad/GetTextureSize editor plumbing are dropped; only the decay+blur+two-color SPINE is
// ported.
//
// ★BACKWARD-TRACE VERDICT — AfterGlow2's exact DELTA vs AfterGlow (AfterGlow2.t3, cited):
//   DECAY: IDENTICAL to AfterGlow. The trail is RenderTarget(Clear=false) (.t3:88-127, node 3f277e17).
//     A Layer2d e1513904 (.t3:201-235) with Color=Vector4(0,0,~0, DecayRate) (the Vector4 node
//     e7b3fe78 routes outer DecayRate -> W; Vector4.cs:28-29 W=6ce53000; wiring .t3:347-381),
//     BlendMode 0 (normal/over) -> trail *= (1 - DecayRate). DecayRate (.t3:27-28) = 0.0157 per-frame
//     delta. (SOURCE-grounded, same as AfterGlow — NOT decay=Color.)
//   GLOW into trail: DrawScreenQuad 1cb10f4e (.t3:71-81) Texture=Blur(current) (.t3:293-297), Color=
//     the outer COLOR pin (.t3:287-291; default white), BlendMode 1 (add) -> trail += Color.rgb*blur.
//   ★THE TWO-COLOR DELTA — a FINAL Blend 5e367fc1 (.t3:128-159, BlendMode 1 = SCREEN; Blend.cs +
//     Blend.hlsl:115-118  rgb = tA.rgb + tB.rgb*tB.a) that AfterGlow does NOT have. AfterGlow's
//     Output IS the trail; AfterGlow2's Output is:
//        ImageA(t0)=trail RenderTarget (.t3:323-327), ColorA=white default (.t3:133-141, unwired),
//        ImageB(t1)=the ORIGINAL current Image (.t3:329-333), ColorB=outer ORGCOLOR (.t3:317-321).
//     => out.rgb = trail.rgb + (Image.rgb*OrgColor.rgb)*Image.a.
//     So COLOR tints the glow accumulated into the trail (AfterGlow's role), ORGCOLOR tints the crisp
//     ORIGINAL image screen-composited on top each frame (the SECOND glow color; the new pass).
//   PORTS dropped vs AfterGlow (named): ContrastOffset2 (AfterGlow2 Blur.Offset is static -0.04,
//     .t3:171-174, no outer pin) and Resolution (trail tracks input dims, same as AfterGlow). Blur
//     Samples = 8 static (.t3:176-179); BlurAmount default 2.0 (.t3:5-6), GlowImpact 0.7 (.t3:31-32).
#include <Metal/Metal.hpp>

#include <algorithm>
#include <cmath>

#include "runtime/afterglow2_params.h"           // AfterGlow2Params, AFTERGLOW2_Params
#include "runtime/blur_params.h"                 // BlurParams, BLUR_Params (reuse the Blur shader)
#include "runtime/image_filter_op_registry.h"    // FeedbackOp (spec + selftest + registerFeedbackOp sink)
#include "runtime/point_graph.h"                 // FeedbackCookCtx, cookParam, PointFeedbackFn
#include "runtime/tex_op_cache.h"                // cachedTexPSO / cachedScratchTex

namespace sw {

int runAfterGlow2SelfTest(bool injectBug);

// Test-only injection seam: when set, SKIP the toggle flip, so the next frame reads/writes the WRONG
// pair half and the cross-frame trail diverges. Off in production.
bool& afterGlow2InjectBug() {
  static bool b = false;
  return b;
}

namespace {

// One blur pass (mirror of blurPass in point_ops_afterglow.cpp): sample `src` along (dirX,dirY),
// render into `dst`, clear-load. Reuses the existing Blur shader (byte-identical to Blur).
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

// cookAfterGlow2: (1) pick read/write pair halves (read one, write the OTHER). (2) blur the current
// image into scratch (2-pass). (3) PASS 1: fold the trail (prev*decay + Color*blur) into writeTo
// (the pair half = the persistent feedback). (4) PASS 2: terminal two-color compose (trail +
// OrgColor*orig screen) into a frame-stable OUTPUT scratch -> route it. (5) flip toggle EXACTLY once.
void cookAfterGlow2(FeedbackCookCtx& c) {
  const MTL::Texture* image = c.inputTextures[0];  // Image = first (only) Texture2D input
  if (!image || !c.pairA || !c.pairB || !c.toggle || !c.lib) return;

  // Read ONE half, write the OTHER (cross-frame double-buffer; NEVER read-write the same half).
  MTL::Texture* prev = *c.toggle ? c.pairB : c.pairA;     // last frame's trail
  MTL::Texture* writeTo = *c.toggle ? c.pairA : c.pairB;  // this frame's trail (the OTHER half)

  const uint32_t W = (uint32_t)image->width(), H = (uint32_t)image->height();
  if (W == 0 || H == 0) return;
  const MTL::PixelFormat fmt = writeTo->pixelFormat();

  // Outer ports/defaults (AfterGlow2.cs:13-29 + AfterGlow2.t3 OUTER): DecayRate 0.0157, BlurAmount
  // 2.0, GlowImpact 0.7, Color white, OrgColor white. Blur Samples = 8, Offset = -0.04 (both static).
  const float decayRate = cookParam(c, "DecayRate", 0.015666667f);
  const float blurAmount = cookParam(c, "BlurAmount", 2.0f);
  const float glowImpact = cookParam(c, "GlowImpact", 0.7f);
  const float colorR = cookParam(c, "Color.x", 1.0f);
  const float colorG = cookParam(c, "Color.y", 1.0f);
  const float colorB = cookParam(c, "Color.z", 1.0f);
  const float orgR = cookParam(c, "OrgColor.x", 1.0f);
  const float orgG = cookParam(c, "OrgColor.y", 1.0f);
  const float orgB = cookParam(c, "OrgColor.z", 1.0f);
  const float orgA = cookParam(c, "OrgColor.w", 1.0f);  // Blend.hlsl:54 ImageBColor.a gates the screen-add

  // PSOs (cached, device-global): the Blur shader (2-pass) + the two AfterGlow2 passes.
  MTL::RenderPipelineState* psoBlur = cachedTexPSO(c.dev, c.lib, "blur_vs", "blur_fs", fmt);
  MTL::RenderPipelineState* psoTrail =
      cachedTexPSO(c.dev, c.lib, "afterglow2_vs", "afterglow2_trail_fs", fmt);
  MTL::RenderPipelineState* psoComp =
      cachedTexPSO(c.dev, c.lib, "afterglow2_vs", "afterglow2_compose_fs", fmt);
  if (!psoBlur || !psoTrail || !psoComp) return;

  // Linear clamp sampler (= Blur's fixed clamp fork; same as afterglow/bloom/blur).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // SCRATCH (in-frame, fully consumed; DISTINCT stable keys, NOT the cross-frame pair):
  //   blurH/blurOut = the separable blur ping-pong; out = the terminal composite that we route.
  MTL::Texture* blurH = cachedScratchTex(c.dev, fmt, W, H, "afterglow2.blur.h");
  MTL::Texture* blurOut = cachedScratchTex(c.dev, fmt, W, H, "afterglow2.blur.out");
  MTL::Texture* outTex = cachedScratchTex(c.dev, fmt, W, H, "afterglow2.out");
  if (!blurH || !blurOut || !outTex) { samp->release(); return; }

  // (2) Blur current image into blurOut (2 passes: H then V). Size <- BlurAmount, Offset = -0.04
  // (static, AfterGlow2.t3:171-174), Glow2 <- GlowImpact, Samples = 8 (AfterGlow2.t3:176-179).
  BlurParams bp{};
  bp.Size = blurAmount;
  bp.NumberOfSamples = 8.0f;
  bp.Offset = -0.04f;
  bp.Glow2 = glowImpact;
  bp.WidthToHeight = (H > 0) ? (float)W / (float)H : 1.0f;
  bp.DirectionX = 1.0f; bp.DirectionY = 0.0f;  // H pass: image -> blurH
  blurPass(c.queue, psoBlur, samp, const_cast<MTL::Texture*>(image), blurH, bp);
  bp.DirectionX = 0.0f; bp.DirectionY = 1.0f;  // V pass: blurH -> blurOut
  blurPass(c.queue, psoBlur, samp, blurH, blurOut, bp);

  // Shared params for both AfterGlow2 passes.
  AfterGlow2Params ap{};
  ap.Decay = 1.0f - decayRate;  // survival multiplier of the existing trail (~0.984)
  ap.GlowR = colorR; ap.GlowG = colorG; ap.GlowB = colorB;
  ap.OrgR = orgR; ap.OrgG = orgG; ap.OrgB = orgB; ap.OrgA = orgA;

  // (3) PASS 1: trail fold into writeTo (the persistent pair half). Reads prev (t0) + blurOut (t1).
  // Clear-load: every texel fully overwritten (prev/blurOut are sampled SOURCES, not the target).
  {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(writeTo);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
    enc->setRenderPipelineState(psoTrail);
    enc->setFragmentTexture(prev, 0);
    enc->setFragmentTexture(blurOut, 1);
    enc->setFragmentSamplerState(samp, 0);
    enc->setFragmentBytes(&ap, sizeof(AfterGlow2Params), AFTERGLOW2_Params);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }

  // (4) PASS 2: terminal two-color compose into outTex. Reads writeTo=trail (t0) + image=orig (t1).
  // This is the AfterGlow2 delta; it does NOT feed back (OrgColor would compound otherwise).
  {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(outTex);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
    enc->setRenderPipelineState(psoComp);
    enc->setFragmentTexture(writeTo, 0);
    enc->setFragmentTexture(const_cast<MTL::Texture*>(image), 1);
    enc->setFragmentSamplerState(samp, 0);
    enc->setFragmentBytes(&ap, sizeof(AfterGlow2Params), AFTERGLOW2_Params);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();  // the readback golden + a same-frame downstream sample need it done
  }

  samp->release();  // scratch + PSOs are cache-owned (tex_op_cache); the pair is driver-owned

  // (5) Route output = outTex (the terminal two-color compose) + flip the toggle EXACTLY ONCE (the
  // driver's feedbackCooked memo guards a double pull, so we do NOT flip inside any loop).
  c.outputs[0] = outTex;
  if (!afterGlow2InjectBug()) *c.toggle = !*c.toggle;
}

}  // namespace

// Self-registration. FeedbackOp{needsPair=true} -> registerFeedbackOp + spec sink + selftest sink.
// Ports 1:1 with AfterGlow2.cs:13-29; OUTER defaults from AfterGlow2.t3 (Color/OrgColor white). The
// trail pair is sized to the input image's description by the driver (ensureFeedbackPair). Color is a
// Vec4 widget (rgb tint; Color.w unused — DrawScreenQuad add ignores it). OrgColor is a full Vec4: rgb
// tints the terminal screen, and OrgColor.w scales it (Blend.hlsl:54 tB.a = clamp(orig.a)*clamp(OrgColor.a)).
static const FeedbackOp _reg_afterglow2{
    {"AfterGlow2", "AfterGlow2",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // AfterGlow2.t3 OUTER defaults: BlurAmount 2.0, GlowImpact 0.7, DecayRate 0.0157,
      // Color white, OrgColor white.
      {"BlurAmount", "BlurAmount", "Float", true, 2.0f, 0.0f, 100.0f},
      {"GlowImpact", "GlowImpact", "Float", true, 0.7f, 0.0f, 4.0f},
      {"DecayRate", "DecayRate", "Float", true, 0.015666667f, 0.0f, 1.0f},
      {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"OrgColor.x", "OrgColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"OrgColor.y", "OrgColor.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"OrgColor.z", "OrgColor.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"OrgColor.w", "OrgColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "AfterGlow2", cookAfterGlow2, /*needsPair=*/true,
    (uint32_t)MTL::PixelFormatRGBA8Unorm, "afterglow2", runAfterGlow2SelfTest};

}  // namespace sw
