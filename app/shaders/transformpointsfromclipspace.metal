// transformpointsfromclipspace — faithful 1:1 port of external/tixl
// .../Assets/shaders/points/modify/TransformPointsFromClipspace.hlsl. The FIRST Points op to consume
// the camera-matrix-into-points seam (PointCookCtx::cameraToWorld). One thread per point:
//   p.Position = (mul(float4(pos,1), CameraToWorld)).xyz / w        // clip-space → world unproject
//   p.Rotation = qMul( normalize(qFromMatrix3Precise(transpose(CameraToWorld 3×3))), p.Rotation )
//
// ★MATRIX CONVENTION (the draw_quad_xf.metal:33 / field_camera.h lock): CameraToWorld arrives ROW-MAJOR
// (m[r*4+c]); mul4row(M,v) == v·M_rowmajor reproduces HLSL `mul(rowVec, M)`. NO host transpose, NO
// column-major float4x4 reinterpret here.
//
// ★ROTATION CONVENTION (the meshverticestopoints.metal:50-53 / addnoise.metal proof, applied to a
// general 3×3): TiXL builds `orientationDest = float3x3(CameraToWorld._m00_m01_m02, _m10_m11_m12,
// _m20_m21_m22)` = the three ROWS of the 3×3 as HLSL float3x3 ROWS, then `transpose()`s it (→ columns =
// those rows) and feeds qFromMatrix3Precise. MSL `float3x3(r0,r1,r2)` already builds r0,r1,r2 as COLUMNS,
// so MSL `float3x3(camRow0,camRow1,camRow2)` ≡ HLSL `transpose(float3x3(rows r0,r1,r2))` — NO explicit
// transpose; that IS the form qFromMatrix3Precise expects. (Same identity the mesh op uses for its TBN
// basis; here the three vectors are the matrix's 3×3 rows instead of T/B/N.)
//
// NAMED FORK vs the .cs/.hlsl:
//   • fork-camera-default-only-v1 / fork-camera-one-matrix-per-op: the host computes ONLY CameraToWorld
//     (default camera, identity ObjectToWorld) and passes it; the other 9 TransformBufferLayout matrices
//     are dead for this kernel. v1 supports a bare point op (no Camera/Transform wrapper) only.
#include <metal_stdlib>
#include "tixl_point.h"                          // SwPoint (64B)
#include "transformpointsfromclipspace_params.h" // TpfcsParams + TPFCS_* bindings
#include "shared/quat.metal.h"                   // qMul + qFromMatrix3Precise (1:1 TiXL ports)
using namespace metal;

// mul4row(M_rowmajor, v) = v·M : (v·M)_j = Σ_i v_i · M[i*4+j]. Byte-identical to draw_quad_xf.metal's
// mul4row (the convention field_camera + the camera selftest pin).
static float4 mul4row(constant float M[16], float4 v) {
  float4 o;
  for (int j = 0; j < 4; ++j) {
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) s += v[i] * M[i * 4 + j];
    o[j] = s;
  }
  return o;
}

kernel void transformpointsfromclipspace(device const SwPoint*    src [[buffer(TPFCS_SourcePoints)]],
                                         device SwPoint*           dst [[buffer(TPFCS_ResultPoints)]],
                                         constant TpfcsParams&     P   [[buffer(TPFCS_Params)]],
                                         uint3                     tid [[thread_position_in_grid]]) {
  uint i = tid.x;
  if (i >= P.Count) return;
  SwPoint p = src[i];

  // Unproject: pInClipSpace = mul(float4(Position,1), CameraToWorld); /= w; w = 1 (TiXL :39-42).
  float3 pos = float3(p.Position.x, p.Position.y, p.Position.z);
  float4 pInWorld = mul4row(P.CameraToWorld, float4(pos, 1.0f));
  pInWorld.xyz /= pInWorld.w;
  p.Position = packed_float3(pInWorld.x, pInWorld.y, pInWorld.z);

  // Rotation: HLSL transpose(float3x3(rows m00.., m10.., m20..)) ≡ MSL float3x3(camRow0,camRow1,camRow2)
  // (columns = those rows) — see the convention note above. The 3×3 ROWS of the row-major CameraToWorld:
  float3 camRow0 = float3(P.CameraToWorld[0], P.CameraToWorld[1], P.CameraToWorld[2]);
  float3 camRow1 = float3(P.CameraToWorld[4], P.CameraToWorld[5], P.CameraToWorld[6]);
  float3 camRow2 = float3(P.CameraToWorld[8], P.CameraToWorld[9], P.CameraToWorld[10]);
  float3x3 orientDestT = float3x3(camRow0, camRow1, camRow2);  // == HLSL transpose(orientationDest)
  float4 newRotation = normalize(qFromMatrix3Precise(orientDestT));
  p.Rotation = qMul(newRotation, p.Rotation);  // TiXL :52 qMul(newRotation, p.Rotation)

  dst[i] = p;
}
