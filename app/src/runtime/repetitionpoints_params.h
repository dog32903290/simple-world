// Shared host<->shader params for the TiXL-ported RepetitionPoints GENERATOR (batch 36).
// Mirrors external/tixl .../point/generate/RepetitionPoints.cs — a CPU StructuredList
// generator (NO .hlsl in TiXL). We do a GPU generator NAMED FORK: no input buffer; thread i
// computes the i-th point by the exact per-point recipe in RepetitionPoints.cs Update().
//
//   u            = i + 1 + Phase
//   translation  = Translate * u + StartPosition
//   rotation     = Quaternion.CreateFromYawPitchRoll(Rotate.X/360*2pi*u,  // yaw  = Rotate.X
//                                                     Rotate.Y/360*2pi*u,  // pitch= Rotate.Y
//                                                     Rotate.Z/360*2pi*u)  // roll = Rotate.Z
//   scale        = (Vector3.One - Vector3(Scale)) * u + Vector3.One        // Scale is a float (broadcast)
//   transform    = GraphicsMath.CreateTransformationMatrix(0, identity, scale, Pivot, rotation, translation)
//   Position     = Vector4.Transform((0,0,0,1), transform).xyz
//   F1           = scale.Length() / sqrt(3) + StartW
//   Orientation  = rotation;  F2 = 1;  Color = white;  Scale-attr = (1,1,1)
//
// AddSeparator (default true): a Point.Separator() is appended at index count (Scale = NaN).
// The bag is sized count(+1 if AddSeparator) via a countTransform reading g_repAddSeparator
// (cook sets it; one-cook lag like PairPointsForLines — settled by warm-up cook in the golden).
//
// All-scalar layout (NO packed_float3) — the particle_params.h discipline. Every Vector3 is
// laid out as X/Y/Z scalars; the shader reassembles float3(...). Pad to a 16-byte multiple
// and static_assert the size so the cbuffer has zero alignment traps.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RepetitionPointsParams {
#ifdef __METAL_VERSION__
  uint  Count;        // clamped 1..10000 real point count (NOT incl. separator slot)
#else
  uint32_t Count;
#endif
  float Phase;        // TiXL Phase — added to (i+1)
  float Scale;        // TiXL Scale (float, broadcast to Vector3) — default 1.0
  float StartW;       // TiXL StartW — added to scale.Length()/sqrt(3) for F1

  float StartPosX, StartPosY, StartPosZ;  // TiXL StartPosition (Vector3)
  float _pad0;        // -> 16-byte boundary

  float TranslateX, TranslateY, TranslateZ;  // TiXL Translate (Vector3) — per-step translation
  float _pad1;

  float RotateX, RotateY, RotateZ;  // TiXL Rotate (Vector3, degrees/step): X=yaw Y=pitch Z=roll
  float _pad2;

  float PivotX, PivotY, PivotZ;  // TiXL Pivot (Vector3) — rotationCenter in CreateTransformationMatrix
  float AddSeparator;            // 0/1 — when 1, thread writes Point.Separator() at index Count
};

enum RepetitionPointsBinding {
  REPETITION_Points = 0,  // device SwPoint* (u0)
  REPETITION_Params = 1,  // constant RepetitionPointsParams& (b0)
};

#ifndef __METAL_VERSION__
// Count/Phase/Scale/StartW(16) | StartPos+pad(16) | Translate+pad(16) | Rotate+pad(16)
// | Pivot+AddSeparator(16) = 80 bytes
static_assert(sizeof(RepetitionPointsParams) == 80, "RepetitionPointsParams must be 80 bytes");
#endif
