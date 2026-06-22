// Shared host<->shader params for the TiXL-ported PointsOnMesh — the FIRST Points op that does an
// AREA-WEIGHTED barycentric surface scatter (one Point per output, NOT per vertex). Two kernels with
// two cbuffers, ported 1:1 from external/tixl:
//   .../Assets/shaders/points/_internal/PointsOnMesh-CalcCdf2.hlsl  (the serial CDF builder)
//   .../Assets/shaders/points/onmesh/DistributePointsOnMesh.hlsl     (the per-point sampler)
//
// Pass 1 (CalcCdf2, EmitParameter b0): float UseVertexSelection — when >0.5 the per-face weight is the
// sum of its three vertices' Selection (else 1). The kernel writes a scratch FaceProperties[faceCount]
// buffer (normalizedFaceArea + running cdf).
// Pass 2 (Distribute, EmitParameter b0): float Seed; float UseVertexSelection — Seed drives the wang_hash
// RNG (rng = i.x * (uint)(Seed*10317)); UseVertexSelection is bound but UNUSED by the distribute kernel
// (it consumed the already-baked CDF). Count is OUR addition (the .hlsl reads pointCount from the
// ResultPoints buffer dimensions; we pass it + guard i.x>=Count, like every sibling point param).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// FaceProperties — the scratch CDF buffer element (PointsOnMesh-CalcCdf2.hlsl:3-6 / DistributePointsOnMesh
// .hlsl:24-27). 8 bytes (2 floats). Shared host<->shader so the cook can size the scratch buffer.
struct PomFaceProperties {
  float normalizedFaceArea;
  float cdf;
};

// cbuffer EmitParameter (b0) for PointsOnMesh-CalcCdf2.hlsl:8-11. One float (the .hlsl cbuffer) + a
// test-only ForceUniformArea flag (NOT in the .hlsl; default 0 → byte-identical to TiXL) + pad to 16B.
// ForceUniformArea>0.5 makes every normalizedFaceArea=1 so the area-weighting golden's RED tooth bites
// CalcCdf2 (the points spread uniformly across faces instead of area-proportionally). Production keeps
// it 0 → no behavior change; the flag never appears as a node param.
struct PomCdfParams {
  float UseVertexSelection;
  float ForceUniformArea;  // TEST ONLY (golden RED tooth); 0 in production → faithful TiXL
  float _pad1, _pad2;      // -> 16
};

// cbuffer EmitParameter (b0) for DistributePointsOnMesh.hlsl:8-12 (+ our Count). Seed + UseVertexSelection
// are the .hlsl cbuffer; Count is OUR addition (.hlsl reads it from the buffer dims). Pad to 16B.
struct PomDistributeParams {
  float Seed;
  float UseVertexSelection;
#ifdef __METAL_VERSION__
  uint  Count;
#else
  uint32_t Count;
#endif
  float _pad0;  // -> 16
};

// CalcCdf2 binding slots (HLSL t0/t1/u0/b0 → MSL buffer indices).
enum PomCdfBinding {
  POM_CDF_Vertices    = 0,  // StructuredBuffer<PbrVertex> Vertices : t0  -> buffer(0)
  POM_CDF_FaceIndices = 1,  // StructuredBuffer<int3>      FaceIndices : t1 -> buffer(1)
  POM_CDF_FaceData    = 2,  // RWStructuredBuffer<FaceProperties> FaceData : u0 -> buffer(2)
  POM_CDF_Params      = 3,  // cbuffer EmitParameter : b0 -> buffer(3)
};

// Distribute binding slots (HLSL t0/t1/t2/t3/s0/u0/b0 → MSL buffer + texture + sampler indices).
enum PomDistributeBinding {
  POM_DIST_Vertices     = 0,  // StructuredBuffer<PbrVertex>      Vertices : t0    -> buffer(0)
  POM_DIST_FaceIndices  = 1,  // StructuredBuffer<int3>           FaceIndices : t1 -> buffer(1)
  POM_DIST_CDFs         = 2,  // StructuredBuffer<FaceProperties> CDFs : t2        -> buffer(2)
  POM_DIST_ResultPoints = 3,  // RWStructuredBuffer<LegacyPoint>  ResultPoints : u0 -> buffer(3)
  POM_DIST_Params       = 4,  // cbuffer EmitParameter : b0 -> buffer(4)
  POM_DIST_ColorMap     = 0,  // Texture2D<float4> ColorMap : t3 -> texture(0)
  POM_DIST_TexSampler   = 0,  // sampler texSampler : s0 -> sampler(0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(PomFaceProperties) == 8, "PomFaceProperties must be 8 bytes (TiXL FaceProperties)");
static_assert(sizeof(PomCdfParams) == 16, "PomCdfParams must be 16 bytes (cbuffer alignment)");
static_assert(sizeof(PomDistributeParams) == 16, "PomDistributeParams must be 16 bytes (cbuffer alignment)");
#endif
