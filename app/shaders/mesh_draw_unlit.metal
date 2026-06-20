// DrawMeshUnlit — the FIRST 3D mesh on screen (DrawKind::Mesh). TiXL authority:
// DrawMeshUnlit.t3 → Lib:shaders/3d/mesh/mesh-DrawUnlit.hlsl (vsMain + psMain non-cubemap default
// branch). Genuinely UNLIT in TiXL: psMain default = albedo·Color·vertexColor, and at the default
// inputs (no Texture → albedo=white, UseVertexColor=false → vertexColor=1, UseCubeMap=false,
// BlurLevel=0, AlphaCutOff=0) it collapses to `return Color`. NO ComputePbr / PointLights / IBL.
//
// VS (mesh-DrawUnlit.hlsl:46-65, VERBATIM index math): SV_VertexID-driven triangle list, NO Metal
// index buffer — the FaceIndices StructuredBuffer is read in the shader:
//   faceIndex = vid/3;  faceVertexIndex = vid%3;
//   vertex    = PbrVertices[ FaceIndices[faceIndex][faceVertexIndex] ];
//   position  = mul(float4(vertex.Position,1), ObjectToClipSpace);   // ROW-VECTOR v·M
// We draw meshIndexCount*3 vertices (3 per face); the host issues drawPrimitives(Triangle, 0, N*3).
//
// MATRIX CONVENTION (locked, runtime/field_camera.h): ObjectToClipSpace is host-packed ROW-MAJOR
// (m[r*4+c]); the VS does the row-vector multiply BY HAND (mul4row), byte-identical to
// draw_quad_xf.metal / field_raymarch_template's mul4 → mul4row(M,v)==v·M_rowmajor. NO transpose.
// applyTransform==0 (the drop-mul golden tooth) skips the mul (raw object position) so the render
// golden can prove the projection is load-bearing (Tooth A's mis-project bite).
//
// t2 white fork (named): the unlit default path has NO Texture wired (DrawMeshUnlit.t3 LoadImage
// white.png fallback), so albedo=white(1,1,1,1) and psMain = Color. We fold that in — the PS returns
// Color·vertexColor directly (no texture sample / bind), byte-identical to sampling a 1×1 white t2.
#include <metal_stdlib>
#include "mesh_draw_params.h"  // MeshDrawParams + MESH_* bindings
#include "sw_mesh.h"           // SwVertex (80B) / SwTriIndex (12B) — MSL-shareable (packed_float3)
using namespace metal;

struct MeshVSOut {
  float4 position [[position]];
  float3 colorRGB;   // vertex.ColorRgb (psMain UseVertexColor branch; unused at default)
};

// mul4row(M_rowmajor, v) = v·M for a ROW-MAJOR float[16]: (v·M)_j = Σ_i v_i · M[i*4+j].
// Byte-identical to draw_quad_xf.metal's mul4row (the convention --selftest-field-camera pins).
static float4 mul4row(constant float M[16], float4 v) {
  float4 o;
  for (int j = 0; j < 4; ++j) {
    float s = 0.0;
    for (int i = 0; i < 4; ++i) s += v[i] * M[i * 4 + j];
    o[j] = s;
  }
  return o;
}

vertex MeshVSOut mesh_draw_unlit_vs(uint vid [[vertex_id]],
                                    device const SwVertex* PbrVertices [[buffer(MESH_PbrVertices)]],
                                    device const SwTriIndex* FaceIndices [[buffer(MESH_FaceIndices)]],
                                    constant MeshDrawParams& P [[buffer(MESH_Params)]]) {
  MeshVSOut o;
  // mesh-DrawUnlit.hlsl:50-53 VERBATIM. FaceIndices[faceIndex][faceVertexIndex] = the X/Y/Z member of
  // the SwTriIndex (Int3) by faceVertexIndex 0/1/2.
  int faceIndex = (int)vid / 3;
  int faceVertexIndex = (int)vid % 3;
  SwTriIndex tri = FaceIndices[faceIndex];
  int vIndex = (faceVertexIndex == 0) ? tri.X : (faceVertexIndex == 1) ? tri.Y : tri.Z;
  SwVertex vtx = PbrVertices[vIndex];  // NOT `vertex` — that is an MSL reserved function qualifier

  float4 posInObject = float4(vtx.Position.x, vtx.Position.y, vtx.Position.z, 1.0f);
  // The ONE projection line (mesh-DrawUnlit.hlsl:57). applyTransform==0 = drop-mul tooth (raw object).
  o.position = (P.applyTransform != 0u) ? mul4row(P.objectToClipSpace, posInObject) : posInObject;
  o.colorRGB = float3(vtx.ColorRgb.x, vtx.ColorRgb.y, vtx.ColorRgb.z);
  return o;
}

// psMain non-cubemap DEFAULT branch (mesh-DrawUnlit.hlsl:83-108): albedo=white (no Texture wired),
// vertexColor=1 (UseVertexColor=false default), AlphaCutOff=0 → return albedo·Color·vertexColor = Color.
fragment float4 mesh_draw_unlit_fs(MeshVSOut pin [[stage_in]],
                                   constant MeshDrawParams& P [[buffer(MESH_Params)]]) {
  float4 albedo = float4(1.0f, 1.0f, 1.0f, 1.0f);  // in-code white t2 (white.png fallback) — named fork
  float4 vertexColor = float4(1.0f, 1.0f, 1.0f, 1.0f);  // UseVertexColor=false default
  return albedo * P.color * vertexColor;
}
