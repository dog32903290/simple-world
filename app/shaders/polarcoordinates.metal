// PolarCoordinates: TiXL-ported bidirectional cartesian<->polar image remap, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/PolarCoordinates.hlsl.
// Mode < 0.5 = Cartesian2Polar: wraps the rectangular image into a polar disc (angle->x, radius->y).
// Mode >= 0.5 = Polar2Cartesian: the inverse — unrolls the polar disc back to a rectangle.
//
// Forks (named, DX11->Metal):
//  - fork[flat-cbuffer]: cbuffer fields read as flat scalars (P.Center[0/1], P.Stretch[0/1]) so the
//    Metal struct byte layout matches the HLSL register(b0) exactly (see polarcoordinates_params.h).
//  - fork[pi-verbatim]: TiXL hardcodes 3.141578 (NOT the true pi 3.14159265). We clone that exact
//    constant verbatim — using true pi would shift the angle->uv scale and break parity. The angle
//    quadrant uses atan2(p.x, p.y) (x first) exactly as TiXL, NOT the usual atan2(y,x).
//  - fork[clamp-sampler]: TiXL's PolarCoordinates uses _ImageFxShaderSetupStatic with no explicit
//    Wrap input -> host default. We use the codebase's fixed linear+clamp sampler (same fork class
//    as Blur/Tint/ChromaB). The commented `//polar = mod(polar,1)` in the .hlsl is left out verbatim.
#include <metal_stdlib>
#include "polarcoordinates_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut polarcoordinates_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of PolarCoordinates.hlsl psMain (verbatim math).
fragment float4 polarcoordinates_fs(VSOut in [[stage_in]],
                                    texture2d<float> Image    [[texture(0)]],
                                    sampler texSampler        [[sampler(0)]],
                                    constant PolarCoordParams& P     [[buffer(POLARCOORD_Params)]],
                                    constant PolarCoordResolution& R [[buffer(POLARCOORD_Resolution)]]) {
  // HLSL: Image.GetDimensions(width,height) is read but width/height are unused in psMain.
  float aspectRatio = R.TargetWidth / R.TargetHeight;
  float2 p = in.texCoord;

  float2 center  = float2(P.Center[0],  P.Center[1]);
  float2 stretch = float2(P.Stretch[0], P.Stretch[1]);

  float2 polar = float2(0.0f);

  if (P.Mode < 0.5f) {
    // Cartesian2Polar
    p -= 0.5f;
    p.x *= aspectRatio;
    float l = 2.0f * length(p) / P.Radius;
    l = pow(l, P.RadialBias);

    // fork[pi-verbatim]: 3.141578 cloned exactly from TiXL; atan2(p.x, p.y) (x first) verbatim.
    polar = float2(atan2(p.x, p.y) / 3.141578f / 2.0f + 0.5f, l) + center;
    polar.y += P.RadialOffset;
    polar.x += polar.y * P.Twist;
    polar *= stretch;
    // polar = mod(polar,1);  // (commented out in TiXL — kept verbatim)
  } else {
    // Polar2Cartesian
    p.y += P.RadialOffset;
    float angle = p.x * 3.141578f * 2.0f;  // fork[pi-verbatim]
    polar = float2(sin(angle), cos(angle)) * pow(p.y, P.RadialBias) / 2.0f * P.Radius;
    polar.x /= aspectRatio;
    polar.x -= 0.5f;
    polar.y -= 0.5f;
    polar += center;
  }

  float4 orgColor = Image.sample(texSampler, polar);
  return orgColor;
}
