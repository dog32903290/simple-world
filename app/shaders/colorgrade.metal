// ColorGrade: TiXL-ported lift/gamma/gain + vignette + pre-saturation image filter, single pass.
// Faithful line-by-line port of external/tixl Operators/Lib/Assets/shaders/img/ColorGrade.hlsl
// (self-contained — no shared include).
//
// ============================== LOAD-BEARING PARITY NOTES (named) ==============================
// [fork-feather-is-bias]  The .cs input "VignetteFeather" feeds the shader cbuffer field
//   "VignetteBias" (see colorgrade_params.h). Host field is VignetteBias; the math below is the
//   .hlsl verbatim.
// [fork-clampresult]      ColorGrade.hlsl ALWAYS clamps c.rgb to [0.000001,1000] (twice) and c.a to
//   [0,1] regardless of the ClampResult bool. The .cs ClampResult does NOT enter this shader — it
//   wires (via BoolToFloat) into the _ImageFxShaderSetupStatic framework node, which selects the
//   OUTPUT TEXTURE FORMAT (a clamping UNorm vs an unbounded float format), not per-pixel math. So
//   the per-pixel clamps here are unconditional, matching the shader bit-for-bit. ClampResult is a
//   NO-OP fork at the leaf seam (no output-format selection) — listed in NodeSpec per .cs order.
// [fork-sampler]          Fixed linear+repeat sampler = ColorGrade.t3's _ImageFxShaderSetupStatic
//   defaults (Filter=MinMagMipLinear, Wrap=Wrap). Set in the cook, not here.
#include <metal_stdlib>
#include "colorgrade_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
// Same convention as convertcolors_vs / transformimage_vs across the image filters.
vertex VSOut colorgrade_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of ColorGrade.hlsl psMain (lines 25-56), line for line.
fragment float4 colorgrade_fs(VSOut psInput [[stage_in]],
                              texture2d<float> inputTexture     [[texture(0)]],
                              sampler texSampler                [[sampler(0)]],
                              constant ColorGradeParams& P      [[buffer(CG_Params)]]) {
  float4 Gain          = float4(P.GainR, P.GainG, P.GainB, P.GainA);
  float4 Gamma         = float4(P.GammaR, P.GammaG, P.GammaB, P.GammaA);
  float4 Lift          = float4(P.LiftR, P.LiftG, P.LiftB, P.LiftA);
  float4 VignetteColor = float4(P.VigColorR, P.VigColorG, P.VigColorB, P.VigColorA);
  float2 VignetteCenter = float2(P.VigCenterX, P.VigCenterY);
  float VignetteRadius = P.VignetteRadius;
  float VignetteBias   = P.VignetteBias;
  float PreSaturate    = P.PreSaturate;

  float2 uv = psInput.texCoord;
  float4 c = inputTexture.sample(texSampler, uv);
  c.rgb = clamp(c.rgb, 0.000001f, 1000.0f);

  // Saturation
  float gray = (c.r * 0.22f + c.g * 0.707f + c.b * 0.071f);
  c.rgb = mix(float3(gray, gray, gray), c.rgb, PreSaturate);  // HLSL lerp(a,b,t)=mix(a,b,t)

  // Vignette
  float flipEdge = VignetteRadius < 0.0f ? -1.0f : 1.0f;

  float v = length((uv - 0.5f - VignetteCenter * float2(1.0f, -1.0f)));
  v /= VignetteRadius * flipEdge / 2.0f;
  v -= 0.5f;
  v = smoothstep(0.0f, 1.0f, (v - 0.5f) / (VignetteBias * flipEdge * 2.0f) + 0.5f);

  // Grade
  float3 liftScaled  = Lift.rgb  * 2.0f * Lift.a  + (0.5f - Lift.a);
  float3 gammaScaled = Gamma.rgb * 2.0f * Gamma.a + (0.5f - Gamma.a);
  float3 gainScaled  = Gain.rgb  * 2.0f * Gain.a  + (0.5f - Gain.a);
  gainScaled += (VignetteColor.rgb - 0.5f) * v * (VignetteColor.a * 2.0f + 1.0f);

  c.rgb = pow((c.rgb + (liftScaled.rgb * 2.0f - 1.0f) * (1.0f - c.rgb))  // Lift
                  * gainScaled * 2.0f,                                    // Gain
              1.0f / (gammaScaled * 2.0f));

  c.rgb = clamp(c.rgb, 0.000001f, 1000.0f);  // [fork-clampresult] unconditional, matches .hlsl
  c.a = clamp(c.a, 0.0f, 1.0f);
  return c;
}
