// KochKaleidoskope: TiXL-ported fractal (Koch-snowflake) kaleidoscope image filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/KochKaleidoscope.hlsl
// (self-contained; original by Martijn Steinrucken / BigWings 2019, CC BY-NC-SA 3.0).
//
// PORT NOTES (named, load-bearing parity risks called out in the work order):
//  [fork-rotation-matrix] .hlsl uses `static float2x2 rotation = rotate2d(...)` and `mul(rotation,uv)`
//    (HLSL mul(M,v) = row semantics). rotate2d fills ROWS (cos,-sin / sin,cos) → mul(rotation,uv) =
//    (cos*x - sin*y, sin*x + cos*y) = standard CCW rotation. We compute that inline (no float2x2)
//    to avoid HLSL-row vs MSL-column ambiguity. rotation is passed as a precomputed float2x2 whose
//    columns are (cos,sin) and (-sin,cos) so MSL `m*v` reproduces HLSL `mul(M,v)` exactly.
//  [fork-loop-bound] .hlsl `for(int i=1; i<Steps; i++)` — Steps is float in the cbuffer (int in .cs).
//    MSL casts `(int)Steps` for the bound so the fold count matches HLSL int-truncation exactly
//    (off-by-one here = wrong fold count; guarded by the golden's injectBug i<=Steps).
//  [fork-4tap-AA] psMain runs KochKaleidoscope 4 times at ±offset (x/y) and averages — ONE pass,
//    4 texture taps, fully preserved (not multi-pass).
//  [fork-center-yflip] uv -= float2(CenterX, 1 - CenterY) — the Y component is flipped (1 - CenterY).
//  [fork-sampler] sampler = Wrap(repeat) per KochKaleidoskope.t3 / the .hlsl Sample of a tiled uv.
//  [fork-clamp] final clamp(c, 0, float4(1000,1000,1000,1)) — alpha capped at 1, rgb at 1000.
//  [fork-resolution] aspect = TargetWidth/TargetHeight and the AA offset's width/height come from the
//    Resolution cbuffer / GetDimensions in .hlsl; we feed output dims via KochKaleidoscopeParams.
//  TimeConstants(b1) is declared-but-unread in the .hlsl → omitted.
#include <metal_stdlib>
#include "kochkaleidoscope_params.h"
using namespace metal;

// .hlsl:43-46  float fmod(float x, float y) { return x - y*floor(x/y); }  (declared, unused in port)
static inline float koch_fmod(float x, float y) { return x - y * floor(x / y); }

// .hlsl:48-51  float2 GetDirection(float angle) { return float2(sin(angle), cos(angle)); }
static inline float2 GetDirection(float angle) { return float2(sin(angle), cos(angle)); }

// .hlsl:59-103  float4 KochKaleidoscope(float2 uv) — `rotation` is the .hlsl static, passed in here.
static inline float4 KochKaleidoscope(float2 uv, float2x2 rotation,
                                      texture2d<float> inputTex, sampler texSampler,
                                      constant KochKaleidoscopeParams& P) {
  uv = rotation * uv;          // HLSL mul(rotation, uv) — see [fork-rotation-matrix]
  uv *= P.Scale;
  uv.x = abs(uv.x);

  float3 col = float3(0.0, 0.0, 0.0);
  float d;

  float angle = 0.0;           // .hlsl:68 (declared, then immediately unused below)
  float2 n = GetDirection((5.0 / 6.0) * 3.1415);

  uv.y += tan((5.0 / 6.0) * 3.1415) * 0.5;
  d = dot(uv - float2(0.5, 0.0), n);
  uv -= max(0.0, d) * n * 2.0;

  float scale = 1.0;
  float foldCount = 0.0;
  n = GetDirection(P.Angle * (2.0 / 3.0) * 3.1415 / 90.0);
  uv.x += 0.5;

  for (int i = 1; i < (int)P.Steps; i++) {   // [fork-loop-bound] int truncation of float Steps
    uv *= 3.0;
    scale *= 3.0;
    uv.x -= 1.5;

    uv.x = abs(uv.x);
    uv.x -= 0.5;
    d = dot(uv, n);
    float foldSideShade = d < 0.0 ? 1.0 : 0.0;
    foldCount += foldSideShade * P.ShadeFolds;
    foldCount += d * P.ShadeSteps;
    float foldFactor = min(0.0, d);
    uv -= foldFactor * n * 2.0;
  }

  d = length(uv - float2(clamp(uv.x, -1.0, 1.0), 0.0));
  col += smoothstep(1.0 / 100.0, 0.0, d / scale);  // .hlsl:97 (col is computed but unused in output)
  uv /= scale;  // normalization

  float4 c = inputTex.sample(texSampler, uv + float2(P.OffsetX, P.OffsetY));
  c.rgb -= foldCount / P.Steps;
  return c;
}

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle (shared convention with the other image filters; matches convertcolors_vs).
vertex VSOut kochkaleidoscope_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// .hlsl:111-128  psMain — aspect, rotation, center y-flip, 4-tap AA, final clamp.
fragment float4 kochkaleidoscope_fs(VSOut in [[stage_in]],
                                    texture2d<float> inputTex          [[texture(0)]],
                                    sampler texSampler                 [[sampler(0)]],
                                    constant KochKaleidoscopeParams& P [[buffer(KK_Params)]]) {
  // .hlsl: uint width,height; inputTexture.GetDimensions(width,height). We use the input texture's
  // own dimensions for the AA offset (identical role to GetDimensions).
  float width  = (float)inputTex.get_width();
  float height = (float)inputTex.get_height();
  float aspect = P.TargetWidth / P.TargetHeight;   // .hlsl:115

  float2 uv = in.texCoord;
  // rotation = rotate2d(Rotate/180 * pi). HLSL rows: (cos,-sin)/(sin,cos); mul(M,v) = row semantics.
  // Build the MSL matrix so `m*v` == HLSL mul(M,v): columns = (cos,sin), (-sin,cos).
  float ang = P.Rotate / 180.0 * 3.141592;          // .hlsl:118 (literal 3.141592)
  float cs = cos(ang), sn = sin(ang);
  float2x2 rotation = float2x2(float2(cs, sn), float2(-sn, cs));  // [fork-rotation-matrix]

  uv -= float2(P.CenterX, 1.0 - P.CenterY);         // [fork-center-yflip] .hlsl:120
  uv.x *= aspect;                                    // .hlsl:121

  float strength = 0.37;
  float2 offset = float2(strength / width, strength / height);  // .hlsl:124

  // .hlsl:126 — 4-tap AA, averaged. [fork-4tap-AA]
  float4 c = (KochKaleidoscope(uv + offset * float2(1.0, 0.0),  rotation, inputTex, texSampler, P) +
              KochKaleidoscope(uv + offset * float2(-1.0, 0.0), rotation, inputTex, texSampler, P) +
              KochKaleidoscope(uv + offset * float2(0.0, 1.0),  rotation, inputTex, texSampler, P) +
              KochKaleidoscope(uv + offset * float2(0.0, -1.0), rotation, inputTex, texSampler, P)) /
             4.0;
  return clamp(c, float4(0.0, 0.0, 0.0, 0.0), float4(1000.0, 1000.0, 1000.0, 1.0));  // [fork-clamp]
}
