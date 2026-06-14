// ChromaticDistortion: TiXL-ported radial bulge + chromatic radial-blur filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/ChromaticDistortion.hlsl psMain.
//
// Kernel (verbatim, HLSL lines 66-104):
//   clampedSampleCount = clamp(((int)SampleCount/2)*2, 1, 100)
//   p = texCoord
//   fromCenter = (0.5 + Center - p) / ScaleImage
//   distance = length(fromCenter)
//   bulge = 1 + Distort*0.5
//   fromCenter *= pow(distance * DistortOffset, Distort) * bulge
//   dir = Size * fromCenter * pow(length(fromCenter), 0.3)
//   p = 0.5 - fromCenter + Center
//   step = 2 / (clampedSampleCount + 0.5)
//   for (f = -1.0; f <= 1; f += step):
//       col = ImageA.SampleLevel(p + dir*f, 0)
//       blurredSum += col
//       chromarizedSum += col.rgb * chromaShift(f)
//   chromarizedSum /= clampedSampleCount; blurredSum /= clampedSampleCount
//   return float4(lerp(blurredSum.rgb, chromarizedSum.rgb, Colorize), blurredSum.a)
//
// chromaShift(range) (HLSL lines 36-42): per-channel triangular RGB weighting centred at
// range = -1 (R), 0 (G), +1 (B).
//
// Forks (named, DX11->Metal):
//   - DX11 PS (VS+PS pipeline) -> Metal fullscreen-triangle VS+FS (same fork class as
//     ChromaticAbberation/Tint/ChannelMixer).
//   - HLSL SampleLevel(uv,0) -> Metal sample(uv, level(0)) (explicit mip-0 fetch).
//   - b1 TimeConstants cbuffer is framework-injected and UNUSED by the kernel; omitted.
//   - Fixed linear+clamp sampler (TiXL host wrap knob not exposed; matches the SampleLevel reach
//     beyond edges with clamp, same fork class as Sharpen).
//   - HLSL `(int)SampleCount` cast preserved; chromeShiftSine/chromaShiftLinear are dead helpers
//     in the HLSL (commented out in the loop) — only chromaShift is active, so only it is ported.
#include <metal_stdlib>
#include "chromaticdistortion_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut chromaticdistortion_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of ChromaticDistortion.hlsl chromaShift (lines 36-42).
static float3 chromaShift(float range) {
  return float3(
      clamp(1.5f - abs(range + 1.0f), 0.0f, 1.0f) * 2.0f,
      clamp(1.5f - abs(range),        0.0f, 1.0f) * 1.5f,
      clamp(1.5f - abs(range - 1.0f), 0.0f, 1.0f) * 2.0f);
}

// Mirror of ChromaticDistortion.hlsl psMain.
fragment float4 chromaticdistortion_fs(VSOut in [[stage_in]],
                                       texture2d<float> ImageA  [[texture(0)]],
                                       sampler texSampler       [[sampler(0)]],
                                       constant ChromaticDistortionParams& P [[buffer(CHROMADIST_Params)]]) {
  float clampedSampleCount = clamp((float)(((int)P.SampleCount / 2) * 2), 1.0f, 100.0f);

  float2 Center = float2(P.CenterX, P.CenterY);
  float2 p = in.texCoord;
  float2 fromCenter = (0.5f + Center - p) / P.ScaleImage;

  float dist = length(fromCenter);
  float bulge = 1.0f + P.Distort * 0.5f;

  fromCenter *= pow(dist * P.DistortOffset, P.Distort) * bulge;

  float2 dir = P.Size * fromCenter * pow(length(fromCenter), 0.3f);
  p = 0.5f - fromCenter + Center;

  float4 blurredSum = float4(0.0f);
  float3 chromarizedSum = float3(0.0f);

  float step = 2.0f / (clampedSampleCount + 0.5f);

  for (float f = -1.0f; f <= 1.0f; f += step) {
    float4 col = ImageA.sample(texSampler, p + dir * f, level(0));
    blurredSum += col;
    chromarizedSum += col.rgb * chromaShift(f);
  }

  chromarizedSum /= clampedSampleCount;
  blurredSum /= clampedSampleCount;
  return float4(mix(blurredSum.rgb, chromarizedSum.rgb, P.Colorize), blurredSum.a);
}
