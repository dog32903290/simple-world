// meshverticestopoints — faithful 1:1 port of external/tixl
// .../Assets/shaders/points/generate/MeshVerticesToPoints.hlsl. The FIRST Points op with a MESH input
// (the proving op for the mesh-into-points seam). One thread per mesh VERTEX → one SwPoint:
//   Position = v.Position + OffsetByTBN.{x,y,z} · {Tangent,Bitangent,Normal} · OffsetScale
//   Rotation = normalize(qFromMatrix3Precise(transpose(float3x3(Tangent,Bitangent,Normal))))
//   Color    = float4(v.ColorRgb, 1)
//   FX1 = FX2 = v.Selection   (the .hlsl calls the field `Selected`; our SwVertex names it Selection)
//   Scale    = 1   (TiXL Point.Scale is a float3 — `= 1` broadcasts to (1,1,1); we write float3(1))
//
// ★MSL packing (metal-cpp-discipline): SwVertex comes from sw_mesh.h's __METAL_VERSION__ branch, which
// declares its vec3 members as packed_float3 (12-byte) — EXACTLY как DrawMeshUnlit's mesh_draw_unlit.metal.
// A native float3 (16-aligned) would corrupt the 80-byte stride and read garbage. SwPoint (tixl_point.h)
// is the 64-byte output point. qFromMatrix3Precise lives in shared/quat.metal.h (1:1 HLSL port).
//
// NAMED FORKS vs the .cs/.hlsl:
//   • OffsetByTBN/OffsetScale default (0,0,0)/1.0 → Position = vertex pos (offset terms vanish).
//   • Rotation matrix convention (the addnoise.metal:91-96 proof, applied here): TiXL HLSL
//     `float3x3(T,B,N)` builds the basis as ROWS, then transpose() flips it to basis-as-COLUMNS. MSL
//     `float3x3(T,B,N)` already builds basis-as-COLUMNS. So HLSL `transpose(float3x3(T,B,N))` ≡ MSL
//     `float3x3(T,B,N)` with NO transpose — that is exactly the form qFromMatrix3Precise expects.
//   • FX1/FX2 ← Selection: the .hlsl writes both from `v.Selected` (same lone-float @64 field).
#include <metal_stdlib>
#include "tixl_point.h"                 // SwPoint (64B output)
#include "sw_mesh.h"                    // SwVertex (80B input) — MSL-shareable (packed_float3)
#include "meshverticestopoints_params.h"  // MeshVtxToPointsParams + MVTP_* bindings
#include "shared/quat.metal.h"          // qFromMatrix3Precise (1:1 TiXL port)
using namespace metal;

kernel void meshverticestopoints(device const SwVertex*           verts  [[buffer(MVTP_Vertices)]],
                                 device SwPoint*                   pts    [[buffer(MVTP_ResultPoints)]],
                                 constant MeshVtxToPointsParams&   P      [[buffer(MVTP_Params)]],
                                 uint3                             tid    [[thread_position_in_grid]]) {
  uint index = tid.x;
  if (index >= P.Count) return;
  SwVertex v = verts[index];

  // packed_float3 members → promote to float3 for math.
  float3 position  = float3(v.Position.x, v.Position.y, v.Position.z);
  float3 tangent   = float3(v.Tangent.x, v.Tangent.y, v.Tangent.z);
  float3 bitangent = float3(v.Bitangent.x, v.Bitangent.y, v.Bitangent.z);
  float3 normal    = float3(v.Normal.x, v.Normal.y, v.Normal.z);
  float3 colorRgb  = float3(v.ColorRgb.x, v.ColorRgb.y, v.ColorRgb.z);

  // Position (mesh-DrawUnlit.hlsl:20 VERBATIM): vertex pos + per-axis TBN offset · OffsetScale.
  float3 outPos = position
                + P.OffsetByTbnX * tangent   * P.OffsetScale
                + P.OffsetByTbnY * bitangent * P.OffsetScale
                + P.OffsetByTbnZ * normal    * P.OffsetScale;

  // Rotation: HLSL transpose(float3x3(T,B,N)) ≡ MSL float3x3(T,B,N) (columns = T,B,N) — see the named
  // fork above (addnoise.metal:91-96 convention). NO explicit transpose; this IS the form the quat fn wants.
  float3x3 m = float3x3(tangent, bitangent, normal);  // MSL: columns = T,B,N = HLSL transpose(rows T,B,N)
  float4 rot = normalize(qFromMatrix3Precise(m));

  SwPoint p;
  p.Position = outPos;
  p.FX1      = v.Selection;            // .hlsl: ResultPoints.FX1 = v.Selected
  p.Rotation = rot;
  p.Color    = float4(colorRgb, 1.0f);  // .hlsl: float4(v.ColorRGB, 1)
  p.Scale    = float3(1.0f);            // .hlsl: Scale = 1 (float3 broadcast)
  p.FX2      = v.Selection;            // .hlsl: ResultPoints.FX2 = v.Selected
  pts[index] = p;
}
