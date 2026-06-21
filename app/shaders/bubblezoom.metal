// BubbleZoom: TiXL-ported magnifying-bubble distortion image FX, single pass.
// VERBATIM port of external/tixl Operators/Lib/Assets/shaders/img/fx/BubbleZoom.hlsl psMain.
// Computes a radial distance from Center, shapes it through a feathered radius into a [0,1] value c,
// maps c through ApplyGainAndBias (+ FlipEffect lerp), samples a rasterized gradient ROW (bound at t1)
// at (dBiased, 0), magnifies the image lookup UV by ScaleFactor toward Center, and lerps the original
// (zoomed) image color toward the gradient color by the gradient's alpha.
//
// ============================== HLSL→MSL NOTES (named forks) ==============================
// [fork-gain-bias-inline]  ApplyGainAndBias + GetBias + GetSchlickBias inlined verbatim from
//   shared/bias-functions.hlsl (scalar overload only — psMain calls the scalar form). Same inline
//   pattern as radialgradient.metal / lineargradient.metal pulling shared helpers in.
// [fork-grad-sampler-clamp]  The gradient row is sampled at v=0 with the clampedSampler (ClampToEdge);
//   ImageA uses texSampler (Clamp — BubbleZoom.t3 _multiImageFxSetupStatic WrapMode=Clamp). The 1-row
//   gradient REQUIRES a clamp sampler so the v edge isn't corrupted (mirrors radialgradient.metal).
// [fork-no-blendcolors]  BubbleZoom does NOT use BlendColors / PingPongRepeat / hash12 (those are
//   RadialGradient/LinearGradient helpers). The final composite is a plain
//   lerp(orgColor.rgb, gradient.rgb, gradient.a) — ported verbatim, NOT routed through BlendColors.
// [fork-dead-fmod]  BubbleZoom.hlsl declares a local `float fmod` (floor-based) but psMain never calls
//   it (the distance math uses no modulo). Dead in psMain → not ported.
#include <metal_stdlib>
#include "bubblezoom_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as radialgradient_vs / lineargradient_vs.
vertex VSOut bubblezoom_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// --- ApplyGainAndBias (scalar) — verbatim from shared/bias-functions.hlsl ---
static inline float GetBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
static inline float GetSchlickBias(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    x = 0.5f * GetBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    x = 0.5f * GetBias(1.0f - g, x) + 0.5f;
  }
  return x;
}
static inline float ApplyGainAndBias(float value, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = GetBias(b, value);
    value = GetSchlickBias(g, value);
  } else {
    value = GetSchlickBias(g, value);
    value = GetBias(b, value);
  }
  return value;
}

// Mirror of BubbleZoom.hlsl psMain (lines 35-64), line for line.
fragment float4 bubblezoom_fs(VSOut input                            [[stage_in]],
                              texture2d<float> ImageA                [[texture(0)]],
                              texture2d<float> Gradient              [[texture(1)]],
                              sampler texSampler                     [[sampler(0)]],
                              sampler clampedSampler                 [[sampler(1)]],
                              constant BubbleZoomParams& P           [[buffer(BUBBLEZOOM_Params)]],
                              constant BubbleZoomResolution& Res     [[buffer(BUBBLEZOOM_Resolution)]]) {
  float2 uv = input.texCoord;                                        // :37

  float aspectRatio = Res.TargetWidth / Res.TargetHeight;           // :39
  float2 p = uv;                                                     // :40
  p -= 0.5f;                                                         // :41
  p.x *= aspectRatio;                                               // :42

  float2 Center = float2(P.CenterX, P.CenterY);

  float c = distance(p, Center * float2(1.0f, -1.0f)) * 2.0f;       // :44

  float adjustedRadius = 2.0f * P.Radius * aspectRatio;            // :46

  c += -adjustedRadius + 2.0f * abs(P.Feather) / aspectRatio;       // :48

  c = saturate(c / P.Feather);                                      // :50

  float dBiased = ApplyGainAndBias(c, float2(P.GainAndBiasX, P.GainAndBiasY));  // :52

  dBiased = mix(dBiased, 1.0f - dBiased, P.FlipEffect);            // :54  (HLSL lerp)

  float4 gradient = Gradient.sample(clampedSampler, float2(dBiased, 0.0f));     // :56

  float2 zoomedUV =                                                  // :58
      ((uv + Center * float2(P.ScaleFactor * 0.5f, -P.ScaleFactor * 0.5f)) - 0.5f) / P.ScaleFactor + 0.5f;

  float2 lookupUv = mix(zoomedUV, uv, dBiased);                     // :60  (HLSL lerp)

  float4 orgColor = ImageA.sample(texSampler, lookupUv);            // :62
  return float4(mix(orgColor.rgb, gradient.rgb, gradient.a), orgColor.a);       // :63 (HLSL lerp)
}
