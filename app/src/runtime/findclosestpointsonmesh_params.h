// Shared host<->shader params for the TiXL-ported FindClosestPointsOnMesh — a point-MODIFY op that
// snaps each input Point onto the nearest surface point of a mesh (brute-force per-point loop over the
// mesh triangles, NO BVH). 1:1 port of external/tixl
//   .../point/transform/FindClosestPointsOnMesh.cs   (thin shader op: Points + Mesh in -> Points out)
//   .../Assets/shaders/points/onmesh/FindClosestPointOnMesh.hlsl  (the brute-force kernel)
//
// .t3 audit (FindClosestPointsOnMesh.t3): the op has NO user-facing scalar params — only two inputs,
// Mesh (DefaultValue null) and Points (DefaultValue null). The .hlsl `cbuffer Params : register(b0)` is
// EMPTY. So this header carries ONLY our Count (TiXL reads pointCount from the Points buffer's
// GetDimensions; we pass it explicitly + guard i.x>=Count, like every sibling point op). Pad to 16B.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer-equivalent for the kernel: the .hlsl cbuffer is empty; Count is OUR addition (.hlsl reads it
// from the ResultPoints/Points buffer dims). Pad to 16B (cbuffer alignment, matches every sibling).
struct FcpomParams {
#ifdef __METAL_VERSION__
  uint  Count;
#else
  uint32_t Count;
#endif
  float _pad0, _pad1, _pad2;  // -> 16
};

// Binding slots (HLSL t0/t1/t2/u0/b0 -> MSL buffer indices). FaceCount passed at buffer(5) like the
// PointsOnMesh kernels (the .hlsl reads it via Indices.GetDimensions; we pass it + guard the loop).
enum FcpomBinding {
  FCPOM_Points       = 0,  // StructuredBuffer<LegacyPoint> Points : t0  -> buffer(0)  (input bag)
  FCPOM_Vertices     = 1,  // StructuredBuffer<PbrVertex>   Vertices : t1 -> buffer(1)  (mesh)
  FCPOM_Indices      = 2,  // StructuredBuffer<int3>        Indices : t2  -> buffer(2)  (mesh faces)
  FCPOM_ResultPoints = 3,  // RWStructuredBuffer<LegacyPoint> ResultPoints : u0 -> buffer(3)  (output)
  FCPOM_Params       = 4,  // cbuffer Params : b0 -> buffer(4)
  FCPOM_FaceCount    = 5,  // faceCount (== SwTriIndex count) -> buffer(5)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(FcpomParams) == 16, "FcpomParams must be 16 bytes (cbuffer alignment)");
#endif
