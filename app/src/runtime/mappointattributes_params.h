// Shared host<->shader params for the TiXL-ported MapPointAttributes — the bake-into-point seam
// consumer (PointCookCtx::inputCurves/inputGradients). Mirrors the TWO cbuffers of external/tixl
// .../Assets/shaders/points/modify/MapPointAttributes.hlsl:6-21 (read the .hlsl cbuffer DIRECTLY — the
// .t3 builds them via FloatsToBuffer/IntsToBuffer node graphs; Cut55 trap: do NOT reconstruct the node
// graph). The .hlsl cbuffers are:
//
//   cbuffer Params : register(b0) { float Strength; float Range; float Phase; }
//   cbuffer Params : register(b1) { int InputMode; int MappingMode; int ApplyMode;
//                                   int WriteTo; int WriteColor; int StrengthFactor; }
//
// We fold both into ONE host struct (the cook does a single setBytes); the kernel reads the same fields.
// The int enums are carried as float (the resolved-param spine is float) and cast to int in the kernel,
// matching the project's Float-port convention (every existing op does this). Count is OUR addition (not
// in the .hlsl cbuffer): the TiXL kernel reads point count via SourcePoints.GetDimensions(); we pass it
// + guard tid>=Count (mirror samplepointcolorattributes_params.h — the dispatch is
// calcDispatchCount(count,tg) threadgroups, so tid can overrun the bag).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — MapPointAttributes (16-byte rows). b0 floats first, then b1 enums (carried as float),
// then Count + pad. 32 bytes = 2×16.
struct MapPointAttrParams {
  float Strength;       // b0 (MapPointAttributes.hlsl:8)
  float Range;          // b0 (:9)
  float Phase;          // b0 (:10)
  float InputMode;      // b1 (:15) — int enum carried as float, cast in kernel
                        // -> 16
  float MappingMode;    // b1 (:16)
  float ApplyMode;      // b1 (:17) — TiXL WriteMode (Replace/Multiply/Add)
  float WriteTo;        // b1 (:18) — None/F1/F2/Scale
  float WriteColor;     // b1 (:19) — None/Replace/Multiply (default 2)
                        // -> 32
  float StrengthFactor; // b1 (:20) — None/F1/F2
#ifdef __METAL_VERSION__
  uint  Count;          // OUR addition (guard); not in the .hlsl cbuffer (see note)
#else
  uint32_t Count;
#endif
  float _pad0, _pad1;   // -> 48 (pad to 16-byte row)
};

enum MapPointAttrBinding {
  MPA_SourcePoints = 0,  // const device SwPoint* (t0)
  MPA_ResultPoints = 1,  // device SwPoint*       (u0)
  MPA_Params       = 2,  // constant MapPointAttrParams& (b0)
};
// Texture + sampler bind slots (separate spaces from buffers; mirror samplepointcolorattributes.metal).
// CurveImage @t1, GradientImage @t2 in HLSL → texture(0)/texture(1) in MSL.
enum MapPointAttrTexBinding {
  MPA_CurveImage    = 0,  // Texture2D<float4> CurveImage    (t1; texture(0) in MSL)
  MPA_GradientImage = 1,  // Texture2D<float4> GradientImage (t2; texture(1) in MSL)
};
enum MapPointAttrSamplerBinding {
  MPA_ClampedSampler = 0,  // sampler ClampedSampler (s0; Clamp/Clamp + Linear per .t3)
};

#ifndef __METAL_VERSION__
// 16 (Strength/Range/Phase/InputMode) + 16 (Mapping/Apply/WriteTo/WriteColor) + 16 (StrengthFactor/
// Count/pad/pad) = 48 bytes.
static_assert(sizeof(MapPointAttrParams) == 48, "MapPointAttrParams must be 48 bytes (3x16)");
#endif
