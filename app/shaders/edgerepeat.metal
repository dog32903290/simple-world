// EdgeRepeat: TiXL-ported mirror-line repeat / kaleidoscope fold, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/EdgeRepeat.hlsl.
// Folds the image across a rotated line through Center: a point's signed distance along the line
// normal is folded into the [0,1] band; once past the band edge it reflects (mirror), tinting the
// repeated region with Background and the in-band region with Fill, and drawing a LineColor fold line.
//
// Forks (named, DX11->Metal):
//  - fork[flat-cbuffer]: cbuffer read as flat scalars (P.Fill[0..3], P.Center[0/1], ...) so the Metal
//    struct byte layout matches the HLSL register(b0) exactly (see edgerepeat_params.h).
//  - fork[pi-verbatim]: TiXL hardcodes 3.141578 (NOT true pi). Cloned verbatim — true pi would shift
//    the Rotation->radians scale and break parity.
//  - fork[mirror-sampler]: TiXL EdgeRepeat.t3 sets Wrap="Mirror" (load-bearing — the fold relies on
//    the sampler mirroring outside [0,1]). We use MTL MirrorRepeat (NOT the clamp default used by
//    Blur/Tint), faithfully reproducing the edge-repeat behavior.
//  - fork[time-cbuffer-dropped]: the .hlsl TimeConstants cbuffer (b1) is unused by psMain and is NOT
//    ported. The resolution cbuffer (.hlsl b2) is bound at EDGEREPEAT_Resolution.
//  - fork[blendmode-dropped]: TiXL .t3 BlendMode=4 is a host-side _ImageFxShaderSetup2 composite knob
//    (how the op's output blends onto its target), NOT part of psMain — out of scope for the kernel.
#include <metal_stdlib>
#include "edgerepeat_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut edgerepeat_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of EdgeRepeat.hlsl psMain (verbatim math).
fragment float4 edgerepeat_fs(VSOut psInput [[stage_in]],
                              texture2d<float> ImageA   [[texture(0)]],
                              sampler texSampler        [[sampler(0)]],
                              constant EdgeRepeatParams& P     [[buffer(EDGEREPEAT_Params)]],
                              constant EdgeRepeatResolution& R [[buffer(EDGEREPEAT_Resolution)]]) {
  float aspectRatio = R.TargetWidth / R.TargetHeight;
  float2 p = psInput.texCoord;
  p.x /= aspectRatio;
  p -= float2(0.5f / aspectRatio, 0.5f);

  float2 center = float2(P.Center[0], P.Center[1]);

  // fork[pi-verbatim]: 3.141578 cloned exactly from TiXL.
  float radians = P.Rotation / 180.0f * 3.141578f;
  float2 angle = float2(sin(radians), cos(radians));

  float dist = dot(p - center, angle) / P.Width;

  if (dist < 0.0f) {
    dist = -dist;
    angle *= -1.0f;
  }

  float4 colorEffect = float4(P.Fill[0], P.Fill[1], P.Fill[2], P.Fill[3]);

  if (dist > 1.0f) {
    p -= (dist - 1.0f) * P.Width * angle;
    colorEffect = float4(P.Background[0], P.Background[1], P.Background[2], P.Background[3]);
  }
  p += float2(0.5f / aspectRatio, 0.5f);
  p.x *= aspectRatio;

  float line2 = smoothstep(1.0f, 0.0f, abs(1.0f - dist) * 1000.0f * P.Width - P.LineThickness + 1.0f);
  float4 lineColor = float4(P.LineColor[0], P.LineColor[1], P.LineColor[2], P.LineColor[3]);
  colorEffect = mix(colorEffect, lineColor, line2);

  // fork[mirror-sampler]: sampler at sampler(0) carries MirrorRepeat (host-set), reproducing the
  // TiXL Wrap="Mirror" edge-repeat for UVs the fold pushes outside [0,1].
  return ImageA.sample(texSampler, p) * colorEffect;
}
