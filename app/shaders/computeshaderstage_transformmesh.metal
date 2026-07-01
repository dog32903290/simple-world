// computeshaderstage_transformmesh — the PROVING kernel for the ComputeShaderStage seam on the MESH
// currency (骨8: mesh family end-to-end .t3 replay). Parallel to computeshaderstage_transformpoints,
// but consumes/produces the 80-byte SwVertex (PbrVertex) instead of the 64-byte SwPoint.
//
// It is a faithful MSL port of external/tixl .../Resources/shaders/3d/mesh/mesh-TransformVertices.hlsl
// (the shader TransformMesh.t3's ComputeShader.Source names: "Lib:shaders/3d/mesh/mesh-TransformVertices.hlsl").
// The .hlsl is NOT in the Operators-only external/tixl checkout, so the VERBATIM math is taken from the
//焊死 oracle app/src/runtime/mesh_ops_transformmesh.cpp lines 16-20, which cites the .hlsl line-by-line:
//   s = UseVertexSelection>0.5 ? Selected : 1   (NGon/Quad set Selection=1 → s=1 → full transform)
//   Position  = lerp(pos,  (float4(pos,1)·M).xyz, s)
//   Normal    = lerp(n,    normalize((float4(n,0)·M).xyz), s)   (same for Tangent, Bitangent)
//   TexCoord/TexCoord2/Selected/ColorRGB copied verbatim.
//
// It is driven by the GENERIC ComputeShaderStage atom exactly as TiXL wires TransformMesh.t3:
//   - the SRV (t0) is the input mesh vertex buffer (SwVertex, stride 80),
//   - the UAV (u0) is the StructuredBufferWithViews write target (allocated stride*count = 80*N),
//   - the const buffer (b0) is what FloatsToBuffer assembles: 16 matrix floats (TransformMatrix.Result,
//     the transposed SRT — Vec4Params FIRST, FloatsToBuffer.cs:38-48) followed by 1 scalar float
//     (BoolToFloat(UseVertexSelection) — Params SECOND, FloatsToBuffer.cs:51-54). So cb0[0..15]=matrix,
//     cb0[16]=useVertexSelection. (TransformMesh.t3 wires only ONE ConstantBuffers → there is no cb1.)
//
// The matrix layout is IDENTICAL to the point kernel's (TransformMatrix.cs TRANSPOSES the SRT before the
// cbuffer write, "mem layout in hlsl constant buffer is row based"), so the 16 floats are m[r*4+c] with the
// TRANSLATION in the W COLUMN (m[3],m[7],m[11]) and the point transformed as out = M·[v;1]. This is HLSL
// mul(float4(v,1), TransformMatrix) under the transposed row-based layout — the SAME convention proven by
// computeshaderstage_transformpoints.metal (mulXform).
#include <metal_stdlib>
#include "sw_mesh.h"                        // SwVertex (80B) + SwTriIndex (12B)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
using namespace metal;

// Position: out = M·[v;1] (translation in the W column). = mul(float4(v,1), TransformMatrix).
static inline float3 mulXformPoint(float3 v, constant float* m) {
  return float3(m[0]*v.x + m[1]*v.y + m[2]*v.z  + m[3],
                m[4]*v.x + m[5]*v.y + m[6]*v.z  + m[7],
                m[8]*v.x + m[9]*v.y + m[10]*v.z + m[11]);
}
// Direction (w=0, no translation): out = M·[d;0]. = mul(float4(d,0), TransformMatrix).
static inline float3 mulXformDir(float3 v, constant float* m) {
  return float3(m[0]*v.x + m[1]*v.y + m[2]*v.z,
                m[4]*v.x + m[5]*v.y + m[6]*v.z,
                m[8]*v.x + m[9]*v.y + m[10]*v.z);
}

kernel void computeshaderstage_transformmesh(
    const device SwVertex* SourceVerts [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwVertex*       ResultVerts [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*        cb0         [[buffer(CS_CB_BASE + 0)]],    // b0: matrix(16) + useVertexSelection
    constant uint&         numStructs  [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (SourceVerts count)
    uint3 i [[thread_position_in_grid]]) {
  if (i.x >= numStructs) return;

  constant float* M = cb0;                 // TransformMatrix (16 floats, transposed: translation in W column)
  float useVertexSelection = cb0[16];      // 17th float (BoolToFloat(UseVertexSelection))

  SwVertex v = SourceVerts[i.x];
  float s = (useVertexSelection > 0.5f) ? v.Selection : 1.0f;

  // Position: lerp(pos, (float4(pos,1)·M).xyz, s).
  float3 pos = float3(v.Position);
  float3 tp = mulXformPoint(pos, M);
  v.Position = SW_MESH_PACKED3(mix(pos, tp, s));

  // Normal/Tangent/Bitangent: lerp(d, normalize((float4(d,0)·M).xyz), s).
  float3 n = float3(v.Normal);
  v.Normal = SW_MESH_PACKED3(mix(n, normalize(mulXformDir(n, M)), s));
  float3 t = float3(v.Tangent);
  v.Tangent = SW_MESH_PACKED3(mix(t, normalize(mulXformDir(t, M)), s));
  float3 b = float3(v.Bitangent);
  v.Bitangent = SW_MESH_PACKED3(mix(b, normalize(mulXformDir(b, M)), s));

  // Texcoord / Texcoord2 / Selection / ColorRgb copied verbatim (v carries them through).
  ResultVerts[i.x] = v;
}
