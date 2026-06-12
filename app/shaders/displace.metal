// Displace: TiXL-ported image warp (lane D2). Faithful port of external/tixl
// Operators/Lib/Assets/shaders/img/fx/Displace.hlsl. A fullscreen triangle samples the input
// `Image` at a UV pushed by a direction read from the `DisplaceMap` texture; the four DisplaceModes
// (IntensityGradient/Intensity/NormalMap/SignedNormalMap) decide how the map becomes a direction.
//
// Fork (named, DX11->Metal):
//   (1) Sampler address mode: the HLSL leans on the host sampler (TiXL .t3 WrapMode "Wrap"); here we
//       bind a fixed linear+clamp-to-edge sampler in the op (cookDisplace), same fork as Blur. A
//       warp that pushes UV past the edge clamps instead of tiling; non-default Wrap is a follow-up.
//   (2) HLSL SampleLevel(s, uv, 0) -> Metal sample(s, uv, level(0)) (explicit mip 0).
//   (3) Faithful quirk preserved: inside the NormalMap branch (DisplaceMode>=2) the HLSL re-tests
//       `DisplaceMode < 0.5` which is ALWAYS false there (mode is 2 or 3) — so it always takes the
//       `rgba.rg * 0.01` arm. Kept verbatim (signed-vs-unsigned read is a TiXL no-op, not ours).
#include <metal_stdlib>
#include "displace_params.h"   // DisplaceParams, DISPLACE_Params
using namespace metal;

constant float kPi = 3.14158f;  // HLSL uses 3.14158 (sic — TiXL's literal); kept for bit-parity.

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (NDC up vs
// texture down), same as blur_vs.
vertex VSOut displace_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of Displace.hlsl DoDisplace(): read the map, derive a direction + length, push the Image UV.
static float4 doDisplace(float2 uv,
                         texture2d<float> image, texture2d<float> displaceMap, sampler samLinear,
                         constant DisplaceParams& P) {
  float mapW = (float)displaceMap.get_width();
  float mapH = (float)displaceMap.get_height();
  float2 mapOffset = float2(P.DisplaceMapOffsetX, P.DisplaceMapOffsetY);

  float sx = P.SampleRadius / mapW;
  float sy = P.SampleRadius / mapH;

  float2 d = float2(0.0f, 0.0f);
  float len = 0.0f;
  float2 direction = float2(0.0f, 0.0f);

  if (P.DisplaceMode < 1.5f) {
    if (P.DisplaceMode < 0.5f) {
      // IntensityGradient: central-difference of luminance -> gradient.
      float4 cx1 = displaceMap.sample(samLinear, float2(uv.x + sx, uv.y) + mapOffset, level(0));
      float x1 = (cx1.r + cx1.g + cx1.b) / 3.0f;
      float4 cx2 = displaceMap.sample(samLinear, float2(uv.x - sx, uv.y) + mapOffset, level(0));
      float x2 = (cx2.r + cx2.g + cx2.b) / 3.0f;
      float4 cy1 = displaceMap.sample(samLinear, float2(uv.x, uv.y + sy) + mapOffset, level(0));
      float y1 = (cy1.r + cy1.g + cy1.b) / 3.0f;
      float4 cy2 = displaceMap.sample(samLinear, float2(uv.x, uv.y - sy) + mapOffset, level(0));
      float y2 = (cy2.r + cy2.g + cy2.b) / 3.0f;
      d += float2((x1 - x2), (y1 - y2));
    } else {
      // Intensity: luminance straight down (only y), scaled.
      float4 rgba = displaceMap.sample(samLinear, uv + mapOffset, level(0));
      d = float2(0.0f, (rgba.r + rgba.g + rgba.b) / 3.0f) / 10.0f;
    }
    float a = (d.x == 0.0f && d.y == 0.0f)
                  ? 0.0f
                  : (atan2(d.x, d.y) + P.Twist / 180.0f * kPi);
    direction = float2(sin(a), cos(a));
    len = length(d) + 0.000001f;
  } else {
    // NormalMap / SignedNormalMap: read rg as a 2D vector, rotate by Twist.
    float4 rgba = displaceMap.sample(samLinear, uv + mapOffset, level(0));
    // Faithful quirk: HLSL re-tests DisplaceMode < 0.5 here (always false for mode>=2) -> rg*0.01.
    d = P.DisplaceMode < 0.5f ? (rgba.rg - 0.5f) * 0.01f
                              : rgba.rg * 0.01f;
    len = length(d) + 0.000001f;

    float rRad = P.Twist / 180.0f * kPi;
    float sina = sin(-rRad);
    float cosa = cos(-rRad);
    d = float2(cosa * d.x - sina * d.y,
               cosa * d.y + sina * d.x);
    direction = d / len;
  }

  float2 p2 = direction * (-P.DisplaceAmount * len * 10.0f + P.DisplaceOffset);
  float imgAspect = P.TargetWidth / P.TargetHeight;
  p2.x /= imgAspect;

  float4 c = image.sample(samLinear, uv + p2, level(0));
  c.rgb *= (1.0f - len * P.Shade * 100.0f);
  return c;
}

fragment float4 displace_fs(VSOut in [[stage_in]],
                            texture2d<float> image       [[texture(0)]],
                            texture2d<float> displaceMap [[texture(1)]],
                            sampler samLinear            [[sampler(0)]],
                            constant DisplaceParams& P   [[buffer(DISPLACE_Params)]]) {
  float2 uv = in.texCoord;
  float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);

  if (P.UseRGSSMultiSampling > 0.5f) {
    // 4x rotated-grid supersample (mirror of psMain).
    float4 offs0 = float4(-0.375f, 0.125f, 0.125f, 0.375f);
    float4 offs1 = float4(0.375f, -0.125f, -0.125f, -0.375f);
    float2 sxy = float2(P.TargetWidth, P.TargetHeight);
    c = (doDisplace(uv + offs0.xy / sxy, image, displaceMap, samLinear, P) +
         doDisplace(uv + offs0.zw / sxy, image, displaceMap, samLinear, P) +
         doDisplace(uv + offs1.xy / sxy, image, displaceMap, samLinear, P) +
         doDisplace(uv + offs1.zw / sxy, image, displaceMap, samLinear, P)) / 4.0f;
  } else {
    c = doDisplace(uv, image, displaceMap, samLinear, P);
  }

  return clamp(c, 0.0f, float4(999.0f, 999.0f, 999.0f, 1.0f));
}
