// RyojiPattern2: TiXL-ported recursive-subdivision pattern generator, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/generate/RyojiPattern2.hlsl
// (psMain, line 259). The unit cell (0,0,1,1) is subdivided SIX times by subDivideCel2 with the
// SplitProbability / SplitA(Splits)/SplitB/SplitC cadence; each surviving cel is colored by a
// hash-driven gray ramp lerped (by Contrast) and blended (by MixOriginal) with the sampled ImageA,
// then a random subset is overwritten by Highlight. Background fills the per-cel Padding gutters.
//
// FORK (named — beatTime): HLSL b1 TimeConstants.beatTime drives subDivideCel2's scroll term. This
// clone has no beatTime clock in the texture cook path; we take BeatTime as a host-supplied param
// (P.BeatTime, default 0 -> static; selftest passes 0). beatTime=0 keeps the full pattern (it only
// scales the cel scroll offset, not the subdivision/coloring). See ryojipattern2_params.h.
//
// FORK (named, DX11->Metal): HLSL `static float2 P;` (file-global mutated by subDivideCel2) -> a
// thread-local float2 passed by reference (thread float2&). HLSL Sample(...) -> Metal sample(...).
// Sampler: linear + REPEAT (RyojiPattern2.t3 leaves _ImageFxShaderSetup2.Wrap at its default "Wrap").
// hash11/hash13/etc. from the .hlsl are dropped — only hash22 / hash12 are referenced by psMain.
#include <metal_stdlib>
#include "ryojipattern2_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut ryojipattern2_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// HLSL: #define mod(x, y) (x - y * floor(x / y))
static inline float2 mod2(float2 x, float2 y) { return x - y * floor(x / y); }

// HLSL hash22: 2 out, 2 in (line 94).
static inline float2 hash22(float2 p) {
  float3 p3 = fract(float3(p.x, p.y, p.x) * float3(0.1031f, 0.1030f, 0.0973f));
  p3 += dot(p3, p3.yzx + 33.33f);
  return fract((p3.xx + p3.yz) * p3.zy);
}

// HLSL hash12: 1 out, 2 in (line 66).
static inline float hash12(float2 p) {
  float3 p3 = fract(float3(p.x, p.y, p.x) * 0.1031f);
  p3 += dot(p3, p3.yzx + 33.33f);
  return fract((p3.x + p3.y) * p3.z);
}

// HLSL subDivideCel2 (line 208). `P` is the HLSL file-global accumulator; here a thread-local ref.
static inline float4 subDivideCel2(float4 cel, float2 splitProbability, float2 split,
                                   float2 scrollProbability, thread float2& P,
                                   constant RyojiPattern2Params& C,
                                   texture2d<float> ImageA, sampler texSampler) {
  float2 hash2 = hash22(cel.xy + C.Seed);
  // HLSL: float2 scrollFactor = hash2 > scrollProbability ? 0:1;
  float2 scrollFactor = float2(hash2.x > scrollProbability.x ? 0.0f : 1.0f,
                               hash2.y > scrollProbability.y ? 0.0f : 1.0f);
  // HLSL: randomShift = (beatTime * ScrollSpeed +1 + ScrollOffset) * scrollFactor * scrollProbability * hash2.x;
  float2 scrollSpeed = float2(C.ScrollSpeedX, C.ScrollSpeedY);
  float2 randomShift = (C.BeatTime * scrollSpeed + 1.0f + C.ScrollOffset)
                       * scrollFactor * scrollProbability * hash2.x;
  P += randomShift;

  // HLSL: float2 hash = hash22(cel.xy + float2(Seed, cel.w) - cel.zw);
  float2 hash = hash22(cel.xy + float2(C.Seed, cel.w) - cel.zw);
  if (hash.x > splitProbability.x && hash.y > splitProbability.y)
    return cel;

  // HLSL samples ImageA here (value unused for the return — kept for parity / side-effect-free).
  // (TiXL declares `float4 color = ImageA.Sample(...)` then discards it; we omit the dead sample.)

  float2 subdiv = float2(hash.x < splitProbability.x ? split.x : 1.0f,
                         hash.y < splitProbability.y ? split.y : 1.0f);
  cel.zw /= subdiv;
  float2 positionInCel = P - cel.xy;
  float2 splitAlignedPosition = floor(positionInCel / cel.zw) * cel.zw;
  cel.xy += splitAlignedPosition;
  return cel;
}

// Mirror of RyojiPattern2.hlsl psMain (line 259).
fragment float4 ryojipattern2_fs(VSOut in [[stage_in]],
                                 texture2d<float> ImageA          [[texture(0)]],
                                 sampler texSampler               [[sampler(0)]],
                                 constant RyojiPattern2Params& C   [[buffer(RYOJIPATTERN2_Params)]],
                                 constant RyojiPattern2Resolution& R [[buffer(RYOJIPATTERN2_Resolution)]]) {
  (void)R;
  float2 P = in.texCoord;  // HLSL: P = psInput.texCoord;

  float4 cel = float4(0, 0, 1, 1);
  float2 splitA = float2(C.SplitAX, C.SplitAY);
  float2 splitB = float2(C.SplitBX, C.SplitBY);
  float2 splitC = float2(C.SplitCX, C.SplitCY);
  float2 splitProb = float2(C.SplitProbabilityX, C.SplitProbabilityY);
  float2 scrollProb = float2(C.ScrollProbabilityX, C.ScrollProbabilityY);

  // HLSL lines 270-278 (six subdivisions, verbatim split-probability cadence).
  cel = subDivideCel2(cel, float2(1, 0),             splitA, float2(0, 0),  P, C, ImageA, texSampler);
  cel = subDivideCel2(cel, float2(0, splitProb.y),   splitA, scrollProb,    P, C, ImageA, texSampler);
  cel = subDivideCel2(cel, float2(splitProb.x, 0),   splitB, scrollProb,    P, C, ImageA, texSampler);
  cel = subDivideCel2(cel, float2(0, splitProb.y),   splitB, scrollProb,    P, C, ImageA, texSampler);
  cel = subDivideCel2(cel, float2(splitProb.x, 0),   splitC, scrollProb,    P, C, ImageA, texSampler);
  cel = subDivideCel2(cel, float2(0, splitProb.y),   splitC, scrollProb,    P, C, ImageA, texSampler);

  // HLSL lines 286-291: Padding gutter -> Background.
  float2 pp = P - cel.xy;
  float2 posInCel = mod2(pp, cel.zw);
  float2 padding = float2(C.PaddingX, C.PaddingY);
  float4 background = float4(C.BackgroundR, C.BackgroundG, C.BackgroundB, C.BackgroundA);
  if (posInCel.x < padding.x * 0.1f || posInCel.y < padding.y * 0.1f) {
    return background;
  }

  // HLSL lines 293-301: per-cel hash -> gray ramp -> lerp(Background, lerp(Foreground, original, MixOriginal), gray).
  float2 hashForCel1 = hash22(cel.xy + float2(cel.z, cel.w) / 2.0f);
  float hashForCel = hash12(cel.xy + hashForCel1);
  float4 originalColor = ImageA.sample(texSampler, P);
  float4 foreground = float4(C.ForegroundR, C.ForegroundG, C.ForegroundB, C.ForegroundA);
  float gray = mix(1.0f - hashForCel,
                   hashForCel > C.ForegroundRatio ? 0.0f : 1.0f,
                   C.Contrast);
  float4 color = mix(background, mix(foreground, originalColor, C.MixOriginal), gray);

  // HLSL lines 306-308: random Highlight overlay.
  if (hashForCel1.x < C.HighlightProbability) {
    color = float4(C.HighlightR, C.HighlightG, C.HighlightB, C.HighlightA);
  }
  return color;
}
