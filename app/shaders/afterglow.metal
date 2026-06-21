// AfterGlow composite: the CROSS-FRAME feedback fold of the TiXL AfterGlow fx, collapsed to one
// fullscreen pass. Authority: external/tixl Operators/Lib/image/fx/feedback/AfterGlow.t3 spine
// (RenderTarget(NoClear) trail + DrawQuad black-over decay + Layer2d additive tinted glow). The
// in-frame blur is done by the existing Blur shader (blur_vs/blur_fs); this pass only does the
// final fold into the trail buffer:
//
//   out.rgb = prev.rgb * Decay  +  blurred.rgb * Color.rgb
//   out.a   = 1.0  (opaque trail; the engine frame format is RGBA8Unorm, alpha unused downstream)
//
// where Decay = (1 - DecayRate) (.t3 DrawQuad black-over, normal blend) and Color is the outer
// Color pin (~0.59 grey) tinting the freshly-added glow (.t3 Layer2d BlendMode=1 add). See
// afterglow_params.h for the full backward-trace + the NAMED verdict divergence from the work-order.
#include <metal_stdlib>
#include "afterglow_params.h"   // AfterGlowParams, AFTERGLOW_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer), same idiom as blur_vs. texCoord 0..1,
// Y-flipped (NDC up vs texture down).
vertex VSOut afterglow_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Composite: decay the previous trail, add the (already glow-scaled) blurred current tinted by Color.
fragment float4 afterglow_composite_fs(VSOut in [[stage_in]],
                                       texture2d<float> prevTex   [[texture(0)]],
                                       texture2d<float> blurTex   [[texture(1)]],
                                       sampler samLinear          [[sampler(0)]],
                                       constant AfterGlowParams& P [[buffer(AFTERGLOW_Params)]]) {
  float4 prev = prevTex.sample(samLinear, in.texCoord);
  float4 cur = blurTex.sample(samLinear, in.texCoord);
  float3 tint = float3(P.GlowR, P.GlowG, P.GlowB);
  float3 outRgb = prev.rgb * P.Decay + cur.rgb * tint;
  return float4(outRgb, 1.0f);
}
