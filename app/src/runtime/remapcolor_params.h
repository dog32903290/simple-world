// Shared host<->shader params for the TiXL-ported RemapColor IMAGE FILTER (image/color).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/ColorRemap.hlsl b0 ParamConstants
// (lines 3-12) VERBATIM, + RemapColor.cs/.t3 defaults & connection routing.
//
// TiXL authority:
//   RemapColor.cs       — slot declarations: Image, Gradient, Mode(enum), Exposure, GainAndBias(Vec2),
//                         Cycle, WrapMode(TextureAddressMode), DontColorAlpha(bool), Resolution(Int2),
//                         Repeat, GradientSteps(int). NOTE: there is NO "Offset" input on the .cs.
//   ColorRemap.hlsl     — cbuffer b0 field order (the 6 fields below) + psMain (per-channel gradient
//                         lookup at float2(orgColor.<ch> + Offset, 0)).
//   RemapColor.t3       — defaults + the FloatsToBuffer (_multiImageFxSetupStatic) cbuffer fill order.
//
// ★★ Cut55 ROUTING TRACE (adversarial — the cbuffer fill order is NOT a 1:1 op-input→field copy).
// RemapColor embeds _multiImageFxSetupStatic (9c2895b1) whose FloatsToBuffer multi-input (slot
// 2929c4c9) is filled in WIRE-DECLARATION order. Tracing RemapColor.t3's Connections array into that
// slot (in order) gives EXACTLY the ColorRemap.hlsl b0 field order:
//   b0 field        | wire | .t3 source (through which node)                 | host expression
//   ----------------+------+------------------------------------------------+----------------------------
//   DontColorAlpha  | [0]  | ROOT.DontColorAlpha(bool) → BoolToFloat node    | DontColorAlpha?1:0
//   Mode            | [1]  | ROOT.Mode(int=1)          → IntToFloat node     | (float)Mode  (default 1!)
//   Offset          | [2]  | ROOT.Cycle (b1763a8b)     → DIRECT              | Cycle  ← ★the rename trap
//   Exposure        | [3]  | ROOT.Exposure             → DIRECT              | Exposure
//   GainAndBias.x   | [4]  | ROOT.GainAndBias → Vector2Components.x (1cee5adb)| GainAndBias.x (gain)
//   GainAndBias.y   | [5]  | ROOT.GainAndBias → Vector2Components.y (305d321d)| GainAndBias.y (bias)
//   Repeat          | [6]  | ROOT.Repeat (7023f71c)    → DIRECT              | Repeat
//
// ★ The two non-1:1 routings (named forks, replicated as scalar expressions in the cook fn):
//   • [fork-offset-from-cycle]    HLSL cbuffer "Offset" ← the op's "Cycle" input (the .cs has NO Offset
//                                 input). The shader's `float2(orgColor.<ch> + Offset, 0)` lookup is
//                                 driven by the user-facing "Cycle" slider. .t3 Cycle default 0.0.
//   • [fork-dontcoloralpha-bool]  HLSL "DontColorAlpha" ← bool input via a BoolToFloat node (1.0/0.0).
//                                 .t3 default false → 0.0 (so gradient.a is used, not orgColor.a).
//   • GradientSteps & WrapMode & Image & Resolution do NOT enter b0 — GradientSteps→GradientsToTexture
//     .Resolution (the row width, see gradient_raster.h [fork-gradientsteps-is-resolution]); WrapMode→
//     _multiImageFxSetupStatic sampler (s0); Image→t0; Resolution→output size.
//
// b0 ParamConstants layout (VERBATIM ColorRemap.hlsl lines 5-11). No straddle → tight packing:
//   float  DontColorAlpha;  offset  0   reg0 [0-3]
//   float  Mode;            offset  4   reg0 [4-7]
//   float  Offset;          offset  8   reg0 [8-11]
//   float  Exposure;        offset 12   reg0 [12-15]  (reg0 full)
//   float2 GainAndBias;     offset 16   reg1 [16-23]
//   float  Repeat;          offset 24   reg1 [24-27]
// Total: 7 floats × 4 = 28 bytes. (HLSL pads the cbuffer up internally, but the HOST writes only the
// 28 bytes the shader reads — Metal setFragmentBytes copies the struct; field offsets match byte-for-
// byte. No interior padding needed — no field straddles a 16-byte register.)
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RemapColorParams {
  // b0 ParamConstants — field order MUST match ColorRemap.hlsl cbuffer verbatim (lines 5-11).
  float DontColorAlpha;              // ← bool input via BoolToFloat (default 0) [fork-dontcoloralpha-bool]
  float Mode;                        // ← Mode int via IntToFloat (default 1 = IndividualChannels)
  float Offset;                      // ← the "Cycle" input (default 0) [fork-offset-from-cycle]
  float Exposure;                    // ← Exposure (default 1.0)
  float GainAndBiasX, GainAndBiasY;  // float2 GainAndBias ← Vector2Components(GainAndBias) (default .5,.5)
  float Repeat;                      // ← Repeat (default 1.0)
  // 7 floats × 4 = 28 bytes. No straddle → no interior padding (see header note).
};

enum RemapColorBinding {
  REMAPCOLOR_Params = 0,  // constant RemapColorParams& (folds ColorRemap.hlsl b0)
  // texture(0) = ImageA (Image, 1×1 transparent-black dummy when unwired), sampler(0) = linearSampler.
  // texture(1) = Gradient (rasterized 1×GradientSteps row),               sampler(1) = clampedSampler.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RemapColorParams) == 28,
              "RemapColorParams must be 28 bytes (7 floats; HLSL b0 field offsets, no straddle)");
#endif
