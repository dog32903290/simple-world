// AfterGlow2 composite: the CROSS-FRAME feedback fold of the TiXL AfterGlow2 fx, collapsed to TWO
// fullscreen passes. Authority: external/tixl Operators/Lib/image/fx/feedback/AfterGlow2.t3 spine.
// AfterGlow2 is a TWO-COLOR sibling of AfterGlow: the persistent TRAIL fold is IDENTICAL (decay*prev
// + Color-tinted blurred current), but the OUTPUT adds a FINAL Screen-blend of the ORIGINAL current
// image tinted by a SECOND color (OrgColor) — a NON-persistent pass that does NOT feed back.
//
// Why two passes (the load-bearing structural point): the TiXL trail RenderTarget (.t3 3f277e17)
// holds ONLY the decayed+glow accumulation; the OrgColor screen-blend (.t3 Blend 5e367fc1) is a
// separate FINAL node whose result is the Output but NEVER re-enters the trail. So:
//   PASS 1 (afterglow2_trail_fs)  -> writes the TRAIL into the cross-frame pair half `writeTo`:
//        trail = prev.rgb * Decay + blurred.rgb * Color
//   PASS 2 (afterglow2_compose_fs) -> reads that trail + the original image, writes the OUTPUT scratch:
//        out.rgb = trail.rgb + (orig.rgb * OrgColor) * orig.a   (Blend.hlsl ColorMode 1 = screen)
// If pass 2 folded OrgColor back into the pair, the second color would compound every frame —
// diverging from TiXL (where the Blend is terminal). See afterglow2_params.h for the .t3 cites.
#include <metal_stdlib>
#include "afterglow2_params.h"   // AfterGlow2Params, AFTERGLOW2_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer), same idiom as blur_vs / afterglow_vs.
vertex VSOut afterglow2_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// PASS 1 — the cross-frame TRAIL fold (the persistent accumulator written into the pair half).
//   trail = prev * Decay + blurred * Color   (.t3 Layer2d decay BlendMode 0 + DrawScreenQuad add).
fragment float4 afterglow2_trail_fs(VSOut in [[stage_in]],
                                    texture2d<float> prevTex   [[texture(0)]],
                                    texture2d<float> blurTex   [[texture(1)]],
                                    sampler samLinear          [[sampler(0)]],
                                    constant AfterGlow2Params& P [[buffer(AFTERGLOW2_Params)]]) {
  float4 prev = prevTex.sample(samLinear, in.texCoord);
  float4 cur  = blurTex.sample(samLinear, in.texCoord);
  float3 glowTint = float3(P.GlowR, P.GlowG, P.GlowB);
  float3 trail = prev.rgb * P.Decay + cur.rgb * glowTint;
  return float4(trail, 1.0f);
}

// PASS 2 — the terminal two-color OUTPUT (the AfterGlow2 delta; does NOT feed back).
//   out.rgb = trail + (orig*OrgColor.rgb) * (clamp(orig.a)*clamp(OrgColor.a))
//   (Blend.hlsl ColorMode 1 screen, line 117 rgb = tA.rgb + tB.rgb*tB.a; tB = ImageB*ImageBColor with
//    tB.a = clamp(ImageB.a,0,1)*clamp(ImageBColor.a,0,1), Blend.hlsl:52,54 — ImageBColor = OrgColor,
//    ColorA = white default. So OrgColor.w scales the whole terminal contribution.)
fragment float4 afterglow2_compose_fs(VSOut in [[stage_in]],
                                      texture2d<float> trailTex  [[texture(0)]],
                                      texture2d<float> origTex   [[texture(1)]],
                                      sampler samLinear          [[sampler(0)]],
                                      constant AfterGlow2Params& P [[buffer(AFTERGLOW2_Params)]]) {
  float4 trail = trailTex.sample(samLinear, in.texCoord);
  float4 orig  = origTex.sample(samLinear, in.texCoord);
  float3 orgTint = float3(P.OrgR, P.OrgG, P.OrgB);
  float tBa = clamp(orig.a, 0.0f, 1.0f) * clamp(P.OrgA, 0.0f, 1.0f);  // Blend.hlsl:54 tB.a
  float3 outRgb = trail.rgb + (orig.rgb * orgTint) * tBa;
  return float4(outRgb, 1.0f);
}
