// NormalMap: TiXL-ported finite-difference gradient -> normal encoder, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/NormalMap.hlsl psMain.
//
// Kernel (verbatim, NormalMap.hlsl:46-93):
//   sx = SampleRadius/width; sy = SampleRadius/height
//   sample ±sx (cx1/cx2) and ±sy (cy1/cy2) of the input image
//   grayX1.. = (r+g+b)/3 of each neighbour
//   d = Mode > Red_ToRG_KeepBA(3.5) ? (grayX1-grayX2, grayY1-grayY2)
//                                    : (cx1.r-cx2.r, cy1.r-cy2.r)
//   angle = (d==0) ? 0 : atan2(d.x,d.y) + Twist/180*PI
//   len = length(d); direction = (sin(angle), cos(angle))
//   Mode < 0.5  : normal = normalize((len*direction*Impact, 1)); normal.y=-normal.y;
//                 return (normal/2 + 0.5, 1)                 // tangent-space RGB (flipped Y)
//   Mode < 1.5  : normal = normalize((len*direction*Impact, 1)); return (normal, 1)
//   Mode < 2.5  : return (mod(-angle, 2*PI), len*Impact, 0, 1) // angle+magnitude
//   else        : return (len*direction*Impact + 0.5, uvImage.ba)
//
// Forks (named, DX11->Metal):
//   - DX11 PS -> Metal fullscreen-triangle VS+FS (same fork class as DetectEdges/Tint).
//   - NormalMap.hlsl reads its input via `DisplaceMap : register(t0)`; we bind the op's single
//     input texture there (texture(0)) — name kept as the kernel's variable for fidelity.
//   - HLSL GetDimensions(width,height) -> Metal Image.get_width()/get_height() (the b2 Resolution
//     cbuffer's TargetWidth/Height; texture-own size, no Resolution port — same as DetectEdges).
//   - b1 TimeConstants unused by psMain -> not bound. Fixed linear+clamp sampler.
//   - HLSL 3.141592 literal kept verbatim (NormalMap.hlsl:71,90).
#include <metal_stdlib>
#include "normalmap_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer).
vertex VSOut normalmap_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);  // flip Y: NDC up vs texture down
  return o;
}

// NormalMap.hlsl mode thresholds (lines 34-37).
constant float Gray_ToRGB                = 0.5f;
constant float Gray_ToRGBNeg             = 1.5f;
constant float Gray_ToAngleAndMagnitude  = 2.5f;
constant float Red_ToRG_KeepBA           = 3.5f;

// Mirror of NormalMap.hlsl psMain.
fragment float4 normalmap_fs(VSOut in [[stage_in]],
                             texture2d<float> Image      [[texture(0)]],
                             sampler texSampler          [[sampler(0)]],
                             constant NormalMapParams& P  [[buffer(NORMALMAP_Params)]]) {
  float width  = (float)Image.get_width();
  float height = (float)Image.get_height();

  float2 uv = in.texCoord;

  float sx = P.SampleRadius / width;
  float sy = P.SampleRadius / height;

  float4 cx1 = Image.sample(texSampler, float2(uv.x + sx, uv.y));
  float4 cx2 = Image.sample(texSampler, float2(uv.x - sx, uv.y));
  float4 cy1 = Image.sample(texSampler, float2(uv.x, uv.y + sy));
  float4 cy2 = Image.sample(texSampler, float2(uv.x, uv.y - sy));

  float grayX1 = (cx1.r + cx1.g + cx1.b) / 3.0f;
  float grayX2 = (cx2.r + cx2.g + cx2.b) / 3.0f;
  float grayY1 = (cy1.r + cy1.g + cy1.b) / 3.0f;
  float grayY2 = (cy2.r + cy2.g + cy2.b) / 3.0f;

  float2 d = P.Mode > Red_ToRG_KeepBA
                 ? float2((grayX1 - grayX2), (grayY1 - grayY2))
                 : float2((cx1.r - cx2.r), (cy1.r - cy2.r));

  float4 uvImage = Image.sample(texSampler, uv);
  float angle = (d.x == 0.0f && d.y == 0.0f) ? 0.0f
                                             : atan2(d.x, d.y) + P.Twist / 180.0f * 3.141592f;
  float len = length(d);
  float2 direction = float2(sin(angle), cos(angle));

  if (P.Mode < Gray_ToRGB) {
    float3 normal = normalize(float3(len * direction * P.Impact, 1.0f));
    normal.y = -normal.y;  // Flip Y channel
    return float4(normal / 2.0f + 0.5f, 1.0f);
  }

  if (P.Mode < Gray_ToRGBNeg) {
    float3 normal = normalize(float3(len * direction * P.Impact, 1.0f));
    return float4(normal, 1.0f);
  }

  if (P.Mode < Gray_ToAngleAndMagnitude) {
    float negAngle = -angle;
    float twoPi = 2.0f * 3.141592f;
    // HLSL #define mod(x,y) ((x)-(y)*floor((x)/(y)))
    float m = negAngle - twoPi * floor(negAngle / twoPi);
    return float4(m, len * P.Impact, 0.0f, 1.0f);
  }

  return float4(float2(len * direction * P.Impact) + 0.5f, uvImage.b, uvImage.a);
}
