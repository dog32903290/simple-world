// Shared host<->shader params for the TiXL-ported SelectPointsWithSDF point MODIFIER (LEAF on the
// direct-Field gather seam, cloned from MoveToSDF). Mirrors external/tixl
// .../Assets/shaders/points/modify/SelectPointsWithField.hlsl (b0/b2 scalars; the field's FloatParams ride
// a SEPARATE assembled buffer at b1 in TiXL / slot 3 here, owned by assembleFieldMSL).
//
// A count-preserving modifier: reads the SDF distance (GetDistance(pos)=GetField(float4(pos,0)).w, w=0 →
// distance branch, IDENTICAL to MoveToSDF's mtsGetDistance) at each point, maps it through
// Mode/Mapping/Range/Center(Offset)/GainAndBias into a selection scalar, and writes it to FX1/FX2 (WriteTo).
// At the .t3 default DiscardNonSelected=FALSE the Scale=NaN discard branch is DEAD → count is preserved.
//
// b0 (SelectPointsWithField.hlsl:9-17) = {Strength, float2 GainAndBias, Scatter, Center, Range}. The .t3
// FloatsToBuffer (de3fceff) routes, in order: Strength, GainAndBias.x, GainAndBias.y, Scatter,
// Offset(->Center), Range. (SelectPointsWithSDF.cs names the field `Offset`; the .hlsl b0 names it `Center`.)
// b2 (hlsl:24-33) = {SelectMode, ClampResult, DiscardNonSelected, StrengthFactor, WriteTo, MappingMode}. The
// .t3 IntsToBuffer (4417c142) routes, in order: Mode(->SelectMode), ClampNegative(BoolToInt ->ClampResult),
// DiscardNonSelected(BoolToInt), StrengthFactor, WriteTo, Mapping(->MappingMode).
//
// We flatten b0 (6 floats) + b2 (6 ints) + Count into one all-scalar struct (no packed_float3 / float4 /
// matrix traps — particle_params.h discipline) so there is NO 16-byte vector padding: 13 × 4 = 52 bytes,
// 4-aligned, byte-identical to the inlined `SpwsdfParams` in selectpointswithsdf_template.metal (verified
// via static_assert below).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SpwsdfParams {
  // --- b0 floats (SelectPointsWithField.hlsl:9-17) ---
  float Strength;       // .cs Strength, default 1.0
  float GainAndBiasX;   // .cs GainAndBias.x, default 0.5
  float GainAndBiasY;   // .cs GainAndBias.y, default 0.5
  float Scatter;        // .cs Scatter, default 0.0
  float Center;         // .cs Offset (b0 name "Center"), default 0.0
  float Range;          // .cs Range, default 1.0
#ifdef __METAL_VERSION__
  // --- b2 ints (hlsl:24-33) ---
  int   SelectMode;          // .cs Mode (Modes), default 0=Override
  int   ClampResult;         // .cs ClampNegative (bool->int), default true(1)
  int   DiscardNonSelected;  // .cs DiscardNonSelected (bool->int), default false(0) -> discard branch DEAD
  int   StrengthFactor;      // .cs StrengthFactor (FModes), default 0=None -> x1
  int   WriteTo;             // .cs WriteTo (FModes), default 1=F1
  int   MappingMode;         // .cs Mapping (MappingModes), default 0=Centered
  uint  Count;               // inherited from upstream bag (modifier: count from input)
#else
  int32_t  SelectMode;
  int32_t  ClampResult;
  int32_t  DiscardNonSelected;
  int32_t  StrengthFactor;
  int32_t  WriteTo;
  int32_t  MappingMode;
  uint32_t Count;
#endif
};

enum SpwsdfBinding {
  SPWSDF_SourcePoints = 0,  // const device SwPoint* (t0)
  SPWSDF_ResultPoints = 1,  // device SwPoint*       (u0)
  SPWSDF_Params       = 2,  // constant SpwsdfParams& (b0)
  SPWSDF_FieldParams  = 3,  // constant FieldParams&  (the assembled field's packed buffer)
};

#ifndef __METAL_VERSION__
// All members are 4-byte scalars (float/int/uint) — NO float4 / packed_float3 — so struct alignment is 4
// and there is NO 16-byte vector padding on either side. 6 floats (24) + 6 ints (24) + Count (4) = 52, and
// the MSL `constant SpwsdfParams&` is the SAME all-scalar layout (also 52, 4-aligned) — byte-identical.
static_assert(sizeof(SpwsdfParams) == 52, "SpwsdfParams must be 52 bytes (13 packed 4-byte scalars)");
#endif
