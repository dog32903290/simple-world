// Shared host<->shader params for DrawMeshUnlit (the FIRST 3D mesh draw — DrawKind::Mesh). Parallels
// draw_params.h: one struct per shader cbuffer, 16-byte aligned, included by both the .metal and the
// executor .cpp so the compiler proves the layout. TiXL authority: DrawMeshUnlit.t3 → mesh-DrawUnlit.hlsl
// (Transforms b0 + Params b1, COLLAPSED here to the ONLY fields the unlit default path reads).
//
// vsMain reads ObjectToClipSpace (mesh-DrawUnlit.hlsl:57); the other 9 TransformBufferLayout matrices
// are DEAD for the unlit VS (F3, same as Layer2d's DrawQuadXfParams). psMain default branch (no Texture,
// UseVertexColor=false, UseCubeMap=false, BlurLevel=0, AlphaCutOff=0) = albedo(white)·Color·1 = Color,
// so the cbuffer carries only Color. ROW-MAJOR float[16] (m[r*4+c]); the VS does the row-vector multiply
// by hand (mul4row), identical to draw_quad_xf.metal / field_raymarch's mul4 → mul4row(M,v)==v·M_rowmajor.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// MeshDrawParams cbuffer. color = TiXL DrawMeshUnlit.Color (.t3 default (1,1,1,1)); objectToClipSpace =
// the composed ObjectToWorld·WorldToCamera·CameraToClipSpace (executor builds it from the camera, like
// Layer2d). applyTransform = the drop-mul golden tooth (0 → raw object position, mis-projects → RED).
// 16 (color) + 64 (mat) + 16 (applyTransform+pad) = 96 bytes (16-byte multiple).
struct MeshDrawParams {
#ifdef __METAL_VERSION__
  float4 color;
  float objectToClipSpace[16];  // row-major m[r*4+c] (NOT a float4x4 — avoids MSL column-major reinterpret)
  uint applyTransform;          // 1 = apply the mul; 0 = raw object pos (drop-mul golden tooth)
  uint _pad0;
  uint _pad1;
  uint _pad2;  // -> 16-byte tail
#else
  float color[4];
  float objectToClipSpace[16];
  uint32_t applyTransform;
  uint32_t _pad0;
  uint32_t _pad1;
  uint32_t _pad2;
#endif
};

// Bindings. The VS reads two StructuredBuffers (mesh-DrawUnlit.hlsl t0 PbrVertices / t1 FaceIndices)
// bound as MSL buffer slots, plus the Params cbuffer. The PS reads only Params (default branch). The
// in-code white t2 (no Texture input) is folded into the shader (returns Color directly) — no texture
// bind needed for the unlit default case (★named fork: byte-identical to white.png albedo=1).
enum MeshDrawBinding {
  MESH_PbrVertices = 0,  // device const SwVertex*   (HLSL t0)
  MESH_FaceIndices = 1,  // device const SwTriIndex* (HLSL t1)
  MESH_Params      = 2,  // constant MeshDrawParams& (vertex + fragment)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(MeshDrawParams) == 96, "MeshDrawParams 96 bytes (16-byte multiple)");
#endif
