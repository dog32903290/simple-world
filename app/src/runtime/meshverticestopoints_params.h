// Shared host<->shader params for the TiXL-ported MeshVerticesToPoints — the FIRST Points op with a
// MESH INPUT (the proving op for the mesh-into-points seam, PointCookCtx::meshVtx). Mirrors the cbuffer
// of external/tixl .../Assets/shaders/points/generate/MeshVerticesToPoints.hlsl:6-10:
//
//   float3 OffsetByTBN;   // per-axis TBN offset weights (Tangent / Bitangent / Normal)
//   float  OffsetScale;   // global multiplier on the TBN offset
//
// .t3 routing (FloatsToBuffer fills the cbuffer in connection order): OffsetByTBN (Vector3 → 3 floats)
// THEN W (the 4th float) = OffsetScale. So the op's `W` input IS OffsetScale (default 1.0). With the
// .t3 default OffsetByTBN=(0,0,0), Position = vertex pos regardless of OffsetScale (the offset terms
// all vanish) — the proving golden runs at this default.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — MeshVerticesToPoints. Count is OUR addition (not in the .hlsl cbuffer): the TiXL
// kernel reads the vertex count via the buffer dimensions; we pass it as a param + guard index>=Count,
// exactly like the sibling point params (the dispatch is calcDispatchCount(vtxCount,tg) threadgroups →
// the index can overrun the vertex buffer).
struct MeshVtxToPointsParams {
#ifdef __METAL_VERSION__
  uint  Count;
#else
  uint32_t Count;
#endif
  float OffsetByTbnX, OffsetByTbnY, OffsetByTbnZ;  // -> 16. TiXL OffsetByTBN (Vector3), default (0,0,0)
  float OffsetScale, _pad0, _pad1, _pad2;          // -> 16. TiXL W input = OffsetScale, default 1.0
};

enum MeshVtxToPointsBinding {
  MVTP_Vertices     = 0,  // const device SwVertex* Vertices (t0 in HLSL; buffer(0) in MSL)
  MVTP_ResultPoints = 1,  // device SwPoint* ResultPoints   (u0 in HLSL; buffer(1) in MSL)
  MVTP_Params       = 2,  // constant MeshVtxToPointsParams& (b0; buffer(2) in MSL)
};

#ifndef __METAL_VERSION__
// 16 (Count+OffsetByTBN) + 16 (OffsetScale+pad) = 32 bytes.
static_assert(sizeof(MeshVtxToPointsParams) == 32, "MeshVtxToPointsParams must be 32 bytes (2x16)");
#endif
