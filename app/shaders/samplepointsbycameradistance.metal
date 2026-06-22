// samplepointsbycameradistance — faithful 1:1 port of external/tixl
// .../Assets/shaders/points/modify/SamplePointsByCameraDistance.hlsl. The SECOND camera-matrix-into-points
// seam consumer (reads ObjectToCamera) AND a rider of the bake-into-point seam (the WForDistance Curve
// baked to a 256×1 R32 texture @t1). One thread per point:
//   d          = mul(float4(pos,1), ObjectToCamera).z                    // camera-space depth (.z)
//   normalized = (-d - NearRange) / (FarRange - NearRange)
//   t          = curveTex.SampleLevel(texSampler, float2(normalized, 0.5), 0)
//   p.W       *= t.r                                                      // p.W == SwPoint.FX1 (@12)
//
// ★MATRIX CONVENTION (draw_quad_xf.metal:33 / field_camera.h lock): ObjectToCamera arrives ROW-MAJOR
// (m[r*4+c]); mul4row(M,v) == v·M_rowmajor reproduces HLSL `mul(rowVec, M)`. Since ObjectToCamera is
// affine (no perspective row), w stays 1 and the .z is the raw camera-space depth.
//
// ★p.W MAPPING (tixl_point.h / shared/point.hlsl): TiXL LegacyPoint.W lives at offset 12 == SwPoint.FX1
// (@12), the W-size field the renderer reads (draw_points2.metal:54 useWForSize ? pt.FX1 : 1). The kernel
// scales FX1, leaving Position/Rotation/Color/Scale/FX2 untouched. (NOTE: pointsonmesh.metal:11 mislabels
// W↔FX2 — harmless there since onmesh fills both with 1 — do NOT trust it as the W mapping authority.)
//
// NAMED FORK vs the .cs/.hlsl:
//   • fork-camera-one-matrix-per-op / fork-camera-default-only-v1: the host computes ONLY ObjectToCamera
//     (default camera, identity ObjectToWorld → ObjectToCamera = WorldToCamera); the other 9
//     TransformBufferLayout matrices are dead for this kernel.
//   • fork-wfordistance-embedded-default-curve: the host bakes the .t3 default linear-0→1 WForDistance
//     curve when the Curve input is unwired (always, in production — no Curve producer op yet).
#include <metal_stdlib>
#include "tixl_point.h"                        // SwPoint (64B); .FX1 (@12) == TiXL p.W
#include "samplepointsbycameradistance_params.h"  // SpcdParams + SPCD_* bindings
using namespace metal;

// mul4row(M_rowmajor, v) = v·M : (v·M)_j = Σ_i v_i · M[i*4+j]. Byte-identical to draw_quad_xf.metal's
// mul4row (the camera-selftest-pinned convention).
static float4 mul4row(constant float M[16], float4 v) {
  float4 o;
  for (int j = 0; j < 4; ++j) {
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) s += v[i] * M[i * 4 + j];
    o[j] = s;
  }
  return o;
}

kernel void samplepointsbycameradistance(device const SwPoint*  src       [[buffer(SPCD_SourcePoints)]],
                                         device SwPoint*        dst       [[buffer(SPCD_ResultPoints)]],
                                         constant SpcdParams&   P         [[buffer(SPCD_Params)]],
                                         texture2d<float>       curveTex  [[texture(SPCD_CurveTex)]],
                                         sampler                texSampler[[sampler(SPCD_TexSampler)]],
                                         uint3                  tid       [[thread_position_in_grid]]) {
  uint i = tid.x;
  if (i >= P.Count) return;
  SwPoint p = src[i];

  float3 pos = float3(p.Position.x, p.Position.y, p.Position.z);
  float4 distanceFromCamera = mul4row(P.ObjectToCamera, float4(pos, 1.0f));  // TiXL :50
  float d = distanceFromCamera.z;                                            // TiXL :51

  float normalized = (-d - P.NearRange) / (P.FarRange - P.NearRange);        // TiXL :53
  float4 t = curveTex.sample(texSampler, float2(normalized, 0.5f), level(0)); // TiXL :54 SampleLevel(...,0)
  p.FX1 *= t.r;                                                              // TiXL :55 p.W *= t.r (W==FX1)

  dst[i] = p;
}
