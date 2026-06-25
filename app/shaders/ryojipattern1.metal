// RyojiPattern1: TiXL-ported recursive-cel pattern generator, single fullscreen pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/generate/RyojiPattern1.hlsl
// (psMain + subDivideCel + the Dave Hoskins hashNN family). The pattern starts from one cel
// covering UV [0,1], then iteratively subdivides it (per-cel hash decides x/y splits) up to
// min(Iterations,10) steps, then fills the final cel with a hashed grayscale lerp of
// Background/Foreground (optionally the sampled image), with an occasional Highlight cel and a
// Padding border drawn in Background.
//
// FORK (named — beatTime host-fed): RyojiPattern1.hlsl reads `beatTime` (TimeConstants b1) only in
// the scroll term. We carry a single host-filled BeatTime (RyojiPattern1Time, b1); a headless cook
// defaults it to 0 -> deterministic, parity-safe (same class as Grain's host-fed Time). See
// ryojipattern1_params.h / point_ops_ryojipattern1.cpp.
//
// FORK (named — Image optional): ImageA (t0) is sampled only when MixOriginal>0
// (color = lerp(Foreground, originalColor, MixOriginal)). The default node has Image=null and
// MixOriginal=0, so the sample is a visual no-op (lerp(...,0)=Foreground). When no input texture is
// wired the cook binds a 1x1 transparent-black dummy so the bind is valid; with MixOriginal=0 the
// dummy is never visible. Sampler: linear + WRAP (RyojiPattern1.t3 address mode = "Wrap").
//
// PORT notes (HLSL -> MSL): frac->fract, lerp->mix, clamp stays, the file-static `P` global in the
// HLSL becomes a thread-local float2 threaded by reference through subDivideCel. The `#define mod`
// macro (x - y*floor(x/y)) is ported verbatim as a helper.
#include <metal_stdlib>
#include "ryojipattern1_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 (V flipped to match DX UV).
vertex VSOut ryojipattern1_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// HLSL: #define mod(x, y) (x - y * floor(x / y))
static inline float2 modHL(float2 x, float2 y) { return x - y * floor(x / y); }

// ---- Dave Hoskins hashNN family (verbatim from RyojiPattern1.hlsl) ----
static inline float hash11(float p) {
  p = fract(p * .1031f);
  p *= p + 33.33f;
  p *= p + p;
  return fract(p);
}
static inline float hash12(float2 p) {
  float3 p3 = fract(float3(p.x, p.y, p.x) * .1031f);
  p3 += dot(p3, p3.yzx + 33.33f);
  return fract((p3.x + p3.y) * p3.z);
}
static inline float2 hash22(float2 p) {
  float3 p3 = fract(float3(p.x, p.y, p.x) * float3(.1031f, .1030f, .0973f));
  p3 += dot(p3, p3.yzx + 33.33f);
  return fract((p3.xx + p3.yz) * p3.zy);
}

// HLSL subDivideCel — mutates the global P; here P is threaded by reference.
static float4 subDivideCel(float4 cel, float2 splitProbability,
                           thread float2& P,
                           constant RyojiPattern1Params& U, float beatTime) {
  float4 orgCel = cel;
  float2 hash = hash22(cel.xy + float2(U.Seed + 0.1f, cel.w) - cel.zw);

  float2 scrollProbability = float2(U.ScrollProbabilityX, U.ScrollProbabilityY);
  float2 scrollSpeed = float2(U.ScrollSpeedX, U.ScrollSpeedY);
  // HLSL: hash > ScrollProbability ? 0 : hash (component-wise). Metal: select(a,b,cond)=b where cond.
  float2 scrollFactor = select(hash, float2(0.0f), hash > scrollProbability);
  float2 randomShift = (beatTime * scrollSpeed - orgCel.zw) * scrollFactor;  // 2
  P = fract(P);
  P -= randomShift;

  float2 subdivisions = float2(U.SubdivisionsX, U.SubdivisionsY);

  if (hash.x > splitProbability.x && hash.y > splitProbability.y)
    return cel;

  // Subdivide
  cel.zw /= float2(
      hash.x < splitProbability.x ? subdivisions.x : 1.0f,
      hash.y < splitProbability.y ? subdivisions.y : 1.0f);

  float2 positionInCel = P - cel.xy;
  float2 splitAlignedPosition = floor(positionInCel / cel.zw) * cel.zw;
  cel.xy += splitAlignedPosition;

  return cel;
}

// Mirror of RyojiPattern1.hlsl psMain.
fragment float4 ryojipattern1_fs(VSOut in [[stage_in]],
                                 texture2d<float> ImageA       [[texture(0)]],
                                 sampler texSampler            [[sampler(0)]],
                                 constant RyojiPattern1Params& U [[buffer(RYOJIPATTERN1_Params)]],
                                 constant RyojiPattern1Time& T   [[buffer(RYOJIPATTERN1_Time)]]) {
  float beatTime = T.BeatTime;
  float2 P = in.texCoord;

  float4 background = float4(U.BackgroundR, U.BackgroundG, U.BackgroundB, U.BackgroundA);
  float4 foreground = float4(U.ForegroundR, U.ForegroundG, U.ForegroundB, U.ForegroundA);
  float4 highlight  = float4(U.HighlightR,  U.HighlightG,  U.HighlightB,  U.HighlightA);
  float2 splitProbability = float2(U.SplitProbabilityX, U.SplitProbabilityY);
  float2 padding = float2(U.PaddingX, U.PaddingY);

  float4 cel = float4(0.0f, 0.0f, 1.0f, 1.0f);
  int steps = (int)min(U.Iterations, 10.0f);
  cel = subDivideCel(cel, float2(1.0f, 1.0f), P, U, beatTime);
  for (int i = 1; i < steps; i++) {
    cel = subDivideCel(cel, splitProbability, P, U, beatTime);
  }

  float2 pp = P - cel.xy;
  float2 posInCel = modHL(pp, cel.zw);
  if (posInCel.x < padding.x * 0.1f || posInCel.y < padding.y * 0.1f) {
    return background;
  }

  float hashForCel = hash12(cel.xy + float2(U.Seed + 0.1f, cel.w));

  float4 originalColor = ImageA.sample(texSampler, P);
  float gray = mix(
      hashForCel,
      hashForCel > U.ForegroundRatio ? 0.0f : 1.0f,
      U.Contrast);

  float4 color = mix(background, mix(foreground, originalColor, U.MixOriginal), gray);
  float hashForCelHighlight = hash11(hashForCel + U.HighlightSeed);
  if (hashForCelHighlight < U.HighlightProbability) {
    color = highlight;
  }
  return clamp(color, float4(0.0f), float4(100.0f, 100.0f, 100.0f, 1.0f));
}
