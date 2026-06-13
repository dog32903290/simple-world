// Shared host<->shader params for the TiXL-ported PolarTransformPoints MODIFIER (lane P, batch 16).
// Mirrors external/tixl .../point/transform/PolarTransformPoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/PolarTransformPoints.hlsl (.hlsl math).
// A count-preserving MODIFIER: first applies a TRS transform to each point's position, then maps
// the result through a cartesian->cylindrical (or ->spherical) polar warp, and rotates the point.
// Count is INHERITED from upstream.
//
// TiXL pipeline (PolarTransformPoints.hlsl):
//   pos = mul(float4(pos,1), TransformMatrix);   // TRS from Translation/Rotation/Scale·UniformScale
//   ... polar warp depending on Mode (b0 float Mode) ...
//   p.Rotation = composed with rotYAxis (+ rotXAxis in spherical mode)
//
// The TransformMatrix child is built host-side in TiXL from Translation/Rotation/Scale +
// UniformScale; PolarTransformPoints exposes NO Pivot/Shear/Invert ports, so those stay at TiXL
// defaults (pivot=0, shear=0, invert=false). NAMED FORK (see .metal): we send the raw TRS scalars
// and compose the same matrix IN the shader (qRotate(pos*scale,R)+trans), exactly as the existing
// transformpoints.metal does — algebraically identical for pivot=0/shear=0, and it avoids a
// packed_float4x4 host param (the particle_params.h discipline).
//
// All Vector3 inputs become X/Y/Z scalars; kernel reassembles them. 16-byte alignment via static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct PolarTransformParams {
#ifdef __METAL_VERSION__
  uint  Count;          // inherited from the input bag (modifier: count from upstream Points)
  int   Mode;           // Modes: 0=CartesianToCylindrical, 1=CartesianToSpherical
  float UniformScale;   // .cs UniformScale (Single), default 1.0 — multiplies Scale
  float _pad0;          // -> 16 bytes for the first row
#else
  uint32_t Count;
  int32_t  Mode;
  float    UniformScale;
  float    _pad0;
#endif
  float TranslationX, TranslationY, TranslationZ;  // .cs Translation (Vector3), default (0,0,0)
  float _pad1;                                     // -> 16 bytes
  float RotationX, RotationY, RotationZ;           // .cs Rotation (Vector3, Euler degrees), (0,0,0)
  float _pad2;                                     // -> 16 bytes
  float ScaleX, ScaleY, ScaleZ;                    // .cs Scale (Vector3, per-axis), default (1,1,1)
  float _pad3;                                     // -> 16 bytes
};

enum PolarTransformBinding {
  POLARXF_SourcePoints = 0,  // const device SwPoint* (t0)
  POLARXF_ResultPoints = 1,  // device SwPoint*       (u0)
  POLARXF_Params       = 2,  // constant PolarTransformParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+Mode(4)+UniformScale(4)+pad(4)=16 | Translation(12)+pad(4)=16 |
// Rotation(12)+pad(4)=16 | Scale(12)+pad(4)=16 = 64 bytes
static_assert(sizeof(PolarTransformParams) == 64, "PolarTransformParams must be 64 bytes (4x16)");
#endif
