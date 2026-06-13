// Shared host<->shader params for the TiXL-ported TransformSomePoints MODIFIER (lane P, batch 18).
// Mirrors external/tixl .../point/transform/TransformSomePoints (.cs ports, .hlsl math).
// A count-preserving MODIFIER: applies a TRS transform to each point, weighted by the point's
// W channel (selection weight) when WIsWeight > 0.5.  Count is INHERITED from upstream.
//
// TiXL pipeline (TransformSomePoints.hlsl):
//   pos = mul(float4(pos,1), TransformMatrix);      // TRS from Translation/Rotation/Scale
//   if(UpdateRotation) newRotation = extracted from TransformMatrix, composed with orgRot
//   if(WIsWeight) { pos = lerp by w; rotation = slerp by w; }
//   p.W = w * ScaleW + OffsetW;
//
// NAMED FORK: TransformMatrix (float4x4) is built host-side in TiXL via the TransformMatrix child
// operator (render/_/TransformMatrix.cs:30-39): CreateTransformationMatrix with Pivot=0, passing
// the result as a cbuffer float4x4.  We compose the TRS IN the shader from raw scalars (same fork
// as transformpoints.metal and polartransformpoints.metal) — algebraically identical for Pivot=0
// (the only supported Pivot on TransformSomePoints, which has no Pivot input port), and it avoids
// a packed float4x4 host param.  Euler order Y·X·Z (= CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z)),
// proven correct by refuter-T and refuter-P in batches 16-17.
// NOTE: TiXL has no Strength port (grep "Strength" in TransformSomePoints.cs = 0 hits).
// Per-point weighting is handled exclusively by WIsWeight × W channel (TiXL HLSL:125-130).
//
// BAKED / DEFERRED (flagged, not yet ported):
//   - Take / Skip / OnlyKeepTakes / RangeStart / LengthFactor / Scatter — selection range indexing;
//     complex sub-system (see TransformSomePoints.hlsl:50-84) deferred; all points are transformed.
//   - UpdateRotation — when false, rotation is left unchanged; BAKED to true (all rotations updated).
//   - ScaleW / OffsetW — W-channel post-processing; BAKED to ScaleW=1/OffsetW=0 (identity on W).
//   - TestParam — debug input, no HLSL effect; baked to 0.
//   - Space=WorldSpace (2) — TiXL has PointSpace(0)/ObjectSpace(1)/WorldSpace(2); WorldSpace requires
//     a view transform not in the current cook ctx.  Baked: Space>1 treated as ObjectSpace.
//
// All Vector3 inputs become X/Y/Z scalars; kernel reassembles them. 16-byte alignment via static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct TransformSomeParams {
#ifdef __METAL_VERSION__
  uint  Count;          // inherited from the input bag (modifier: count from upstream Points)
  int   Space;          // 0=PointSpace, 1=ObjectSpace (WorldSpace baked to ObjectSpace)
  float WIsWeight;      // >0.5 -> lerp pos and slerp rot by point.W (TiXL WIsWeight)
  float _pad_w;         // padding to keep Translation row at 16-byte boundary
#else
  uint32_t Count;
  int32_t  Space;
  float    WIsWeight;
  float    _pad_w;      // alignment padding (no Strength in TiXL TransformSomePoints.cs)
#endif
  float TranslationX, TranslationY, TranslationZ;  // .cs Translation (Vector3), default (0,0,0)
  float _pad0;                                     // -> 16 bytes
  float RotationX, RotationY, RotationZ;           // .cs Rotation (Vector3, Euler degrees), (0,0,0)
  float _pad1;                                     // -> 16 bytes
  float StretchX, StretchY, StretchZ;              // .cs Scale (per-axis stretch), default (1,1,1)
  float UniformScale;                              // .cs UniformScale (Single), default 1.0
};

enum TransformSomeBinding {
  XFSOME_SourcePoints = 0,  // const device SwPoint* (t0)
  XFSOME_ResultPoints = 1,  // device SwPoint*       (u0)
  XFSOME_Params       = 2,  // constant TransformSomeParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+Space(4)+WIsWeight(4)+_pad_w(4)=16 | Translation(12)+_pad0(4)=16 |
// Rotation(12)+_pad1(4)=16 | Stretch(12)+UniformScale(4)=16 = 64 bytes
static_assert(sizeof(TransformSomeParams) == 64, "TransformSomeParams must be 64 bytes (4x16)");
#endif
