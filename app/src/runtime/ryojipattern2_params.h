// Shared host<->shader params for the TiXL-ported RyojiPattern2 IMAGE op (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/RyojiPattern2.hlsl and
// RyojiPattern2.cs/.t3. TiXL authority: RyojiPattern2.hlsl psMain (line 259) — subdivides a unit
// cell six times (subDivideCel2), then colors each cel by a hash-driven gray ramp lerped between
// Background / Foreground / sampled ImageA, with a random Highlight overlay.
//
// PARAM ORDER = the .hlsl cbuffer ParamConstants (b0) verbatim (lines 1-22). RyojiPattern2.t3 fills
// this cbuffer via _ImageFxShaderSetup2's float-list (slot 8e9b8826) and the connection ORDER in the
// .t3 is 1:1 with this cbuffer layout (Background.xyzw, Foreground.xyzw, Highlight.xyzw, Splits->
// SplitA.xy, SplitB.xy, SplitC.xy, SplitProbability.xy, ScrollSpeed.xy, ScrollProbability.xy,
// Padding.xy, Contrast, Seed, ForegroundRatio, HighlightProbability, MixOriginal, ScrollOffset,
// HighlightSeed-via-IntToFloat). No intermediate math except IntToFloat(HighlightSeed) — a plain
// int->float cast — so this is a clean 1:1 cbuffer mirror (no FloatsToBuffer recomputation trap).
//
// FORK (named — TimeConstants b1 / beatTime): the .hlsl's b1 (globalTime/time/runTime/beatTime) is
// not bound by _ImageFxShaderSetup2 the way b0 is; only `beatTime` is referenced (in subDivideCel2's
// scroll term). This clone has no beatTime clock in the texture-cook path (EvaluationContext exposes
// `time` seconds + `localFxTime` bars, not beatTime). We carry BeatTime as ONE trailing host-supplied
// float (mirror of Grain's host-supplied Time): the cook fills it from ctx->time (or 0 headless), and
// the selftest passes BeatTime=0 -> deterministic. beatTime=0 still produces the full pattern (it only
// scales the scroll offset); see ryojipattern2.metal. This is the same fork class as Grain's Time.
//
// Resolution cbuffer (b1 here) carries the OUTPUT dims — RyojiPattern2 is a fullscreen generator that
// samples ImageA at the same texCoord, so no GetDimensions tile math is needed; we keep a Resolution
// struct only to mirror the image-filter convention and in case a future tweak needs it.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RyojiPattern2Params {
  // ParamConstants (b0) — verbatim order from RyojiPattern2.hlsl lines 3-21.
  float BackgroundR, BackgroundG, BackgroundB, BackgroundA;  // float4 Background
  float ForegroundR, ForegroundG, ForegroundB, ForegroundA;  // float4 Foreground
  float HighlightR, HighlightG, HighlightB, HighlightA;       // float4 Highlight
  float SplitAX, SplitAY;                                     // float2 SplitA  (Splits input)
  float SplitBX, SplitBY;                                     // float2 SplitB
  float SplitCX, SplitCY;                                     // float2 SplitC
  float SplitProbabilityX, SplitProbabilityY;                 // float2 SplitProbability
  float ScrollSpeedX, ScrollSpeedY;                           // float2 ScrollSpeed
  float ScrollProbabilityX, ScrollProbabilityY;               // float2 ScrollProbability
  float PaddingX, PaddingY;                                   // float2 Padding
  float Contrast;                                             // float Contrast
  float Seed;                                                 // float Seed
  float ForegroundRatio;                                      // float ForegroundRatio
  float HighlightProbability;                                 // float HighlightProbability
  float MixOriginal;                                          // float MixOriginal
  float ScrollOffset;                                         // float ScrollOffset
  float HighlightSeed;                                        // float HighlightSeed (IntToFloat)
  // FORK (named): host-supplied beatTime (b1 TimeConstants.beatTime). 0 = deterministic / static.
  float BeatTime;
  // 12 (3 vec4) + 14 (7 vec2) + 7 scalars + 1 BeatTime = 34 floats; pad 2 -> 36 (144 bytes).
  float _pad0, _pad1;
};

struct RyojiPattern2Resolution {
  // Output dims (mirror of the image-filter Resolution cbuffer convention). RyojiPattern2 does not
  // use GetDimensions tile math (it samples ImageA at texCoord directly), so these are informational.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16
};

enum RyojiPattern2Binding {
  RYOJIPATTERN2_Params     = 0,  // constant RyojiPattern2Params& (b0)
  RYOJIPATTERN2_Resolution = 1,  // constant RyojiPattern2Resolution& (b1)
  // texture(0) = ImageA, sampler(0) = linear+repeat (RyojiPattern2 default Wrap=Wrap).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RyojiPattern2Params) == 144, "RyojiPattern2Params 144 bytes (16-byte multiple)");
static_assert(sizeof(RyojiPattern2Resolution) == 16, "RyojiPattern2Resolution 16 bytes");
#endif
