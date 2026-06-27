// Shared host<->shader params for the TiXL-ported SetAttributesWithPointFields — the
// second-point-buffer point-modify (PointCookCtx::inputs[1] = FieldPoints) + bake-into-point seam
// consumer (a Gradient → GradientImage). Mirrors the TWO cbuffers of external/tixl
// .../Assets/shaders/points/modify/SetPointAttributesWithPointFields.hlsl:21-43 (read the .hlsl
// cbuffer DIRECTLY — the .t3 builds them via FloatsToBuffer/IntsToBuffer node graphs; Cut55 trap: do
// NOT reconstruct the node graph). The .hlsl cbuffers are:
//
//   cbuffer Params : register(b0) {
//       float Amount; float Range; float OffsetRange; float AffectPosition;
//       float3 OrientationUpVector; float AffectOrientation;
//       float AffectW; float AffectColor; float2 GainAndBias; float Variation; }
//   cbuffer Params : register(b1) {
//       int FieldCount; int ColorMode; int WMode; int WCurveAffectsWeight; }
//
// We fold both into ONE host struct (the cook does a single setBytes); the kernel reads the same
// fields. The int enums (ColorMode/WMode/WCurveAffectsWeight) are carried as float (the resolved-param
// spine is float) and cast to int in the kernel — the project's Float-port convention. FieldCount is
// the FieldPoints (inputs[1]) element count, passed from the cook (the TiXL kernel reads it via
// FieldPoints.GetDimensions()). Count is OUR addition (the SourcePoints/inputs[0] element count): the
// TiXL kernel reads it via SourcePoints.GetDimensions(); we pass it + guard tid>=Count (dispatch is
// calcDispatchCount(count,tg) threadgroups so tid can overrun the bag).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — SetAttributesWithPointFields. NOT HLSL-cbuffer 16B-row packed: this is a flat
// host↔shader struct where BOTH sides use a 12-byte float3 (host float[3] / MSL packed_float3) for
// OrientationUpVector with NO inserted padding — so the host C layout and the MSL layout agree
// byte-for-byte. Sequence: 4 floats (16) + float[3]+float (16) + 4 floats (16) + 4 floats (16) +
// 2 uints + 2 pad floats (16) = 80 bytes. The kernel reads each field by name (setBytes), no cbuffer
// register alignment needed.
struct SetAttrWithFieldsParams {
  // --- b0 row 0 (16B) ---
  float Amount;                 // hlsl b0 (:23)
  float Range;                  // b0 (:24)
  float OffsetRange;            // b0 (:25)
  float AffectPosition;         // b0 (:26)
  // --- b0 row 1 (16B): float3 + trailing float pack into one 16B HLSL row ---
#ifdef __METAL_VERSION__
  packed_float3 OrientationUpVector;  // b0 (:28) — packed so the host float3 matches HLSL float3 pack
#else
  float OrientationUpVector[3];
#endif
  float AffectOrientation;      // b0 (:29) — fills the 4th slot of the float3 row
  // --- b0 row 2 (16B) ---
  float AffectW;                // b0 (:31)
  float AffectColor;            // b0 (:32)
  float GainAndBiasX;           // b0 (:33) float2 GainAndBias .x (TiXL BiasAndGain.x default 0.365)
  float GainAndBiasY;           // b0 (:33) float2 GainAndBias .y (default 0.59)
  // --- b0 row 3 head + b1 (16B) ---
  float Variation;              // b0 (:34)
  float ColorMode;              // b1 (:40) — int enum carried as float (0 Add/1 Average/2 Blend)
  float WMode;                  // b1 (:41) — int enum carried as float (0 Set/1 Add/2 Blend)
  float WCurveAffectsWeight;    // b1 (:42) — bool→float (0/1)
  // --- counts (16B) ---
#ifdef __METAL_VERSION__
  uint Count;                   // SourcePoints (inputs[0]) count — OUR guard (not in .hlsl cbuffer)
  uint FieldCount;              // FieldPoints (inputs[1]) count — b1 FieldCount (:39)
#else
  uint32_t Count;
  uint32_t FieldCount;
#endif
  float _pad0, _pad1;           // -> 80 (pad to 16-byte row)
};

enum SetAttrWithFieldsBinding {
  SAWF_SourcePoints = 0,  // const device SwPoint* (t0)
  SAWF_FieldPoints  = 1,  // const device SwPoint* (t1)
  SAWF_ResultPoints = 2,  // device SwPoint*       (u0)
  SAWF_Params       = 3,  // constant SetAttrWithFieldsParams& (b0+b1 folded)
};
// Texture + sampler bind slots (separate spaces from buffers; mirror mappointattributes_params.h).
// CurveImage @t2, GradientImage @t3 in HLSL → texture(0)/texture(1) in MSL.
enum SetAttrWithFieldsTexBinding {
  SAWF_CurveImage    = 0,  // Texture2D<float4> CurveImage    (t2; texture(0) in MSL)
  SAWF_GradientImage = 1,  // Texture2D<float4> GradientImage (t3; texture(1) in MSL)
};
enum SetAttrWithFieldsSamplerBinding {
  SAWF_TexSampler = 0,  // sampler texSampler (s0; Clamp/Clamp + Linear per .t3)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SetAttrWithFieldsParams) == 80,
              "SetAttrWithFieldsParams must be 80 bytes (host float[3] + MSL packed_float3 agree)");
#endif
