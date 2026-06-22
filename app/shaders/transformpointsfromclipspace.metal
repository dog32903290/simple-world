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
// ★ROTATION CONVENTION (cbuffer-transpose backward-trace — NOT the mesh-op analogy, which does NOT hold
// here; see WHY below): TiXL feeds `qFromMatrix3Precise(transpose(orientationDest))` where
// `orientationDest = float3x3(CameraToWorld._m00_m01_m02, _m10_m11_m12, _m20_m21_m22)` (the three ROWS of
// the HLSL CameraToWorld 3×3). The decisive fact the first port missed: TransformBufferLayout.cs uploads
// EVERY matrix already `Matrix4x4.Transpose`d ("mem layout in hlsl constant buffer is row based"), so HLSL
// `CameraToWorld._m{r}{c}` reads `cameraToWorld_numerics[c][r]`. SW stores the SAME numerics matrix
// ROW-MAJOR with NO cbuffer transpose, so `P.CameraToWorld[r*4+c] == cameraToWorld_numerics[r][c] ==
// HLSL CameraToWorld._m{c}{r}` (off by ONE transpose vs the GPU view). Threading that transpose through
// `transpose(orientationDest)` and the MSL `m[col][row]` reader (quat.metal.h: HLSL `_mRC == m[C][R]`)
// collapses to: the float3x3 MSL must receive is `float3x3(camCol0,camCol1,camCol2)` built from the
// matrix's 3×3 COLUMNS (P.CameraToWorld[0],[4],[8] / [1],[5],[9] / [2],[6],[10]) — NOT its rows.
//   ★WHY the mesh-op analogy is WRONG here: meshverticestopoints assembles its float3x3 from a
//   freshly-built T/B/N basis (no upstream cbuffer-transpose convention), so its `float3x3(rows)≡
//   transpose` shortcut is self-consistent. This matrix arrives from the fixed row-major TransformBuffer
//   layout, so the same shortcut double-counts a transpose and yields the rotation's CONJUGATE (the
//   inverse orientation). Anchored vs real TiXL at eye=(3,2,4): 抽column → (0.179,-0.311,-0.060,0.932)
//   (== TiXL GPU); 抽row → (-0.179,0.311,0.060,0.932) (== the conjugate). Default camera C2W≈identity so
//   conjugate==self → the bug hid until a non-axis-aligned camera leg bit it.
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

  // Rotation: feed qFromMatrix3Precise the matrix's 3×3 COLUMNS (see the cbuffer-transpose note above).
  // 抽column reproduces TiXL's GPU quaternion exactly; 抽row would compute its conjugate (inverse).
  float3 camCol0 = float3(P.CameraToWorld[0], P.CameraToWorld[4], P.CameraToWorld[8]);
  float3 camCol1 = float3(P.CameraToWorld[1], P.CameraToWorld[5], P.CameraToWorld[9]);
  float3 camCol2 = float3(P.CameraToWorld[2], P.CameraToWorld[6], P.CameraToWorld[10]);
  float3x3 orientDest = float3x3(camCol0, camCol1, camCol2);
  float4 newRotation = normalize(qFromMatrix3Precise(orientDest));
  p.Rotation = qMul(newRotation, p.Rotation);  // TiXL :52 qMul(newRotation, p.Rotation)

  dst[i] = p;
}
