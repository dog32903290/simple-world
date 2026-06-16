// CheckerBoard: TiXL-ported UV-based checkerboard pattern generator, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/generate/CheckerBoard.hlsl.
// Pure generator: no input texture. Produces a two-color checkerboard pattern from UV coords.
//
// HLSL kernel (verbatim):
//   float aspectRatio = TargetWidth/TargetHeight;
//   float2 p = psInput.texCoord;
//   if(UseAspectRatio > 0.5) { p -= 0.5; p.x *= aspectRatio; }
//   p /= Size * Scale;
//   p += Offset * float2(-1,1);
//   float2 a = mod(p, 1);
//   float t = (a.x > 0.5 && a.y < 0.5) || (a.x < 0.5 && a.y > 0.5) ? 0 : 1;
//   return lerp(ColorA, ColorB, t);
//
// Fork (named, DX11->Metal):
//   - fork[mod-macro]: HLSL defines `#define mod(x,y) (x - y*floor(x/y))`. We use the same
//     formula inline (Metal fmod has different sign for negatives; the floor-based form is
//     faithful and matches TiXL for all real UV ranges including negative Offset).
//   - fork[no-sampler]: CheckerBoard has no texture input — no sampler needed. Pure math on UV.
//   - fork[no-tex-guard]: cookCheckerBoard does NOT check c.inputTexture (it is always null for
//     a generator); the cook runs unconditionally (unlike filter ops that clear-to-black when
//     unwired). This is correct: TiXL CheckerBoard has no Image slot in CheckerBoard.cs.
#include <metal_stdlib>
#include "checkerboard_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut checkerboard_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);  // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);       // flip Y: NDC up vs texture down
  return o;
}

// Mirror of CheckerBoard.hlsl psMain (verbatim math).
fragment float4 checkerboard_fs(VSOut in                        [[stage_in]],
                                constant CheckerBoardParams& P  [[buffer(CHECKERBOARD_Params)]],
                                constant CheckerBoardResolution& R [[buffer(CHECKERBOARD_Resolution)]]) {
  float aspectRatio = R.TargetWidth / R.TargetHeight;

  float2 p = in.texCoord;

  if (P.UseAspectRatio > 0.5f) {
    p -= 0.5f;
    p.x *= aspectRatio;
  }

  float2 size = float2(P.SizeX, P.SizeY);
  p /= size * P.Scale;
  p += float2(P.OffsetX, P.OffsetY) * float2(-1.0f, 1.0f);

  // fork[mod-macro]: HLSL `#define mod(x,y) (x - y*floor(x/y))` with y=1 simplifies to p-floor(p).
  // fract() is the Metal equivalent of (x - floor(x)); identical to TiXL macro for all real x.
  float2 a = fract(p);

  float t = ((a.x > 0.5f && a.y < 0.5f) || (a.x < 0.5f && a.y > 0.5f)) ? 0.0f : 1.0f;

  float4 colorA = float4(P.ColorAR, P.ColorAG, P.ColorAB, P.ColorAA);
  float4 colorB = float4(P.ColorBR, P.ColorBG, P.ColorBB, P.ColorBA);
  return mix(colorA, colorB, t);
}
