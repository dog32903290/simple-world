// Shared host<->shader params for DrawMeshPbr (the LIT 3D mesh draw — DrawKind::MeshPbr). Parallels
// mesh_draw_params.h (the UNLIT sibling): one struct per shader cbuffer, 16-byte aligned, included by
// BOTH the .metal and the executor .cpp so the compiler proves the layout byte-for-byte.
//
// TiXL authority: DrawMesh.t3 → mesh-Draw.hlsl. The FIVE cbuffers that shader reads (Transforms b0,
// Params b1, FogParams b2, PointLights b3, PbrParams b4) are COLLAPSED here to the minimal-viable subset
// the direct-lighting Cook-Torrance path reads (pbr.hlsl:47-71 + the fog/emissive tail pbr-render.hlsl:
// 106-110). The IBL/ambient half (pbr-render.hlsl:74-103) needs cubemap + BRDF-LUT textures → the
// NEXT-BATTLE fork (no fields carried). Texture maps (albedo/normal/RSMO/emissive) → also deferred; the
// no-map defaults fold in (albedo=white, N=vertex normal, RSMO=0, emissive-map=1) exactly like DrawMeshUnlit
// folds its white t2. So this cbuffer carries: the transform, the vertex Color tint, the material params,
// the fog params, and up to kMaxPbrLights point lights.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// Must match sw_pbr_scope.h kMaxPbrLights (mesh-Draw.hlsl b3 `PointLight Lights[8]`). Kept as a literal
// here (this header is MSL-shareable and must not pull the host-only scope header).
#define MESH_PBR_MAX_LIGHTS 8

// One point light, packed to match TiXL's PointLightStack.cs PointLight (48 bytes) AND sw_point_light.h.
// GPU layout: position float3 @0 | intensity float @12 | color float4 @16 | range float @32 | decay float @36
// | __padding float2 @40. In MSL we use packed_float3 for the tight 12-byte position so the 48-byte stride
// is byte-identical to the host SwPointLight (no float3 16-byte-align corruption — the metal-cpp-discipline
// trap). The host side memcpy's SwPointLight straight in (same field order/offsets).
struct MeshPbrLight {
#ifdef __METAL_VERSION__
  packed_float3 position;  // @0
  float intensity;         // @12
  float4 color;            // @16
  float range;             // @32
  float decay;             // @36
  float2 _pad;             // @40 -> 48
#else
  float position[3];       // @0
  float intensity;         // @12
  float color[4];          // @16
  float range;             // @32
  float decay;             // @36
  float _pad[2];           // @40 -> 48
#endif
};

// MeshPbrParams cbuffer. Field order chosen for 16-byte alignment (float4/float2-of-16 blocks first, then
// the matrix, then the lights array, then the scalar tail padded to 16).
//   objectToClipSpace  — composed ObjectToWorld·WorldToCamera·CameraToClipSpace (executor builds it, like
//                        Layer2d/Mesh). ROW-MAJOR m[r*4+c]; the VS does v·M by hand (mul4row). @0 (64B)
//   objectToCamera     — for the fog VS: posInCamera.z drives fog (mesh-Draw.hlsl:120-121). ROW-MAJOR. @64 (64B)
//   cameraToWorld      — for the eye position: eyePos = (0,0,0,1)·CameraToWorld (mesh-Draw.hlsl:227). @128 (64B)
//   color              — vertex Color tint (DrawMesh has no explicit Color input in the minimal path; the
//                        litColor *= Color; at pbr-render.hlsl:106 uses the .t3 default white). @192 (16B)
//   baseColor          — PbrParams.BaseColor  (b4; SwPbrParameters @0). @208 (16B)
//   emissiveColor      — PbrParams.EmissiveColor (b4; @16). @224 (16B)
//   fogColor           — FogParams.Color (b2). @240 (16B)
//   roughness/specular/metal — PbrParams scalars (@32/@36/@40). @256
//   fogDistance/fogBias      — FogParams scalars.
//   activeLightCount   — b3 ActiveLightCount.
//   applyTransform     — drop-mul golden tooth (0 → raw object pos; mis-projects → RED). scalar tail @256 (16B)
//   lights[8]          — b3 Lights[] (48B each = 384B). @272 -> 656
struct MeshPbrParams {
#ifdef __METAL_VERSION__
  float objectToClipSpace[16];  // @0
  float objectToCamera[16];     // @64
  float cameraToWorld[16];      // @128
  float4 color;                 // @192
  float4 baseColor;             // @208
  float4 emissiveColor;         // @224
  float4 fogColor;              // @240
  float roughness;              // @256
  float specular;               // @260
  float metal;                  // @264
  float fogDistance;            // @268
  float fogBias;                // @272
  uint  activeLightCount;       // @276
  uint  applyTransform;         // @280
  uint  _pad0;                  // @284 -> 288 (16-byte aligned before the lights array)
  MeshPbrLight lights[MESH_PBR_MAX_LIGHTS];  // @288 -> 288 + 8*48 = 672
#else
  float objectToClipSpace[16];
  float objectToCamera[16];
  float cameraToWorld[16];
  float color[4];
  float baseColor[4];
  float emissiveColor[4];
  float fogColor[4];
  float roughness;
  float specular;
  float metal;
  float fogDistance;
  float fogBias;
  uint32_t activeLightCount;
  uint32_t applyTransform;
  uint32_t _pad0;
  MeshPbrLight lights[MESH_PBR_MAX_LIGHTS];
#endif
};

// Bindings (mirror MeshDrawBinding). The VS reads the two StructuredBuffers (t0 PbrVertices / t1 FaceIndices)
// + Params; the PS reads only Params (the no-texture default path folds all texture samples to constants).
enum MeshPbrBinding {
  MESHPBR_PbrVertices = 0,  // device const SwVertex*   (HLSL t0)
  MESHPBR_FaceIndices = 1,  // device const SwTriIndex* (HLSL t1)
  MESHPBR_Params      = 2,  // constant MeshPbrParams&  (vertex + fragment)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(MeshPbrLight) == 48, "MeshPbrLight must be 48B (TiXL PointLight stride)");
static_assert(sizeof(MeshPbrParams) == 288 + 8 * 48, "MeshPbrParams layout (672 bytes, 16-byte multiple)");
#endif
