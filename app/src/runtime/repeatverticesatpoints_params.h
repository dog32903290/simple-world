// Host<->shader params for TiXL mesh-RepeatVerticesAtPoints — mathv transpiler-batch kernel INVENTORY
// (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-2). NOT wired to node_registry / t3_import — a
// verified-kernel-in-stock entry; connecting it to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/mesh-RepeatVerticesAtPoints.hlsl — TWO HLSL
// cbuffers (b0 Stretch/Size/ApplyScale, b1 PointCount/ScaleFX/TexCoord2Factor), flattened into one
// host ABI struct (§10.2③ dual-cbuffer reconstruction). Owner: external/tixl/Operators/Lib/mesh/
// generate/RepeatMeshAtPoints.t3 (sibling of mesh-RepeatIndicesAtPoints.hlsl — the two together
// instance a source mesh at every point in a Points buffer; TARGET is a 2D grid, x=source vertex,
// y=point index).
//
// `PointsCount`/`VerticesCount` are NOT HLSL cbuffer fields — host-ABI-only (§10.2③/§10.5①, replace
// Points.GetDimensions(sourcePointCount,stride) / SourceVertices.GetDimensions(sourceVertexCount,
// stride)).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RepeatVerticesAtPointsParams {
#ifdef __METAL_VERSION__
  packed_float3 Stretch;         // b0 .hlsl:8 Stretch
  float         Size;             // b0 .hlsl:9 Size
  float         ApplyScale;       // b0 .hlsl:10 ApplyScale (truthy float, not bool)
  int           PointCount;       // b1 .hlsl:14 PointCount (declared, unread by main() — dead field,
                                   // kept for ABI-mirroring completeness, same as SimBlendTo precedent)
  int           ScaleFX;          // b1 .hlsl:15 ScaleFX (0/1/2 enum: 1=const/2=p.FX1/3=p.FX2... see .cpp)
  int           TexCoord2Factor;  // b1 .hlsl:16 TexCoord2Factor (same 0/1/2 enum shape)
  uint          PointsCount;      // host ABI: Points SRV element count
  uint          VerticesCount;    // host ABI: SourceVertices SRV element count
#else
  float    StretchX, StretchY, StretchZ;
  float    Size;
  float    ApplyScale;
  int32_t  PointCount;
  int32_t  ScaleFX;
  int32_t  TexCoord2Factor;
  uint32_t PointsCount;
  uint32_t VerticesCount;
#endif
};

enum RepeatVerticesAtPointsBinding {
  // Fresh, non-colliding bindings (the dual-cbuffer merge already deviates from any single raw-output
  // binding number, §10.2③) — SRVs first (declaration order), UAV, then the flattened Params.
  RVATP_Points          = 0,  // const device SwPoint*  (t1 in HLSL)
  RVATP_SourceVertices  = 1,  // const device SwVertex* (t0 in HLSL)
  RVATP_ResultVertices  = 2,  // device SwVertex*       (u0 in HLSL)
  RVATP_Params          = 3,  // constant RepeatVerticesAtPointsParams&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RepeatVerticesAtPointsParams) == 40, "RepeatVerticesAtPointsParams must be 40 bytes");
#endif
