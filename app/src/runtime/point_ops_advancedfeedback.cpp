// AdvancedFeedback feedback×multipass VJ warp — each frame the PREVIOUS trail is Blurred, value-range
// stabilized (FeedbackAdjust), Displaced (using the blur as the displacement map), Transformed
// (Zoom/Rotate/Offset shrink+shift), then the CURRENT input is composited OVER the warped trail.
// The trail SURVIVING and WARPING across frames IS the effect.
//
// Composition of ALREADY-BUILT seams (no new driver wiring):
//   * feedback-pair seam (FeedbackCookCtx pairA/pairB + toggle, point_graph.h:235) — the cross-frame
//     double buffer (clone of point_ops_afterglow.cpp; R-2 auto on flat + resident).
//   * landed warp PSOs reused verbatim: blur_vs/blur_fs (point_ops_blur.cpp), displace_vs/displace_fs
//     (point_ops_displace.cpp), transformimage_vs/transformimage_fs (point_ops_transformimage.cpp).
//   * the ONE new shader: feedbackadjust_vs/_fs + feedbackadjust_composite_fs (app/shaders/
//     feedbackadjust.metal), port of TiXL FeedbackAdjustImage.hlsl.
//
// ===== Backward-trace of the per-frame warp chain (AdvancedFeedback.t3 + .cs + .hlsl, cited) =====
// The compound's spine is a RenderTarget(Clear=true at node 9d6f4cb6) whose persistent texture (read
// via UseTextureReference 03d46d71, out 7e846b2f -> Blur+FeedbackAdjust) is the trail buffer == our
// feedback pair. Resolving the .t3 Connections (lines 352-569; outer port = GUID 0000…):
//   * Blur (c96a785d): Image <- prev trail (.t3:533-538);  Size <- outer BlurRadius (4.0) (.t3:527-532);
//                      Samples = 40 literal (.t3:304-307);  Wrap = MirrorOnce (.t3:313-317).
//   * FeedbackAdjust (_AdjustFeedbackImage 4318ad5e): Image <- prev trail (.t3:431-436);
//       SampleRadius <- outer SampleRadius (1.263) (.t3:401-406); ShiftSaturation <- outer
//       ShiftSaturation (.t3:407-412); ShiftHue -> Hue <- outer ShiftHue (0.3) (.t3:413-418);
//       ShiftBrightness <- outer ShiftBrightness (.t3:419-424); LimitBrights <- outer LimitBrights
//       (1.0) (.t3:425-430); AmplifyEdges -> DetectEdges <- outer AmplifyEdges (0.141) (.t3:437-442);
//       LimitDarks is the child LITERAL 0.32 (NOT outer; no connection) (.t3:174-177). Hue/Sat passed
//       RAW (no ×360) — _AdjustFeedbackImage.t3 has no Multiply on the path; 0.3 = 0.3°, faithful.
//   * Displace (b3a97d7b): Image <- FeedbackAdjust output (.t3:515-520);  DisplaceMap(SampleTexture
//       3b5b278d) <- Blur output (.t3:503-508);  Twist <- outer Twist (-15.0) (.t3:521-526);
//       ★Displacement <- Multiply(B=0.01) <- outer Displacement (15) => 0.15 (.t3:497-502,123-133);
//       ★DisplacementOffset <- Multiply(B=0.01) <- outer DisplaceOffset (0) => 0 (.t3:491-496,491-);
//       ★Shade <- Multiply(B=0.01) <- outer Shade (0.4) => 0.004 (.t3:509-514,212-221);  WrapMode =
//       MirrorOnce literal (.t3:282-285).  (The ×0.01 on Displacement/Offset/Shade is the .t3 wiring;
//       warp would be 100× too strong without it.)
//   * Layer2d/Transform (195de773): Texture <- Displace output (.t3:359-364);  Scale <- outer Zoom
//       (.t3:365-370);  Position <- outer Offset (.t3:371-376);  Rotation <- outer Rotate
//       (.t3:377-382);  BlendMode = 0 (normal) (.t3:114-118).
//   * Composite: the CURRENT input (AdvancedFeedback Command/Image = c.inputTextures[0]) drawn OVER
//       the transformed trail (Layer2d BlendMode 0 = trail base, current over) -> writeTo.
//
// ★DECAY IS IMPLICIT (NO DecayRate scalar): decay = Transform Scale (default 1.0; the 0.998 shrink is
//   driven via Zoom when the user/golden sets it) + FeedbackAdjust LimitDarks/LimitBrights value-range
//   pull + ShiftBrightness. We port the FeedbackAdjust constants exactly so the loop stays stable
//   (wrong stabilizer => runaway white-out or black-hole). See feedbackadjust.metal.
//
// ★NAMED FORK vs the work-order's stated pass order. The brief listed Blur->Displace->Transform->
//   FeedbackAdjust (FeedbackAdjust last). The .t3 SOURCE says FeedbackAdjust feeds Displace.Image
//   (FeedbackAdjust is BEFORE Displace, in parallel with Blur off the same prev trail), and Layer2d/
//   Transform is LAST. We follow the SOURCE (Guards: 查 TiXL 不發明): order = Blur+FeedbackAdjust(prev)
//   -> Displace(adjust, map=blur) -> Transform -> composite current over.
//
// ★NAMED FORK: outer Zoom/Offset defaults are 1.0 / (0,0) (AdvancedFeedback.t3:67-70,20-26). The child
//   Layer2d's stored literals 0.998 / (0,0.003) are OVERRIDDEN by the connections to the outer ports
//   (TiXL: a connected input ignores its literal). So at pure defaults there is NO geometric shrink/
//   shift — the implicit-trail-shrink the brief describes happens when Zoom is set to 0.998 (which the
//   golden drives via param-path). Faithful to the connection-wins semantics.
//
// ★NAMED FORK: WrapMode. TiXL Blur/Displace use MirrorOnce; the reused Metal PSOs sample with a fixed
//   linear CLAMP sampler (same fork class already shipped for Blur/Displace/AfterGlow). At the band
//   the edge is invisible; carried forward unchanged.
//
// ★NAMED FORK: outer Reset (af523ebc) gates RenderTarget.Clear via And/Or/HasTimeChanged glue
//   (.t3 nodes d8a9130e/fd9aff2c/35f6c959). Cut55 collapse: we map outer Reset -> clear-the-pair
//   (re-init BOTH halves to black), the single observable effect of that glue.
#include <Metal/Metal.hpp>

#include <algorithm>
#include <cmath>

#include "runtime/blur_params.h"                  // BlurParams, BLUR_Params (reuse Blur shader)
#include "runtime/displace_params.h"              // DisplaceParams, DISPLACE_Params (reuse Displace)
#include "runtime/feedbackadjust_params.h"        // FeedbackAdjustParams, FEEDBACKADJUST_Params (NEW)
#include "runtime/image_filter_op_registry.h"     // FeedbackOp (spec + selftest + registerFeedbackOp)
#include "runtime/point_graph.h"                   // FeedbackCookCtx, cookParam
#include "runtime/transformimage_params.h"        // TransformImageParams, TI_Params (reuse Transform)
#include "runtime/tex_op_cache.h"                  // cachedTexPSO / cachedScratchTex

namespace sw {

int runAdvancedFeedbackSelfTest(bool injectBug);

// Test-only injection seam (RED case corrupts the REAL cook path): when set, the op SKIPS the toggle
// flip, so the next frame reads/writes the WRONG buffer and the cross-frame trail diverges. Off in
// production. (Same idiom as afterGlowInjectBug.)
bool& advancedFeedbackInjectBug() {
  static bool b = false;
  return b;
}

namespace {

// One fullscreen render pass: bind `pso`, sample tex0 (+optional tex1), write `dst` (clear-load),
// upload `params` of `paramsLen` bytes at `paramSlot`. Shared by every warp pass below.
void fxPass(MTL::CommandQueue* q, MTL::RenderPipelineState* pso, MTL::SamplerState* samp,
            MTL::Texture* tex0, MTL::Texture* tex1, MTL::Texture* dst,
            const void* params, size_t paramsLen, int paramSlot) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(dst);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(pso);
  enc->setFragmentTexture(tex0, 0);
  if (tex1) enc->setFragmentTexture(tex1, 1);
  enc->setFragmentSamplerState(samp, 0);
  if (params && paramsLen) enc->setFragmentBytes(params, paramsLen, paramSlot);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// Clear a texture to opaque black (for Reset / pair re-init).
void clearTex(MTL::CommandQueue* q, MTL::Texture* t) {
  if (!t) return;
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(t);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void cookAdvancedFeedback(FeedbackCookCtx& c) {
  const MTL::Texture* image = c.inputTextures[0];  // CURRENT content (Command/Image input)
  if (!image || !c.pairA || !c.pairB || !c.toggle || !c.lib) return;

  // Read ONE half (prev trail), write the OTHER (this frame's trail) — never read-write the same half.
  MTL::Texture* prev = *c.toggle ? c.pairB : c.pairA;
  MTL::Texture* writeTo = *c.toggle ? c.pairA : c.pairB;

  const uint32_t W = (uint32_t)image->width(), H = (uint32_t)image->height();
  if (W == 0 || H == 0) return;
  const MTL::PixelFormat fmt = writeTo->pixelFormat();

  // Reset (outer Reset gate, Cut55 collapse): re-init BOTH halves to black BEFORE warping. The fresh
  // black prev then produces a black trail and the current image alone is composited (a clean restart).
  if (cookParam(c, "Reset", 0.0f) > 0.5f) {
    clearTex(c.queue, c.pairA);
    clearTex(c.queue, c.pairB);
  }

  // ---- Outer ports / defaults (AdvancedFeedback.cs:9-61 + .t3:9-78 OUTER) -------------------------
  const float blurRadius     = cookParam(c, "BlurRadius", 4.0f);
  const float sampleRadius   = cookParam(c, "SampleRadius", 1.263f);
  const float amplifyEdges   = cookParam(c, "AmplifyEdges", 0.141f);
  const float limitBrights   = cookParam(c, "LimitBrights", 1.0f);
  const float shiftHue       = cookParam(c, "ShiftHue", 0.3f);
  const float shiftSat       = cookParam(c, "ShiftSaturation", 0.0f);
  const float shiftBright    = cookParam(c, "ShiftBrightness", 0.0f);
  const float displacement   = cookParam(c, "Displacement", 15.0f);
  const float displaceOffset = cookParam(c, "DisplaceOffset", 0.0f);
  const float shade          = cookParam(c, "Shade", 0.4f);
  const float twist          = cookParam(c, "Twist", -15.0f);
  const float zoom           = cookParam(c, "Zoom", 1.0f);
  const float rotate         = cookParam(c, "Rotate", 0.0f);
  const float offsetX        = cookParam(c, "Offset.x", 0.0f);
  const float offsetY        = cookParam(c, "Offset.y", 0.0f);
  // LimitDarks is the child literal 0.32 (no outer port; not connected) — fixed.
  const float limitDarks     = 0.32f;

  // ---- PSOs (cached, device-global): 3 reused warp PSOs + 1 new adjust + 1 new composite ----------
  MTL::RenderPipelineState* psoBlur =
      cachedTexPSO(c.dev, c.lib, "blur_vs", "blur_fs", fmt);
  MTL::RenderPipelineState* psoAdjust =
      cachedTexPSO(c.dev, c.lib, "feedbackadjust_vs", "feedbackadjust_fs", fmt);
  MTL::RenderPipelineState* psoDisplace =
      cachedTexPSO(c.dev, c.lib, "displace_vs", "displace_fs", fmt);
  MTL::RenderPipelineState* psoTransform =
      cachedTexPSO(c.dev, c.lib, "transformimage_vs", "transformimage_fs", fmt);
  MTL::RenderPipelineState* psoComposite =
      cachedTexPSO(c.dev, c.lib, "feedbackadjust_vs", "feedbackadjust_composite_fs", fmt);
  if (!psoBlur || !psoAdjust || !psoDisplace || !psoTransform || !psoComposite) return;

  // Linear clamp sampler (shared fork with Blur/Displace/AfterGlow; named in the header).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // SCRATCH (in-frame, fully consumed; op-namespaced stable keys — never the cross-frame pair).
  MTL::Texture* blurH     = cachedScratchTex(c.dev, fmt, W, H, "advancedfeedback.blur.h");
  MTL::Texture* blurOut   = cachedScratchTex(c.dev, fmt, W, H, "advancedfeedback.blur.out");
  MTL::Texture* adjustOut = cachedScratchTex(c.dev, fmt, W, H, "advancedfeedback.adjust.out");
  MTL::Texture* dispOut   = cachedScratchTex(c.dev, fmt, W, H, "advancedfeedback.displace.out");
  MTL::Texture* xformOut  = cachedScratchTex(c.dev, fmt, W, H, "advancedfeedback.transform.out");
  if (!blurH || !blurOut || !adjustOut || !dispOut || !xformOut) { samp->release(); return; }

  // (1) Blur(prev) -> blurOut. 2 passes (H then V), Size <- BlurRadius, Samples 40, Glow2/Offset
  //     neutral (no glow/offset wiring in AdvancedFeedback's Blur node). MirrorOnce -> clamp fork.
  {
    BlurParams bp{};
    bp.Size = blurRadius;
    bp.NumberOfSamples = 40.0f;
    bp.Offset = 0.0f;
    bp.Glow2 = 1.0f;
    bp.WidthToHeight = (H > 0) ? (float)W / (float)H : 1.0f;
    bp.DirectionX = 1.0f; bp.DirectionY = 0.0f;
    fxPass(c.queue, psoBlur, samp, const_cast<MTL::Texture*>(prev), nullptr, blurH,
           &bp, sizeof(bp), BLUR_Params);
    bp.DirectionX = 0.0f; bp.DirectionY = 1.0f;
    fxPass(c.queue, psoBlur, samp, blurH, nullptr, blurOut, &bp, sizeof(bp), BLUR_Params);
  }

  // (2) FeedbackAdjust(prev) -> adjustOut. The value-range stabilizer (decay-implicit). Hue/Sat RAW.
  {
    FeedbackAdjustParams fp{};
    fp.LimitDarks = limitDarks;
    fp.LimitBrights = limitBrights;
    fp.ShiftBrightness = shiftBright;
    fp.Hue = shiftHue;
    fp.Saturation = shiftSat;
    fp.DetectEdges = amplifyEdges;
    fp.SampleRadius = sampleRadius;
    fxPass(c.queue, psoAdjust, samp, const_cast<MTL::Texture*>(prev), nullptr, adjustOut,
           &fp, sizeof(fp), FEEDBACKADJUST_Params);
  }

  // (3) Displace(adjustOut, DisplaceMap = blurOut) -> dispOut. ★×0.01 on Displacement/Offset/Shade.
  {
    DisplaceParams dp{};
    dp.DisplaceAmount = displacement * 0.01f;       // outer Displacement(15) ×0.01 = 0.15
    dp.DisplaceOffset = displaceOffset * 0.01f;     // outer DisplaceOffset(0) ×0.01 = 0
    dp.Twist = twist;                               // outer Twist (-15°) direct
    dp.Shade = shade * 0.01f;                        // outer Shade(0.4) ×0.01 = 0.004
    dp.DisplaceMapOffsetX = 0.0f; dp.DisplaceMapOffsetY = 0.0f;
    dp.SampleRadius = 1.0f;                          // Displace.SampleRadius default (not the FA one)
    dp.DisplaceMode = 0.0f;                          // IntensityGradient
    dp.UseRGSSMultiSampling = 0.0f;
    dp.TargetWidth = (float)W; dp.TargetHeight = (float)H;
    fxPass(c.queue, psoDisplace, samp, adjustOut, blurOut, dispOut, &dp, sizeof(dp), DISPLACE_Params);
  }

  // (4) Transform(dispOut) -> xformOut. Scale <- Zoom, Rotation <- Rotate, Offset <- Offset, Stretch 1.
  {
    TransformImageParams tp{};
    tp.OffsetX = offsetX; tp.OffsetY = offsetY;
    tp.StretchX = 1.0f; tp.StretchY = 1.0f;
    tp.Scale = zoom;
    tp.Rotation = rotate;
    tp.RepeatMode = 0.0f;
    tp.OrgResolutionX = (int)W; tp.OrgResolutionY = (int)H;
    tp.TargetResolutionX = (int)W; tp.TargetResolutionY = (int)H;
    fxPass(c.queue, psoTransform, samp, dispOut, nullptr, xformOut, &tp, sizeof(tp), TI_Params);
  }

  // (5) Composite: current input OVER the warped trail (BlendMode 0). texture(0)=trail(xformOut, base),
  //     texture(1)=current image (over). -> writeTo.
  fxPass(c.queue, psoComposite, samp, xformOut, const_cast<MTL::Texture*>(image), writeTo,
         nullptr, 0, 0);

  samp->release();  // scratch + PSOs are cache-owned; the pair is driver-owned.

  // (6) Route output = writeTo + flip toggle EXACTLY once (the driver's feedbackCooked memo guards a
  //     double pull, so we do NOT flip inside any loop). injectBug suppresses the flip (RED tooth).
  c.outputs[0] = writeTo;
  if (!advancedFeedbackInjectBug()) *c.toggle = !*c.toggle;
}

}  // namespace

// Self-registration. FeedbackOp feeds registerFeedbackOp (cross-frame ping-pong) + the spec sink
// (findSpec) + the selftest sink (run_all discovers --selftest-advancedfeedback). needsPair=true with
// RGBA8Unorm (= the engine frame format). Ports 1:1 with AdvancedFeedback.cs:9-61 (append-only); OUTER
// defaults from AdvancedFeedback.t3:9-78 (NOT the internal child literals). IsEnabled is carried as a
// Bool port (the .cs IsEnabled pin); the cook always runs (a disabled feedback op would freeze the
// trail — out of scope, a NAMED no-op: the port exists for parity, the bypass behaviour is the
// engine's, not this leaf's).
static const FeedbackOp _reg_advancedfeedback{
    {"AdvancedFeedback", "AdvancedFeedback",
     {{"Image", "Image", "Texture2D", true},            // Command/Image: the new content (current)
      {"out", "out", "Texture2D", false},
      // OUTER defaults (AdvancedFeedback.t3:9-78):
      {"Displacement", "Displacement", "Float", true, 15.0f, -100.0f, 100.0f},
      {"Shade", "Shade", "Float", true, 0.4f, -2.0f, 2.0f},
      {"BlurRadius", "BlurRadius", "Float", true, 4.0f, 0.0f, 100.0f},
      {"SampleDistance", "SampleDistance", "Float", true, 0.2f, 0.0f, 10.0f},
      {"SampleRadius", "SampleRadius", "Float", true, 1.263f, 0.0f, 10.0f},
      {"Twist", "Twist", "Float", true, -15.0f, -360.0f, 360.0f},
      {"Zoom", "Zoom", "Float", true, 1.0f, 0.01f, 8.0f},
      {"Rotate", "Rotate", "Float", true, 0.0f, -360.0f, 360.0f},
      {"Offset.x", "Offset", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
      {"DisplaceOffset", "DisplaceOffset", "Float", true, 0.0f, -10.0f, 10.0f},
      {"ShiftHue", "ShiftHue", "Float", true, 0.3f, -360.0f, 360.0f},
      {"ShiftSaturation", "ShiftSaturation", "Float", true, 0.0f, -1.0f, 1.0f},
      {"ShiftBrightness", "ShiftBrightness", "Float", true, 0.0f, -1.0f, 1.0f},
      {"LimitBrights", "LimitBrights", "Float", true, 1.0f, 0.0f, 10.0f},
      {"AmplifyEdges", "AmplifyEdges", "Float", true, 0.141f, 0.0f, 4.0f},
      {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
     /*evaluate=*/nullptr},  // Texture2D output cannot ride NodeSpec::evaluate (returns ONE float)
    "AdvancedFeedback", cookAdvancedFeedback, /*needsPair=*/true,
    (uint32_t)MTL::PixelFormatRGBA8Unorm, "advancedfeedback", runAdvancedFeedbackSelfTest};

}  // namespace sw
