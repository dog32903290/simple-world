// TiXL-faithful mesh currency: SwVertex (PbrVertex) + SwTriIndex (Int3). The binding
// spine of the 4th cook flow (MeshBuffers = vertex-buffer + index-buffer pair),
// parallel to tixl_point.h's SwPoint for the Points flow.
//
// Ported 1:1 from external/tixl Core/Rendering/PbrVertex.cs (80B / 20-float, explicit
// FieldOffset layout) and Core/DataTypes/Vector/Int3.cs (3×int32 = 12B).
//
// CRITICAL (metal-cpp-discipline): a mesh op consumes/produces a StructuredBuffer<PbrVertex>
// exactly like TiXL. HLSL `float3` inside a struct packs to 12 bytes; MSL `float3` /
// simd `float3` are 16-byte ALIGNED. PbrVertex's layout depends on tight 12-byte float3:
// the ★alignment trap is `Selection` — a lone float @64 immediately BEFORE `ColorRgb`
// (a float3) @68. With a 16-byte-aligned float3, ColorRgb would jump to @80 and the
// whole 80-byte stride silently corrupts (no crash, garbage GPU reads). We use the
// sw_packed3 trick (12-byte float3, same as tixl_point.h's SwPoint) for EVERY vec3
// member so the struct lays out byte-exactly like PbrVertex.cs, and static_assert
// EVERY field offset + sizeof==80 so the compiler proves it every build (Rule 1+4).
//
// Batch-1 (mesh-pipeline seam) is CPU-self-sufficient: the cook writes contents(),
// the golden reads back via memcpy — NO GPU draw/camera yet. The header is shareable
// with MSL (the __METAL_VERSION__ branch) for when a future Draw*Mesh op lands.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
  #define SW_MESH_PACKED3 packed_float3  // native 12-byte float3 in MSL
  #define SW_MESH_FLOAT2  float2
#else
  #include <cstddef>
  #include <cstdint>
  // Apple's host simd has no packed_float3 (MSL-only). Match MSL's 12-byte packed_float3
  // with an explicit 3-float struct so host & GPU strides agree (same as tixl_point.h).
  struct sw_mesh_packed3 { float x, y, z; };
  struct sw_mesh_float2  { float x, y; };
  #define SW_MESH_PACKED3 sw_mesh_packed3
  #define SW_MESH_FLOAT2  sw_mesh_float2
#endif

// One mesh vertex — TiXL's PbrVertex (Core/Rendering/PbrVertex.cs). 80 bytes / 20 floats.
// Field order + offsets are FORCED parity (PbrVertex.cs FieldOffset attributes):
//   Position@0 Normal@12 Tangent@24 Bitangent@36 Texcoord@48 Texcoord2@56 Selection@64 ColorRgb@68.
struct SwVertex {
  SW_MESH_PACKED3 Position;   // @0   (12 bytes)
  SW_MESH_PACKED3 Normal;     // @12  (12 bytes)
  SW_MESH_PACKED3 Tangent;    // @24  (12 bytes)
  SW_MESH_PACKED3 Bitangent;  // @36  (12 bytes)
  SW_MESH_FLOAT2  Texcoord;   // @48  (8 bytes)
  SW_MESH_FLOAT2  Texcoord2;  // @56  (8 bytes)
  float           Selection;  // @64  (4 bytes) — ★lone float before a float3 (the trap)
  SW_MESH_PACKED3 ColorRgb;   // @68  (12 bytes)
};                            // 80

// One triangle = 3 vertex indices. TiXL's Int3 (Core/DataTypes/Vector/Int3.cs): 3×int32,
// 12 bytes. TiXL's index stride is literally `3 * 4` (NGonMesh.cs:131 / QuadMesh.cs:117).
struct SwTriIndex {
  int32_t X, Y, Z;  // the three vertex indices of one face, in TiXL Int3(x,y,z) order
};                  // 12

#ifndef __METAL_VERSION__
// Prove the 80-byte PbrVertex layout byte-exactly (metal-cpp-discipline Rule 1+4): every
// offset matched against PbrVertex.cs FieldOffset, NOT just the total size. host==GPU stride.
static_assert(sizeof(SwVertex) == 80, "SwVertex must match TiXL PbrVertex 80-byte stride");
static_assert(offsetof(SwVertex, Position) == 0, "PbrVertex.Position @0");
static_assert(offsetof(SwVertex, Normal) == 12, "PbrVertex.Normal @12 (3*4)");
static_assert(offsetof(SwVertex, Tangent) == 24, "PbrVertex.Tangent @24 (6*4)");
static_assert(offsetof(SwVertex, Bitangent) == 36, "PbrVertex.Bitangent @36 (9*4)");
static_assert(offsetof(SwVertex, Texcoord) == 48, "PbrVertex.Texcoord @48 (12*4)");
static_assert(offsetof(SwVertex, Texcoord2) == 56, "PbrVertex.Texcoord2 @56 (14*4)");
static_assert(offsetof(SwVertex, Selection) == 64, "PbrVertex.Selection @64 (16*4) — lone float");
static_assert(offsetof(SwVertex, ColorRgb) == 68, "PbrVertex.ColorRgb @68 (17*4) — float3 right after the lone float");
static_assert(sizeof(SwTriIndex) == 12, "SwTriIndex must match TiXL Int3 (3*int32 = 12B), index stride 3*4");
static_assert(offsetof(SwTriIndex, Y) == 4, "Int3.Y @4");
static_assert(offsetof(SwTriIndex, Z) == 8, "Int3.Z @8");
#endif
