// computeshaderstage_transformpoints — the PROVING kernel for the generic ComputeShaderStage seam.
//
// This is NOT the fused scalar transformpoints.metal (which takes a hand-assembled TransformParams).
// It is a faithful MSL port of external/tixl .../Assets/shaders/points/modify/TransformPoints.hlsl
// with the SAME constant-buffer / SRV / UAV binding contract the .hlsl declares, so it can be driven
// by the GENERIC ComputeShaderStage atom exactly as TiXL drives it: the const buffers are the raw
// bytes FloatsToBuffer / IntsToBuffer assemble (matrix+strength at b0, space+strengthfactor at b1),
// the SRV is the input Point buffer (t0), the UAV is the output Point buffer (u0).
//
// HLSL cbuffer byte layout (what FloatsToBuffer/IntsToBuffer write, tightly packed float[]):
//   b0 : float4x4 TransformMatrix (16 floats, ROW-MAJOR — TransformMatrix.cs:63 transposes before
//        write "mem layout in hlsl constant buffer is row based") + float Strength (17th float).
//   b1 : int CoordinateSpace + int StrengthFactor (2 ints).
// We read them as raw float/int pointers so the byte layout is unambiguous (no MSL struct padding).
//
// The MATH is line-for-line the .hlsl (TransformPoints.hlsl:38-98): mul(float4(pos,1), M),
// ExtractScale, qFromMatrix3Precise(transpose(rotationMatrix)), space-dependent newRotation, Strength
// lerp/slerp. Reusing the existing transformpoints.metal math (scalar TRS) would NOT prove the
// generic const-buffer binding path — this kernel consumes the REAL assembled cbuffer bytes.
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
#include "shared/quat.metal.h"              // qMul, qRotateVec3, qSlerp, qFromMatrix3Precise
using namespace metal;

// Apply the transform. TransformMatrix.cs TRANSPOSES the SRT before writing the cbuffer, so the 16 floats
// are m[r*4+c] with the TRANSLATION in the W COLUMN (m[3], m[7], m[11]) and last row (0,0,0,1) — i.e. the
// point is transformed as out = M · [v;1] with M's rows being (m0..3),(m4..7),(m8..11):
//   out.x = m[0]*x + m[1]*y + m[2]*z + m[3];  (row0 · [v;1])   — matches the sw TransformMatrix golden
// (Scale=(2,3,4),T=(5,6,7) → Row1=(2,0,0,5): out.x = 2*x + 5). This is HLSL mul(float4(v,1), TransformMatrix)
// under TiXL's transposed row-based cbuffer layout (TransformMatrix.cs:62-63).
static inline float3 mulXform(float3 v, constant float* m) {
  return float3(m[0]*v.x + m[1]*v.y + m[2]*v.z  + m[3],
                m[4]*v.x + m[5]*v.y + m[6]*v.z  + m[7],
                m[8]*v.x + m[9]*v.y + m[10]*v.z + m[11]);
}

// ExtractScale: lengths of the 3 basis rows. With the transposed layout the basis vectors are the COLUMNS
// (m[0],m[4],m[8]) etc — the .hlsl's TransformMatrix._m00_m01_m02 (a column of the transposed = a basis).
static inline float3 extractScale(constant float* m) {
  return float3(length(float3(m[0], m[4], m[8])),
                length(float3(m[1], m[5], m[9])),
                length(float3(m[2], m[6], m[10])));
}

kernel void computeshaderstage_transformpoints(
    const device SwPoint* SourcePoints [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*       cb0          [[buffer(CS_CB_BASE + 0)]],    // b0: matrix(16) + Strength
    constant int*         cb1          [[buffer(CS_CB_BASE + 1)]],    // b1: Space + StrengthFactor
    constant uint&        numStructs   [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (SourcePoints count)
    uint3 i [[thread_position_in_grid]]) {
  if (i.x >= numStructs) return;

  constant float* M = cb0;              // TransformMatrix (16 floats, transposed: translation in W column)
  float Strength = cb0[16];             // 17th float
  int CoordinateSpace = cb1[0];
  int StrengthFactor = cb1[1];

  SwPoint p = SourcePoints[i.x];
  float3 pos = float3(p.Position);
  float4 orgRot = p.Rotation;
  float4 rotation = orgRot;

  if (CoordinateSpace < 0.5f) {         // PointSpace (.hlsl:51-55)
    pos = float3(0.0f);
    rotation = float4(0, 0, 0, 1);
  }

  pos = mulXform(pos, M);              // out = M·[pos;1] (= mul(float4(pos,1), TransformMatrix), .hlsl:58)

  float3 scale = extractScale(M);
  // Pure rotation R = linear-part with per-axis scale removed. The linear part L (L·v rotates+scales) has
  // rows (m0..2),(m4..6),(m8..10). Its COLUMNS are the scaled basis axes (|col j| = scale.j), so R's
  // columns = L's columns / scale.j. qFromMatrix3Precise wants a COLUMN-MAJOR float3x3 (its cols = basis);
  // MSL float3x3(c0,c1,c2) is column-major, so pass R's columns directly.
  float3x3 R3 = float3x3(float3(M[0], M[4], M[8])  / scale.x,   // column 0 (x-axis basis)
                         float3(M[1], M[5], M[9])  / scale.y,   // column 1 (y-axis basis)
                         float3(M[2], M[6], M[10]) / scale.z);  // column 2 (z-axis basis)
  float4 newRotation = normalize(qFromMatrix3Precise(R3));

  if (CoordinateSpace < 0.5f)          // PointSpace (.hlsl:73-80)
    newRotation = qMul(orgRot, newRotation);
  else
    newRotation = qMul(newRotation, orgRot);

  float strength = Strength * (StrengthFactor == 0 ? 1.0f
                              : (StrengthFactor == 1 ? p.FX1 : p.FX2));

  if (CoordinateSpace == 0) {          // PointSpace world-add (.hlsl:87-94)
    pos = qRotateVec3(pos, orgRot);
    pos += float3(p.Position);
    p.Scale = SW_PACKED3(float3(p.Scale) * mix(float3(1.0f), scale, strength));
  }

  p.Position = SW_PACKED3(mix(float3(p.Position), pos, strength));
  p.Rotation = qSlerp(p.Rotation, newRotation, strength);
  ResultPoints[i.x] = p;
}
