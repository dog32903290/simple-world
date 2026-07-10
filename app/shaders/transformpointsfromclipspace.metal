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
// ★ROTATION CONVENTION (pinned parity — the mesh-op identity DOES hold here; the "cbuffer-transpose
// backward-trace" that once argued for 抽column was WRONG, reversed by S's semantic audit): TiXL feeds
// `qFromMatrix3Precise(transpose(orientationDest))` where `orientationDest =
// float3x3(CameraToWorld._m00_m01_m02, _m10_m11_m12, _m20_m21_m22)` (the three ROWS of the HLSL
// CameraToWorld 3×3). The HLSL-visible matrix M ≡ the logical camera matrix N that sw stores row-major:
// TiXL compiles with NO row-major flag (DX11ShaderCompiler.cs:38 `ShaderFlags.None`) so its cbuffers pack
// COLUMN-MAJOR, which CANCELS the host's `Matrix4x4.Transpose` (TransformBufferLayout.cs:24) — the .cs
// comment "hlsl constant buffer is row based" is itself wrong. (Self-証: WrapPointPosition.hlsl:45 reads
// `CameraToWorld._m30_m31_m32` AS the camera world position — only true if M≡N.) So orientationDest == R
// (rows of N), and HLSL `transpose(orientationDest)` is a REAL transpose of the logical 3×3: the net
// Rotation semantics is Shepperd(transpose(R)). MSL float3x3(v0,v1,v2) builds v0,v1,v2 as COLUMNS and
// qFromMatrix3Precise reads m[col][row], so MSL `float3x3(camRow0,camRow1,camRow2)` (columns = R's rows)
// ≡ HLSL `transpose(float3x3(rows))` — the exact form qFromMatrix3Precise expects (SAME identity the mesh
// op uses for its TBN basis; here the three vectors are the matrix's 3×3 ROWS).
//   Anchored vs real TiXL at eye=(3,2,4): 抽row → (-0.179,0.311,0.060,0.932) (== TiXL; +X rotated onto
//   the camera right axis); 抽column → (0.179,-0.311,-0.060,0.932) (== the CONJUGATE / inverse orientation
//   — the bug S retired). Default camera C2W≈identity so conjugate==self → 抽column hid until a
//   non-axis-aligned camera leg bit it. Pinned by point_ops golden leg (2b) + the mathv conjugate tooth.
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

  // Rotation: HLSL feeds qFromMatrix3Precise(transpose(orientationDest)) where orientationDest =
  // float3x3(rows m00.., m10.., m20..) of the logical camera 3×3 R. MSL float3x3(v0,v1,v2) builds
  // v0,v1,v2 as COLUMNS, and MSL qFromMatrix3Precise reads m[col][row], so a float3x3 whose COLUMNS
  // are R's ROWS reproduces Shepperd(transpose(R)) exactly (see the ★ROTATION note above). The 3×3
  // ROWS of the row-major CameraToWorld:
  float3 camRow0 = float3(P.CameraToWorld[0], P.CameraToWorld[1], P.CameraToWorld[2]);
  float3 camRow1 = float3(P.CameraToWorld[4], P.CameraToWorld[5], P.CameraToWorld[6]);
  float3 camRow2 = float3(P.CameraToWorld[8], P.CameraToWorld[9], P.CameraToWorld[10]);
  float3x3 orientDestT = float3x3(camRow0, camRow1, camRow2);  // == HLSL transpose(orientationDest)
  float4 newRotation = normalize(qFromMatrix3Precise(orientDestT));
  p.Rotation = qMul(newRotation, p.Rotation);  // TiXL :52 qMul(newRotation, p.Rotation)

  dst[i] = p;
}
