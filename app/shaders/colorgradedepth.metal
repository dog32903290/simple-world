// ColorGradeDepth: TiXL-ported depth-aware color grader (Lift/Gamma/Gain + depth-driven Gradient tint
// + radial vignette). Faithful port of external/tixl Operators/Lib/Assets/shaders/img/adjust/
// ColorGradeWithDepth.hlsl psMain (verbatim below, line refs to that file).
//
// Kernel (verbatim):
//   c = Input.Sample(uv); c.rgb = clamp(c.rgb, 1e-6, 1000)                        // :40-41
//   depth = DepthBuffer.SampleLevel(uv,0).r                                        // :43
//   z = DepthToSceneZ(depth) = (2*Near)/(Far+Near-depth*(Far-Near))*(Far-Near)+Near // :29-34
//   normalizedZ = saturate((z - GradientDepthRange.x) / (GradientDepthRange.y - GradientDepthRange.x)) // :45
//   gradientColor = Gradient.SampleLevel((normalizedZ,0.5), 0)                     // :49
//   gray = dot(c.rgb, (0.22,0.707,0.071)); c.rgb = lerp(gray.xxx, c.rgb, PreSaturate) // :53-54
//   v = length(uv-0.5-VignetteCenter*(1,-1)); v /= VignetteRadius*flipEdge/2; v -= 0.5;
//   v = smoothstep(0,1,(v-0.5)/(VignetteBias*flipEdge*2)+0.5)                      // :57-62
//   liftScaled  = Lift.rgb*2*Lift.a   + (0.5-Lift.a)                               // :65
//   gammaScaled = Gamma.rgb*2*Gamma.a + (0.5-Gamma.a)                              // :66
//   gainScaled  = Gain.rgb*2*Gain.a   + (0.5-Gain.a)                               // :67
//   gainScaled += (VignetteColor.rgb-0.5)*v*(VignetteColor.a*2+1)
//              + (gradientColor.rgb-0.5)*(gradientColor.a*2+1)                     // :68-69
//   c.rgb = pow((c.rgb + (liftScaled*2-1)*(1-c.rgb)) * gainScaled*2, 1/(gammaScaled*2))  // :72-74
//   c.rgb = clamp(c.rgb, 1e-6, 1000); c.a = saturate(c.a)                          // :76-77
//
// Forks (named, DX11 pixel-shader -> Metal fullscreen-triangle VS+FS; same fork class as
// DepthBufferAsGrayScale/Displace):
//   - DX11 psMain (VS+PS pair fed by a fullscreen-quad Draw) -> Metal fullscreen-triangle VS+FS
//     (identical pixel coverage, one fewer vertex).
//   - Gradient (T3.Core.DataTypes.Gradient) -> baked at cook time into a 1xN RGBA32Float row texture
//     via the shared rasterizeGradientRow/gradient_raster.h seam (same seam LinearGradient/
//     GradientsToTexture use — no drift), bound at texture(2).
//   - Single TiXL sampler (register s0, no explicit Filter InputValue in ColorGradeDepth.t3 -> TiXL
//     SamplerState node default, source unavailable in this vendor tree) applied to ALL THREE reads
//     (Input/DepthBuffer/Gradient) -> we bind ONE Linear+ClampToEdge sampler for all three, matching
//     the established convention for the gradient-row read (gradient_raster.h clampedSampler) and
//     for full-screen image filters generally (Displace/RgbTV's texSampler). [named fork: assumed
//     filter mode, inert for the golden — every golden texture here is UNIFORM so point vs linear
//     sampling of it is byte-identical either way.]
#include <metal_stdlib>
#include "colorgradedepth_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut colorgradedepth_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// DepthToSceneZ (ColorGradeWithDepth.hlsl :29-34), verbatim.
inline float depthToSceneZ(float depth, float nearClip, float farClip) {
  float n = nearClip;
  float f = farClip;
  return (2.0f * n) / (f + n - depth * (f - n)) * (f - n) + n;
}

fragment float4 colorgradedepth_fs(VSOut in [[stage_in]],
                                   texture2d<float> InputTexture [[texture(0)]],
                                   texture2d<float> DepthBuffer  [[texture(1)]],
                                   texture2d<float> Gradient     [[texture(2)]],
                                   sampler texSampler            [[sampler(0)]],
                                   constant ColorGradeDepthParams& P
                                       [[buffer(COLORGRADEDEPTH_Params)]]) {
  float2 uv = in.texCoord;
  float4 c = InputTexture.sample(texSampler, uv);          // :40
  c.rgb = clamp(c.rgb, 0.000001f, 1000.0f);                 // :41

  float depth = DepthBuffer.sample(texSampler, uv, level(0)).r;  // :43
  float z = depthToSceneZ(depth, P.NearClip, P.FarClip);         // :44
  float normalizedZ = saturate((z - P.GradientDepthRangeX) /
                               (P.GradientDepthRangeY - P.GradientDepthRangeX));  // :45

  float4 gradientColor = Gradient.sample(texSampler, float2(normalizedZ, 0.5f), level(0));  // :49

  // Saturation (:53-54).
  float gray = c.r * 0.22f + c.g * 0.707f + c.b * 0.071f;
  c.rgb = mix(float3(gray, gray, gray), c.rgb, P.PreSaturate);

  // Vignette (:57-62).
  float flipEdge = P.VignetteRadius < 0.0f ? -1.0f : 1.0f;
  float2 vc = float2(P.VignetteCenterX, P.VignetteCenterY);
  float v = length(uv - 0.5f - vc * float2(1.0f, -1.0f));
  v /= P.VignetteRadius * flipEdge / 2.0f;
  v -= 0.5f;
  v = smoothstep(0.0f, 1.0f, (v - 0.5f) / (P.VignetteBias * flipEdge * 2.0f) + 0.5f);

  // Grade (:65-74).
  float3 liftScaled  = float3(P.LiftX, P.LiftY, P.LiftZ)  * 2.0f * P.LiftW  + (0.5f - P.LiftW);
  float3 gammaScaled = float3(P.GammaX, P.GammaY, P.GammaZ) * 2.0f * P.GammaW + (0.5f - P.GammaW);
  float3 gainScaled  = float3(P.GainX, P.GainY, P.GainZ)  * 2.0f * P.GainW  + (0.5f - P.GainW);
  float3 vignetteColorRgb = float3(P.VignetteColorX, P.VignetteColorY, P.VignetteColorZ);
  gainScaled += (vignetteColorRgb - 0.5f) * v * (P.VignetteColorW * 2.0f + 1.0f)
              + (gradientColor.rgb - 0.5f) * (gradientColor.a * 2.0f + 1.0f);

  c.rgb = pow((c.rgb + (liftScaled * 2.0f - 1.0f) * (1.0f - c.rgb)) * gainScaled * 2.0f,
             1.0f / (gammaScaled * 2.0f));

  c.rgb = clamp(c.rgb, 0.000001f, 1000.0f);   // :76
  c.a = saturate(c.a);                         // :77
  return c;
}
