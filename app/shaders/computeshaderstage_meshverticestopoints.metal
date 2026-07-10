// computeshaderstage_meshverticestopoints — the ABI-repacked kernel that lets the GENERIC
// ComputeShaderStage atom drive MeshVerticesToPoints after the flat atom retires.
//
// This is NOT the fused-struct meshverticestopoints.metal (which binds a hand-assembled
// MeshVtxToPointsParams struct at fixed MVTP_* indices). It is the SAME faithful MSL port of
// external/tixl/Operators/Lib/Assets/shaders/points/generate/MeshVerticesToPoints.hlsl, repacked onto
// the GENERIC ComputeShaderStage ABI so it is driven exactly as TiXL drives MeshVerticesToPoints.t3:
// t0 = the gathered Mesh's vertex SRV (via _MeshBufferComponents -> GetBufferComponents ->
// ComputeShaderStage.ShaderResources), u0 = the fresh StructuredBufferWithViews (Stride=64, one
// SwPoint per vertex — TiXL's native Point stride already matches sw's SwPoint, NO stride fork needed
// here, unlike the mesh-OUTPUT ops' PbrVertex 64->80 allocation fork), b0 = FloatsToBuffer's
// [OffsetByTBN.x, OffsetByTBN.y, OffsetByTBN.z, W] (4 floats, wire order X,Y,Z,W — MeshVerticesToPoints.t3
// Connections feed Vector3Components' three outputs then the boundary W input into FloatsToBuffer.Params
// in that order).
//
// ── HLSL cbuffer byte layout (what FloatsToBuffer writes, tightly packed float[]) ────────────────
//   b0 (FloatsToBuffer, MeshVerticesToPoints.hlsl:6-10):
//        [0]=OffsetByTBN.x [1]=OffsetByTBN.y [2]=OffsetByTBN.z [3]=OffsetScale(the .t3 "W" input)
//
// ── Dispatch bound ──────────────────────────────────────────────────────────────────────────────
// The generic cook (buffer_ops_computeshaderstage.cpp) sizes the dispatch from the FRONT SRV's
// elementCount when an SRV is wired (this kernel's t0 = the mesh vertex SRV) — exactly the
// countFromMeshVtx policy the flat op's registerPointOp used (one Point per mesh vertex).
//
// ── NAMED FORKS (preserved from meshverticestopoints.metal) ────────────────────────────────────
//   • OffsetByTBN/OffsetScale default (0,0,0)/1.0 -> Position = vertex pos (offset terms vanish).
//   • Rotation matrix convention (the addnoise.metal:91-96 proof): TiXL HLSL `float3x3(T,B,N)` builds
//     the basis as ROWS, then transpose() flips it to basis-as-COLUMNS. MSL `float3x3(T,B,N)` already
//     builds basis-as-COLUMNS. So HLSL `transpose(float3x3(T,B,N))` == MSL `float3x3(T,B,N)` with NO
//     transpose — exactly the form qFromMatrix3Precise expects. Single normalize() (not double) —
//     matches the fused kernel's proven form (R6 mathv already validated this against the ref's
//     double-normalize; a unit quaternion renormalized is itself up to float rounding).
//   • FX1/FX2 <- Selection: the .hlsl writes both from `v.Selected` (same lone-float @64 field).
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B output)
#include "sw_mesh.h"                        // SwVertex (80B input) — MSL-shareable (packed_float3)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
#include "shared/quat.metal.h"              // qFromMatrix3Precise (1:1 TiXL port)
using namespace metal;

kernel void computeshaderstage_meshverticestopoints(
    const device SwVertex* SourceVerts [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwPoint*        ResultPoints [[buffer(CS_UAV_BASE + 0)]],  // u0
    constant float*        cb0         [[buffer(CS_CB_BASE + 0)]],    // b0: [OffsetByTBN.xyz, OffsetScale]
    constant uint&         numStructs  [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (SourceVerts count)
    uint3 i [[thread_position_in_grid]])
{
  if (i.x >= numStructs) return;
  SwVertex v = SourceVerts[i.x];

  const float offsetByTbnX = cb0[0];
  const float offsetByTbnY = cb0[1];
  const float offsetByTbnZ = cb0[2];
  const float offsetScale  = cb0[3];

  // packed_float3 members -> promote to float3 for math.
  float3 position  = float3(v.Position.x, v.Position.y, v.Position.z);
  float3 tangent   = float3(v.Tangent.x, v.Tangent.y, v.Tangent.z);
  float3 bitangent = float3(v.Bitangent.x, v.Bitangent.y, v.Bitangent.z);
  float3 normal    = float3(v.Normal.x, v.Normal.y, v.Normal.z);
  float3 colorRgb  = float3(v.ColorRgb.x, v.ColorRgb.y, v.ColorRgb.z);

  // Position (MeshVerticesToPoints.hlsl VERBATIM): vertex pos + per-axis TBN offset * OffsetScale.
  float3 outPos = position
                + offsetByTbnX * tangent   * offsetScale
                + offsetByTbnY * bitangent * offsetScale
                + offsetByTbnZ * normal    * offsetScale;

  // Rotation: HLSL transpose(float3x3(T,B,N)) == MSL float3x3(T,B,N) (columns = T,B,N) — see the named
  // fork above. NO explicit transpose; this IS the form the quat fn wants.
  float3x3 m = float3x3(tangent, bitangent, normal);  // MSL: columns = T,B,N = HLSL transpose(rows T,B,N)
  float4 rot = normalize(qFromMatrix3Precise(m));

  SwPoint p;
  p.Position = outPos;
  p.FX1      = v.Selection;            // .hlsl: ResultPoints.FX1 = v.Selected
  p.Rotation = rot;
  p.Color    = float4(colorRgb, 1.0f);  // .hlsl: float4(v.ColorRGB, 1)
  p.Scale    = float3(1.0f);            // .hlsl: Scale = 1 (float3 broadcast)
  p.FX2      = v.Selection;            // .hlsl: ResultPoints.FX2 = v.Selected
  ResultPoints[i.x] = p;
}
