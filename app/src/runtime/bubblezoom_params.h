// Shared host<->shader params for the TiXL-ported BubbleZoom IMAGE FX
// (image/fx/distort). Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/
// BubbleZoom.hlsl (b0 ParamConstants, lines 3-12) + BubbleZoom.cs/.t3 defaults.
//
// TiXL authority:
//   BubbleZoom.cs   — slot declarations: Image, Center, Magnify, Feather, FeatherGradient, Radius,
//                     FlipEffect, GainAndBias, Resolution, Bias. (Bias is DEAD — see fork below.)
//   BubbleZoom.t3   — default values (Center=(0,0), Magnify=1.25, Feather=1.0, Radius=0.5,
//                     GainAndBias=(0.5,0.5), FlipEffect=0.0, Bias=0.0) + the FeatherGradient→
//                     GradientsToTexture→t1 plumbing (the op's OWN FeatherGradient slot default
//                     OVERRIDES the GradientsToTexture child's embedded magenta→blue default).
//   BubbleZoom.hlsl — cbuffer b0 field order (lines 5-12) + b1 Resolution (lines 14-18) + psMain.
//
// b0 ParamConstants layout (VERBATIM HLSL cbuffer field order, BubbleZoom.hlsl lines 5-12).
// HLSL packs scalars/float2 tightly UNLESS a var straddles a 16-byte register; trace shows NO straddle:
//   float2 Center;        offset   0   (default (0,0))            reg0 [0-7]
//   float  ScaleFactor;   offset   8   (<- Magnify input, def 1.25) reg0 [8-11]
//   float  Feather;       offset  12   (default 1.0)              reg0 [12-15]  (reg0 full)
//   float  Radius;        offset  16   (default 0.5)              reg1 [16-19]
//   float2 GainAndBias;   offset  20   (default (0.5,0.5))        reg1 [20-27]  (no straddle: in reg1)
//   float  FlipEffect;    offset  28   (default 0.0)              reg1 [28-31]  (reg1 full)
// Total: 8 floats × 4 = 32 bytes. No straddle → no interior padding; both registers exactly full.
//
// ★HLSL cbuffer field name vs BubbleZoom.cs input name (the only rename trap):
//   HLSL "ScaleFactor" (b0, offset 8) <- .cs input "Magnify" (Single, .t3 default 1.25). The cook fn
//   maps the op input Magnify to this shader field. No Multiply/PickFloat in the .t3 — the connection
//   wires Magnify DIRECTLY into the FloatsToBuffer slot that fills this cbuffer (traced, Cut55
//   discipline). [fork-magnify-rename]
//
// ★DEAD input: BubbleZoom.cs declares a `Bias` input (Single, .t3 default 0.0) that is UNCONNECTED in
//   the .t3 (no Connection targets its GUID e5a5d0cf). It feeds NO shader field and NO child. We do NOT
//   add a cbuffer slot for it and we do NOT wire it. [fork-dead-bias]
//
// ★No IsTextureValid: UNLIKE the gradient GENERATORS (LinearGradient/RadialGradient), BubbleZoom is an
//   FX — Image is mandatory (t0). The .hlsl always samples ImageA; there is no generator branch. When
//   the host has no upstream Image the cook binds a 1×1 transparent-black dummy (so ImageA is a valid
//   handle), matching the shader's unconditional ImageA.Sample. No IsTextureValid field exists.
//
// b1 Resolution{TargetWidth, TargetHeight} — host-filled from output size; bound at Metal fragment
// cbuffer index 1 (same pattern as RadialGradient / LinearGradient / NGon).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct BubbleZoomParams {
  // b0 ParamConstants — field order MUST match BubbleZoom.hlsl cbuffer verbatim (lines 5-12).
  float CenterX, CenterY;            // float2 Center,      default (0,0)
  float ScaleFactor;                 // <- Magnify input (Single), default 1.25  [fork-magnify-rename]
  float Feather;                     // TiXL Feather (Single),  default 1.0
  float Radius;                      // TiXL Radius (Single),   default 0.5
  float GainAndBiasX, GainAndBiasY;  // float2 GainAndBias, default (0.5,0.5)
  float FlipEffect;                  // TiXL FlipEffect (Single), default 0.0 (lerp dBiased↔1-dBiased)
  // 8 floats × 4 = 32 bytes. No straddle → no interior padding (see header note).
};

struct BubbleZoomResolution {
  // Mirrors BubbleZoom.hlsl b1 Resolution cbuffer; host-filled from output size.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8->16 bytes
};

enum BubbleZoomBinding {
  BUBBLEZOOM_Params     = 0,  // constant BubbleZoomParams& (folds BubbleZoom.hlsl b0)
  BUBBLEZOOM_Resolution = 1,  // constant BubbleZoomResolution& (b1; Metal index 1)
  // texture(0) = ImageA (Image input), sampler(0) = texSampler (Clamp — .t3 WrapMode=Clamp).
  // texture(1) = Gradient (rasterized 1xN row), sampler(1) = clampedSampler (ClampToEdge).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(BubbleZoomParams) == 32,
              "BubbleZoomParams must be 32 bytes (8 floats; HLSL b0 field offsets, no straddle)");
static_assert(sizeof(BubbleZoomResolution) == 16, "BubbleZoomResolution 16 bytes");
#endif
