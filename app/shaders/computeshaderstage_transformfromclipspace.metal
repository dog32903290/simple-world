// computeshaderstage_transformfromclipspace — the ABI-repacked kernel that lets the GENERIC
// ComputeShaderStage atom drive TransformFromClipSpace after the flat atom retires.
//
// This is NOT the fused-struct transformpointsfromclipspace.metal (which binds a hand-assembled
// TpfcsParams.CameraToWorld[16] the host fills directly from PointCookCtx::cameraToWorld). It is the
// SAME faithful MSL port of external/tixl/Operators/Lib/Assets/shaders/points/modify/
// TransformPointsFromClipspace.hlsl, repacked onto the GENERIC ComputeShaderStage ABI so it is driven
// exactly as TiXL drives TransformFromClipSpace.t3: t0 = the Points boundary SRV (via
// GetBufferComponents -> ComputeShaderStage.ShaderResources), u0 = the fresh StructuredBufferWithViews
// (Stride=64 = SwPoint, no fork), b.. = the wired ConstantBuffers (FloatsToBuffer + TransformsConstBuffer).
//
// ── ★ NAMED FORK `computestage-empty-floatstobuffer-reindex` (THE binding-slot risk — read this) ──
// TransformFromClipSpace.t3 wires TWO children onto ComputeShaderStage.ConstantBuffers, in this wire
// order: (1) FloatsToBuffer (b0 in TiXL's HLSL — `cbuffer Params : register(b0) {}`, but the .t3 wires
// ZERO scalar/matrix inputs into it — the HLSL Params cbuffer is declared EMPTY), then (2)
// TransformsConstBuffer (`cbuffer Transforms : register(b1)`, 640B / 10 matrices). buffer_ops_floatstobuffer.cpp
// early-returns on totalFloatCount==0 (.cs:28-29), leaving its output SwBuffer at the struct default
// (bytes=nullptr). buffer_ops_computeshaderstage.cpp's cook then SKIPS any wired buffer whose
// `->bytes` is null (`if (!b || !b->bytes) continue;`) BEFORE assigning cb#-slot order — so the empty
// FloatsToBuffer output never occupies a cb slot at all, and TransformsConstBuffer becomes cbs[0],
// landing at **CS_CB_BASE+0 (b0)**, NOT b1. This kernel therefore binds the Transforms buffer at
// CS_CB_BASE+0. (Contrast WrapPointPosition: its FloatsToBuffer wires 8 REAL scalars — non-empty,
// non-null — so it legitimately occupies cb0 and TransformsConstBuffer legitimately lands at cb1,
// which that kernel deliberately never reads.) Verified end-to-end by this op's ②parity cook-driven
// golden (t3import_transformfromclipspace_retire_golden.cpp) — a wrong-slot bind reads garbage/zero
// bytes and diverges from the mathv oracle immediately.
//
// ── Transforms cbuffer (640B, 10 transposed row-major float4x4, buffer_ops_transformsconstbuffer.cpp)
// Only CameraToWorld (matrix index 3, byte offset 192 = float offset 48) is read (TransformPointsFromClipspace
// .hlsl :14/:39/:47-49; the other 9 are dead for this kernel, still bound/sized as part of the 640B block).
// The buffer holds transpose(M_logical) stored row-major (fork `transformsconstbuffer-hlsl-rowmajor-bytes`,
// buffer_ops_transformsconstbuffer.cpp): raw[a*4+b] = M_logical(row=b,col=a). To recover M_logical(row,col)
// (the SAME row-major convention PointCookCtx::cameraToWorld / field_camera.h's Mat4 use, m[row*4+col]),
// read TRANSPOSED: M[row*4+col] = raw[col*4+row]. This un-transpose step is the ONLY thing that differs
// from transformpointsfromclipspace.metal's mul4row/camRow math below — everything past it is the SAME
// proven per-thread body (R6 mathv-verified), just fed the un-transposed M instead of a direct memcpy.
#include <metal_stdlib>
#include "tixl_point.h"                          // SwPoint (64B)
#include "computeshaderstage_params.h"           // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
#include "shared/quat.metal.h"                   // qMul + qFromMatrix3Precise (1:1 TiXL ports)
using namespace metal;

// mul4row(M_rowmajor, v) = v.M : (v.M)_j = sum_i v_i * M[i*4+j]. Byte-identical to
// transformpointsfromclipspace.metal's mul4row (the convention field_camera + the camera selftest pin).
static float4 mul4row(thread const float M[16], float4 v) {
  float4 o;
  for (int j = 0; j < 4; ++j) {
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) s += v[i] * M[i * 4 + j];
    o[j] = s;
  }
  return o;
}

kernel void computeshaderstage_transformfromclipspace(
    const device SwPoint* SourcePoints [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*       cb0         [[buffer(CS_CB_BASE + 0)]],     // b0: Transforms (640B) — SEE FORK ABOVE
    constant uint&        numStructs  [[buffer(CS_CB_BASE + 3)]],     // dispatch bound (SourcePoints count)
    uint3 i [[thread_position_in_grid]])
{
  if (i.x >= numStructs) return;

  // Un-transpose CameraToWorld (raw bytes at float offset 48..63) into row-major M[row*4+col].
  constant float* cw = cb0 + 48;
  float M[16];
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) M[r * 4 + c] = cw[c * 4 + r];

  SwPoint p = SourcePoints[i.x];

  // Unproject: pInClipSpace = mul(float4(Position,1), CameraToWorld); /= w (TiXL :39-42).
  float3 pos = float3(p.Position.x, p.Position.y, p.Position.z);
  float4 pInWorld = mul4row(M, float4(pos, 1.0f));
  pInWorld.xyz /= pInWorld.w;
  p.Position = packed_float3(pInWorld.x, pInWorld.y, pInWorld.z);

  // Rotation: HLSL feeds qFromMatrix3Precise(transpose(orientationDest)) where orientationDest =
  // float3x3(rows m00.., m10.., m20..) of the logical camera 3x3 R. MSL float3x3(v0,v1,v2) builds
  // v0,v1,v2 as COLUMNS, and MSL qFromMatrix3Precise reads m[col][row], so a float3x3 whose COLUMNS
  // are R's ROWS reproduces Shepperd(transpose(R)) exactly (transformpointsfromclipspace.metal's proven
  // ★ROTATION CONVENTION note). The 3x3 ROWS of the row-major CameraToWorld:
  float3 camRow0 = float3(M[0], M[1], M[2]);
  float3 camRow1 = float3(M[4], M[5], M[6]);
  float3 camRow2 = float3(M[8], M[9], M[10]);
  float3x3 orientDestT = float3x3(camRow0, camRow1, camRow2);  // == HLSL transpose(orientationDest)
  float4 newRotation = normalize(qFromMatrix3Precise(orientDestT));
  p.Rotation = qMul(newRotation, p.Rotation);  // TiXL :52 qMul(newRotation, p.Rotation)

  ResultPoints[i.x] = p;
}
