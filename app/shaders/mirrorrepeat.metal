// MirrorRepeat: TiXL-ported mirror/rotate kaleidoscope-fold image filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/MirrorRepeat.hlsl (self-
// contained — no shared include). MirrorRepeat.cs/.t3 supply ports/defaults; .t3 sets the sampler
// Wrap=Mirror (-> MTL::SamplerAddressModeMirrorRepeat, set host-side in point_ops_mirrorrepeat.cpp)
// and the default filter is linear (no _ImageFxShaderSetupStatic Point filter in this .t3).
//
// PARITY NOTES (named):
//  - TiXL uses the literal `3.141578` for pi here (NOT 3.141592). Ported verbatim — a "corrected"
//    pi would shift every rotation by ~4.4e-4 rad and break exact parity with the reference.
//  - HLSL `%` on floats lowers to fmod (truncated remainder, sign of dividend). TiXL DECLARES a
//    `mod()` helper (floor-remainder) at the top of the .hlsl but psMain never calls it — both
//    `Offset % 2` and `dist % (2 * Width)` use the `%` OPERATOR (== fmod), not the helper. So MSL
//    `fmod` is the exact equivalent for both. (A floor-`mod` would diverge for negative `dist`.)
//  - Two rotation radians: rotateScreenRad uses (-RotateMirror + RotateImage); mirrorRotationRad
//    uses (+RotateImage) only. Kept distinct (承重 #3).
//  - The mirror-fold branch (`if dist>Width` / `else if dist<0`, inner `mDist>Width`/`mDist<Width`)
//    is the保真 core; sign of `d = -2*(mDist - Width)` ported exactly (承重 #4, refuteFocus).
#include <metal_stdlib>
#include "mirrorrepeat_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
// Shared fullscreen-triangle convention across the image filters (= tint_vs/convertcolors_vs).
vertex VSOut mirrorrepeat_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of MirrorRepeat.hlsl psMain (lines 44-122), line-for-line.
fragment float4 mirrorrepeat_fs(VSOut in [[stage_in]],
                                texture2d<float> ImageA            [[texture(0)]],
                                sampler texSampler                 [[sampler(0)]],
                                constant MirrorRepeatParams& P     [[buffer(MR_Params)]]) {
  const float PI = 3.141578;  // TiXL literal (NOT 3.141592)

  float rotateScreenRad = (-P.RotateMirror + P.RotateImage - 90.0) / 180.0 * PI;

  uint imageWidth = ImageA.get_width();
  uint imageHeight = ImageA.get_height();

  float imageAspect = (float)imageWidth / (float)imageHeight;

  float aspectRatio = P.TargetWidth / P.TargetHeight;
  float2 p = in.texCoord;
  p -= 0.5;
  p.x *= aspectRatio;

  float sina = sin(-rotateScreenRad - PI / 2.0);
  float cosa = cos(-rotateScreenRad - PI / 2.0);

  p = float2(
      cosa * p.x - sina * p.y,
      cosa * p.y + sina * p.x);

  float mirrorRotationRad = (+P.RotateImage - 90.0) / 180.0 * PI;
  float2 angle = float2(sin(mirrorRotationRad), cos(mirrorRotationRad));

  float dist = dot(p, angle);
  float offset = fmod(P.Offset, 2.0);   // HLSL `Offset % 2` (float %  == fmod, truncated)
  dist += offset;

  float shade = 0.0;

  float d = 0.0;
  float mDist = fmod(dist, (2.0 * P.Width));  // HLSL `dist % (2 * Width)`
  if (dist > P.Width) {
    if (mDist > P.Width) {
      shade = 1.0;
      d = -2.0 * (mDist - P.Width);
    }
  } else if (dist < 0.0) {
    mDist *= -1.0;
    if (mDist < P.Width) {
      shade = 1.0;
    } else {
      d = -2.0 * (mDist - P.Width);
    }
  }
  d -= dist - mDist;
  d += offset;
  d += P.OffsetEdge;
  p += d * angle;
  p.x /= aspectRatio;

  p *= float2(aspectRatio / imageAspect, 1.0);
  p += float2(0.5, 0.5);
  p += P.OffsetImage * float2(1.0 / imageAspect, 1.0);

  float4 texColor = ImageA.sample(texSampler, p);

  float4 color = mix(texColor, P.ShadeColor, shade * P.ShadeAmount);  // HLSL lerp

  color = clamp(color, float4(0.0, 0.0, 0.0, 0.0), float4(100.0, 100.0, 100.0, 1.0));
  return color;
}
