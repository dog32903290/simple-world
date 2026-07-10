// Steps: TiXL-ported posterize/quantize filter (gray -> Bias2 curve -> N-step quantize -> ramp/edge
// gradient LUT lookup -> optional highlight tint -> alpha composite, optional 4x supersample).
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/Steps.hlsl (verbatim below,
// line refs to that file).
//
// Kernel (verbatim ComputeColor, :39-84):
//   orgColor = ImageA.Sample(uv)                                                       // :41
//   gray = saturate((r+g+b)/3)                                                          // :43
//   biased = Bias2(gray, Bias) = gray / ((1/Bias - 2)*(1-gray) + 1)                     // :44,:35-38
//   c = biased + Offset/StepCount                                                       // :45
//   modulo = Repeat ? mod(c,1) : saturate(c)                                            // :46-47
//   index = int(modulo * StepCount)                                                     // :49
//   main = index / (StepCount - 1)                                                      // :51
//   remainder = (modulo - main) * StepCount + main                                      // :52
//   if (Repeat) {                                                                       // :55-64
//     modulo2 = mod(c - 0.01 + 1/StepCount, 1); index2 = int(modulo2*StepCount)
//     main2 = index2/(StepCount-1); r=100
//     edge = saturate(abs(gray-0.5)*2*r - r + 1)
//     offsetMod = mod(Offset-0.01, 1); newEdge = lerp(main, main2, offsetMod)
//     main = lerp(main, newEdge, edge)
//   }
//   isHighlight = index == modi(int(HighlightIndex), int(StepCount))                    // :74
//   rampColor = RampImageA.Sample(main, 0.5/2)                                          // :75 (SPLIT
//     into RampTex.sample(main,0.5) here — see .cpp header for the 2-row->2-texture fork)
//   mainRampColor = isHighlight ? lerp(rampColor, Highlight, Highlight.a) : rampColor    // :78-80
//   edgeRampColor = RampImageA.Sample(remainder, 1.5/2)                                 // :90 (SPLIT
//     into EdgeTex.sample(remainder,0.5) here)
//   a = clamp(mainRampColor.a + edgeRampColor.a - mainRampColor.a*edgeRampColor.a, 0,1)  // :92
//   rgb = (1-edgeRampColor.a)*mainRampColor.rgb + edgeRampColor.a*edgeRampColor.rgb      // :93
//
// psMain (:97-116): UseSuperSampling -> average ComputeColor at 4 offset taps (+-0.375/+-0.125 px in
// a 2x2 rotated-grid pattern) / else -> ComputeColor(p) once.
//
// Forks (named, DX11 pixel-shader -> Metal fullscreen-triangle VS+FS; same fork class as
// ColorGradeDepth/Displace):
//   - DX11 psMain (VS+PS fed by fullscreen-quad Draw) -> Metal fullscreen-triangle VS+FS.
//   - RampImageA (a SINGLE 2-row texture: row0=Ramp gradient LUT, row1=Edge gradient LUT — baked by
//     Steps.t3's embedded GradientsToTexture child, both t3 Ramp/Edge inputs wired into its ONE
//     "Gradients" MultiInput slot) is SPLIT into TWO separate 1-row textures (RampTex/EdgeTex) here,
//     each sampled at v=0.5 (exact row-center of a 1-tall texture) instead of v=0.25/v=0.75 of a
//     2-tall texture. Byte-identical: ClampToEdge + sampling at the exact texel-row center never
//     blends across rows, so this is a shape fork, not a value fork (mirrors gradient_raster.h's
//     shared 1-row-LUT convention every other gradient-consuming op in this engine already uses).
//   - SmoothRadius (Steps.hlsl cbuffer :9) is dead code in the HLSL itself — dropped (see .h header).
//   - Single TiXL sampler (SamplerState Clamp/Clamp/Clamp, Steps.t3) -> one Linear+ClampToEdge sampler
//     for all three texture reads (same convention as ColorGradeDepth).
#include <metal_stdlib>
#include "steps_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

vertex VSOut steps_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

inline float steps_mod(float x, float y) { return x - y * floor(x / y); }         // :30-33
inline int steps_modi(int x, int y) {                                              // :35-38 (Steps.cs's own modi)
  return x >= 0 ? (x % y) : (y - ((-x) % (-y)));
}
inline float steps_bias2(float x, float bias) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }  // :35-38

inline float4 stepsComputeColor(float2 uv, texture2d<float> ImageA, texture2d<float> RampTex,
                                texture2d<float> EdgeTex, sampler texSampler,
                                constant StepsParams& P) {
  float4 orgColor = ImageA.sample(texSampler, uv);                          // :41
  float gray = saturate((orgColor.r + orgColor.g + orgColor.b) / 3.0f);     // :43
  float biased = steps_bias2(gray, P.Bias);                                 // :44
  float c = biased + P.Offset / P.StepCount;                                // :45
  float modulo = (P.Repeat > 0.5f) ? steps_mod(c, 1.0f) : saturate(c);      // :46-47

  int index = (int)(modulo * P.StepCount);                                  // :49
  float main = (float)index / (P.StepCount - 1.0f);                        // :51
  float remainder = (modulo - main) * P.StepCount + main;                   // :52

  if (P.Repeat > 0.5f) {                                                    // :55-64
    float modulo2 = steps_mod(c - 0.01f + 1.0f / P.StepCount, 1.0f);
    int index2 = (int)(modulo2 * P.StepCount);
    float main2 = (float)index2 / (P.StepCount - 1.0f);
    float rr = 100.0f;
    float edge = saturate(fabs(gray - 0.5f) * 2.0f * rr - rr + 1.0f);
    float offsetMod = steps_mod(P.Offset - 0.01f, 1.0f);
    float newEdge = mix(main, main2, offsetMod);
    main = mix(main, newEdge, edge);
  }

  bool isHighlight = index == steps_modi((int)P.HighlightIndex, (int)P.StepCount);  // :74
  float4 rampColor = RampTex.sample(texSampler, float2(main, 0.5f));                // :75 (SPLIT)
  float4 highlight = float4(P.HighlightX, P.HighlightY, P.HighlightZ, P.HighlightW);
  float4 mainRampColor = isHighlight ? mix(rampColor, highlight, highlight.a) : rampColor;  // :78-80

  float4 edgeRampColor = EdgeTex.sample(texSampler, float2(remainder, 0.5f));        // :90 (SPLIT)

  float a = clamp(mainRampColor.a + edgeRampColor.a - mainRampColor.a * edgeRampColor.a, 0.0f, 1.0f);  // :92
  float3 rgb = (1.0f - edgeRampColor.a) * mainRampColor.rgb + edgeRampColor.a * edgeRampColor.rgb;      // :93
  return float4(rgb, a);
}

fragment float4 steps_fs(VSOut in [[stage_in]],
                         texture2d<float> ImageA  [[texture(0)]],
                         texture2d<float> RampTex [[texture(1)]],
                         texture2d<float> EdgeTex [[texture(2)]],
                         sampler texSampler       [[sampler(0)]],
                         constant StepsParams& P  [[buffer(STEPS_Params)]]) {
  float2 p = in.texCoord;

  if (P.UseSuperSampling > 0.5f) {                                           // :101-111
    float2 sxy = float2(P.TargetWidth, P.TargetHeight);
    float4 sum =
        stepsComputeColor(p + float2(-0.375f,  0.125f) / sxy, ImageA, RampTex, EdgeTex, texSampler, P) +
        stepsComputeColor(p + float2( 0.125f,  0.375f) / sxy, ImageA, RampTex, EdgeTex, texSampler, P) +
        stepsComputeColor(p + float2( 0.375f, -0.125f) / sxy, ImageA, RampTex, EdgeTex, texSampler, P) +
        stepsComputeColor(p + float2(-0.125f, -0.375f) / sxy, ImageA, RampTex, EdgeTex, texSampler, P);
    return sum / 4.0f;
  }
  return stepsComputeColor(p, ImageA, RampTex, EdgeTex, texSampler, P);      // :114
}
