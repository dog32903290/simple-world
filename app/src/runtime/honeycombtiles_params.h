// Shared host<->shader params for the TiXL-ported HoneyCombTiles IMAGE FILTER (lane stylize,
// image/fx/stylize). Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/HexGridDisplace.hlsl
// (HoneyCombTiles.cs is the op wrapper; the shader is HexGridDisplace.hlsl). The op tiles `ImageA`
// into a hexagonal grid: each hex cell samples ImageA at the cell center, looks the luminance up
// through an "Effects" curve LUT, and fills the cell via lerp(Background, Fill, c).
//
// ── STEP-0 STRUCTURE (backward-traced from HoneyCombTiles.t3, the three vetted risks) ──
//   (1) Effects texture (t1, the curve LUT): the .t3 bakes TWO embedded curves through a
//       CurvesToTexture (b14b3243, Horizontal -> a 2-row R32_Float texture). Row 0 = the FIRST
//       MultiInput curve (SampleCurve 2491a6d0, keys (0,0)Linear->(1.0357,1.72)Spline), Row 1 =
//       the SECOND (SampleCurve d466a50a, keys (0,0)Linear->(1,0.18169)Tangent). The shader samples
//       row 0 at float2(value,0) (the value remap) and row 1 at float2(value,0.75) (edgeEffect).
//       These are CONSTANT embedded curves (no external wire) -> the op bakes them in-cook with the
//       MapPointAttributes makeRowTex technique (sw::Curve.sample, the faithful Curve port). NOT a
//       blocked curve seam: the curve currency + sampler + in-cook bake-and-bind all already exist.
//   (2) "Two-pass": NOT two passes. The .t3 has two shader-setup children but _multiImageFxSetup
//       (c290c47c) output is UNCONSUMED (0 source connections) — a TiXL authoring leftover. Only
//       _multiImageFxSetupStatic (4a503a94) produces the final TextureOutput => ONE fullscreen pass.
//   (3) FloatsToBuffer routing: CLEAN 1:1, no intermediate math. The 14 FloatParams (connection
//       order) map straight to the cbuffer field order below: Vector4Components(Fill).{X..W},
//       Vector4Components(Background).{X..W}, Vector2Components(Center).{X,Y}, Divisions,
//       LineThickness, MixOriginal, Rotation. (Center -> the shader's `Offset` field.)
//
// cbuffer ParamConstants (HexGridDisplace.hlsl b0) field order — packed here as scalars
// (particle_params.h discipline, no Vector2/Vector4 forcing host/shader layout drift):
//     float4 Fill;  float4 Background;  float2 Offset;  float Divisions;
//     float LineThickness;  float MixOriginal;  float Rotation;
//
// TiXL .t3 defaults (HoneyCombTiles.t3): Fill (1,1,1,1), Background (1,0.99999,0.99999,0.804),
// Center (0,0), Divisions 20, LineThickness 0, MixOriginal 0, Rotation 0. Image default null.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct HoneyCombTilesParams {
  // cbuffer ParamConstants (HLSL b0), same field order:
  float FillR, FillG, FillB, FillA;              // float4 Fill, default (1,1,1,1)
  float BackgroundR, BackgroundG, BackgroundB, BackgroundA;  // float4 Background, default (1,~1,~1,0.804)
  float OffsetX, OffsetY;                        // float2 Offset (= root Center), default (0,0)
  float Divisions;                               // default 20
  float LineThickness;                           // default 0
  float MixOriginal;                             // default 0
  float Rotation;                                // default 0
};

enum HoneyCombTilesBinding {
  HONEYCOMBTILES_Params = 0,    // constant HoneyCombTilesParams& (b0)
  HONEYCOMBTILES_TexSize = 1,   // constant float2& (HexGridDisplace.hlsl b1 TimeConstants:
                                //   TargetWidth/TargetHeight, framework-injected from output dims)
  // texture(0) = ImageA, texture(1) = Effects (baked curve LUT, 2-row R32_Float).
  // sampler(0) = Repeat/Linear (WrapMode default "Wrap") — shared by ImageA AND the LUT (the .hlsl
  // has exactly one texSampler:register(s0)).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(HoneyCombTilesParams) == 56,
              "HoneyCombTilesParams 56 bytes (14 floats)");
#endif
