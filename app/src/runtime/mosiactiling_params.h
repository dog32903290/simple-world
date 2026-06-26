// Shared host<->shader params for the TiXL-ported MosiacTiling IMAGE FILTER (lane multi-image,
// image/fx/stylize). Faithful 1:1 mirror of
//   external/tixl Operators/Lib/Assets/shaders/img/fx/MosiacTiling.hlsl.
//
// MosiacTiling recursively subdivides the frame into quad cells (quadtree), stopping a branch when
// the FxImage's four corner samples agree (their pair-distance + a hashed random jitter is below
// SubdivisionThreshold) — so the 2nd input (FxImage) DRIVES the tiling structure per-pixel. The cell
// is then filled from the Image with a Padding/Feather gap (GapColor) between tiles.
//
// .t3 STEP-0 (ATOMIC op, OWN fullscreen pixel shader): MosiacTiling.t3 wraps the SINGLE
// MosiacTiling.hlsl psMain through the standard _multiImageFxSetup render-pipeline compound. The
// op's BEHAVIOR is the one .hlsl psMain. Fixed numbered Texture2D ports (Image t0 / FxImage t1), NOT
// MultiInput. The cbuffer is fed by _multiImageFxSetup's FloatsToBuffer MultiInput — backward-traced
// (MosiacTiling.t3 connection order into slot bcc7fb78), the float order is EXACTLY the cbuffer field
// order below (Center.xy, Stretch.xy, Size, SubdivisionThreshold, Padding, Feather, GapColor.rgba,
// MixOriginal, IntToFloat(MaxSubdivisions), Randomize) — a 1:1 routing, NO intermediate math except
// IntToFloat (a pure int->float cast, no scaling). NOT a DirectionalBlur-style trap.
//
// cbuffer ParamConstants (HLSL b0) field order — packed here as all-scalar floats + one vec4
// (particle_params.h discipline). HLSL byte layout: Center(0) Stretch2(8) Size(16) SubdivThr(20)
// Padding(24) Feather(28) GapColor(32, 16-aligned) MixOriginal(48) MaxSubdivisions(52) Randomize(56)
// -> 60 bytes padded to 64. A flat run of 15 floats reproduces this byte layout exactly because
// GapColor lands naturally 16-aligned (offset 32), so no HLSL register straddle to mimic.
//
// FAITHFUL-UNUSED (named): `Stretch2` (cbuffer offset 8) is declared in MosiacTiling.hlsl but NEVER
// read by psMain (the cell math uses only Center/Size/aspectRatio). Kept in the layout for byte
// parity with TiXL's cbuffer; the op routes the Stretch input into it (matching the .t3) but the
// pixel result is independent of Stretch. Documented so a future edit knows it is intentionally dead.
//
// SAMPLER (MosiacTiling.t3 via _multiImageFxSetup, verbatim): one sampler s0 used for BOTH Image and
// FxImage = Filter MinMagMipLinear (linear), WrapMode MirrorOnce (AddressU/V = MirrorClampToEdge in
// Metal). All SampleLevel(s, uv, 0) calls use mip level 0.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

#ifdef __METAL_VERSION__
struct MosiacTilingParams {
  float2 Center;                // cbuffer offset 0
  float2 Stretch2;              // offset 8  — FAITHFUL-UNUSED (declared, never read by psMain)
  float Size;                   // offset 16
  float SubdivisionThreshold;   // offset 20
  float Padding;                // offset 24
  float Feather;                // offset 28
  float4 GapColor;              // offset 32 (16-aligned)
  float MixOriginal;            // offset 48
  float MaxSubdivisions;        // offset 52 (IntToFloat cast of the int input)
  float Randomize;              // offset 56
};
#else
struct MosiacTilingParams {
  float Center_x, Center_y;                 // cbuffer offset 0,4
  float Stretch2_x, Stretch2_y;             // offset 8,12 — FAITHFUL-UNUSED
  float Size;                               // offset 16
  float SubdivisionThreshold;               // offset 20
  float Padding;                            // offset 24
  float Feather;                            // offset 28
  float GapColor_r, GapColor_g, GapColor_b, GapColor_a;  // offset 32..44 (16-aligned)
  float MixOriginal;                        // offset 48
  float MaxSubdivisions;                    // offset 52
  float Randomize;                          // offset 56
  float _pad;                               // pad 60 -> 64 (16-byte multiple)
};
#endif

enum MosiacTilingBinding {
  MOSIACTILING_Params = 0,  // constant MosiacTilingParams& (b0)
  // texture(0)=Image, texture(1)=FxImage. sampler(0)=texSampler (linear / MirrorClampToEdge).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(MosiacTilingParams) == 64,
              "MosiacTilingParams 64 bytes (15 floats + 1 pad, GapColor 16-aligned at offset 32)");
#endif
