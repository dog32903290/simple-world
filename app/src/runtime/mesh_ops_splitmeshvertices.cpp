// SplitMeshVertices mesh op (a mesh→mesh CONSUMER that CHANGES TOPOLOGY: it un-welds the mesh so every
// face owns its own 3 vertices = flat-shading prep). TiXL authority:
// external/tixl/Operators/Lib/mesh/modify/SplitMeshVertices.cs (one Mesh input + ShadeFlat float, one
// Mesh output) + its compute shader mesh-SplitVertices.hlsl (the verbatim per-FACE math).
//
// VERBATIM math (mesh-SplitVertices.hlsl, dispatched per FACE i.x = faceIndex):
//   vertexIndex = faceIndex*3
//   for each of the face's 3 source verts (indicesForFace.x/.y/.z) copy the whole PbrVertex into
//   ResultVertices[vertexIndex+0/1/2]   (positions/uv/selection/color all carried over)
//   ResultIndices[faceIndex] = int3(vertexIndex, vertexIndex+1, vertexIndex+2)  (sequential re-index)
//   FLAT-SHADING blend (ShadeFlat in [0,1]): a per-face averaged Normal/Tangent/Bitangent is computed
//   with a corner WEIGHT w that drops the right-angle corner of the face:
//     a1 = dot(p2-p1, p3-p1); a2 = dot(p1-p2, p3-p2); a3 = dot(p1-p3, p1-p3)
//     w = (1,1,1); if |a1-1|<0.2 w=(0,1,1); else if |a2-1|<0.2 w=(1,0,1); else if |a3-1|<0.2 w=(1,1,0)
//     n = normalize(n1*w.x + n2*w.y + n3*w.z)   (same for tangent t, bitangent b)
//     ResultVerts[k].Normal = lerp(nk, n, ShadeFlat)  (k = each of the 3 corners; same for T,B)
//   At ShadeFlat=0 (default) lerp(nk,n,0)=nk → Normal/Tangent/Bitangent are UNCHANGED (pure un-weld).
//
// FORKS (named): none. The whole op (ShadeFlat knob + the corner-weight flat blend) is ported 1:1.
// Output topology: vertexCount = 3*faceCount, faceCount unchanged. Unwired → 0/0 (empty mesh).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + SwMeshView + cookMeshParam
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

void normalize3(float v[3]) {
  float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len > 1e-12f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

// count: topology CHANGE — output verts = 3 per face (un-weld); output faces = input faceCount.
// Unwired → 0/0 (empty mesh, faithful no-op).
void splitMeshVerticesCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                            int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx || !inputs[0].idx) { vtx = 0; idx = 0; return; }
  idx = inputs[0].faceCount;
  vtx = inputs[0].faceCount * 3u;
}

void splitMeshVerticesCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  float shadeFlat = cookMeshParam(c.params, "ShadeFlat", 0.0f);

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) {
    const SwTriIndex& tri = srcIdx[f];
    // Guard: only read source verts that exist (faithful to GPU's StructuredBuffer bounds behavior).
    if (tri.X < 0 || tri.Y < 0 || tri.Z < 0) continue;
    if ((uint32_t)tri.X >= in.vtxCount || (uint32_t)tri.Y >= in.vtxCount ||
        (uint32_t)tri.Z >= in.vtxCount)
      continue;
    uint32_t vbase = f * 3u;
    if (vbase + 2u >= c.vertexCount) break;  // output buffer bound

    const SwVertex& s1 = src[tri.X];
    const SwVertex& s2 = src[tri.Y];
    const SwVertex& s3 = src[tri.Z];

    // Whole-vertex copy first (Position/TexCoord/TexCoord2/Selection/ColorRgb carried verbatim).
    dst[vbase + 0] = s1;
    dst[vbase + 1] = s2;
    dst[vbase + 2] = s3;

    // Corner-weight flat blend (mesh-SplitVertices.hlsl). a3 uses (p1-p3)·(p1-p3) per the shader (sic).
    float p1[3] = {s1.Position.x, s1.Position.y, s1.Position.z};
    float p2[3] = {s2.Position.x, s2.Position.y, s2.Position.z};
    float p3[3] = {s3.Position.x, s3.Position.y, s3.Position.z};
    auto dot3 = [](const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; };
    float e21[3] = {p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2]};
    float e31[3] = {p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2]};
    float e12[3] = {p1[0]-p2[0], p1[1]-p2[1], p1[2]-p2[2]};
    float e32[3] = {p3[0]-p2[0], p3[1]-p2[1], p3[2]-p2[2]};
    float e13[3] = {p1[0]-p3[0], p1[1]-p3[1], p1[2]-p3[2]};
    float a1 = dot3(e21, e31);
    float a2 = dot3(e12, e32);
    float a3 = dot3(e13, e13);
    float w[3] = {1.0f, 1.0f, 1.0f};
    if (std::fabs(a1 - 1.0f) < 0.2f) { w[0]=0; w[1]=1; w[2]=1; }
    else if (std::fabs(a2 - 1.0f) < 0.2f) { w[0]=1; w[1]=0; w[2]=1; }
    else if (std::fabs(a3 - 1.0f) < 0.2f) { w[0]=1; w[1]=1; w[2]=0; }

    auto blend = [&](const SW_MESH_PACKED3& d1, const SW_MESH_PACKED3& d2, const SW_MESH_PACKED3& d3,
                     float out[3]) {
      out[0] = d1.x*w[0] + d2.x*w[1] + d3.x*w[2];
      out[1] = d1.y*w[0] + d2.y*w[1] + d3.y*w[2];
      out[2] = d1.z*w[0] + d2.z*w[1] + d3.z*w[2];
      normalize3(out);
    };
    auto lerp3 = [&](const SW_MESH_PACKED3& a, const float b[3]) -> SW_MESH_PACKED3 {
      return {a.x + (b[0]-a.x)*shadeFlat, a.y + (b[1]-a.y)*shadeFlat, a.z + (b[2]-a.z)*shadeFlat};
    };
    float n[3], t[3], b[3];
    blend(s1.Normal, s2.Normal, s3.Normal, n);
    blend(s1.Tangent, s2.Tangent, s3.Tangent, t);
    blend(s1.Bitangent, s2.Bitangent, s3.Bitangent, b);
    dst[vbase + 0].Normal = lerp3(s1.Normal, n);
    dst[vbase + 1].Normal = lerp3(s2.Normal, n);
    dst[vbase + 2].Normal = lerp3(s3.Normal, n);
    dst[vbase + 0].Tangent = lerp3(s1.Tangent, t);
    dst[vbase + 1].Tangent = lerp3(s2.Tangent, t);
    dst[vbase + 2].Tangent = lerp3(s3.Tangent, t);
    dst[vbase + 0].Bitangent = lerp3(s1.Bitangent, b);
    dst[vbase + 1].Bitangent = lerp3(s2.Bitangent, b);
    dst[vbase + 2].Bitangent = lerp3(s3.Bitangent, b);

    dstIdx[f] = {(int32_t)(vbase + 0), (int32_t)(vbase + 1), (int32_t)(vbase + 2)};
  }

  // Test injection (golden RED): corrupt the FIRST output vertex's Position in the REAL cook.
  if (meshInjectBug() && c.vertexCount > 0) dst[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec splitMeshVerticesSpec() {
  NodeSpec s;
  s.type = "SplitMeshVertices";
  s.title = "Split Mesh Vertices";
  // SplitMeshVertices.cs: ONE Mesh input (22370faa), ShadeFlat float (308f12dc), ONE Mesh output.
  PortSpec meshIn; meshIn.id = "Mesh"; meshIn.name = "Mesh"; meshIn.dataType = "Mesh"; meshIn.isInput = true;
  PortSpec sf; sf.id = "ShadeFlat"; sf.name = "ShadeFlat"; sf.dataType = "Float"; sf.isInput = true;
  sf.def = 0.0f; sf.minV = 0.0f; sf.maxV = 1.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {meshIn, sf, out};
  return s;
}

const MeshOp g_splitMeshVerticesOp(splitMeshVerticesSpec(), splitMeshVerticesCount, splitMeshVerticesCook);

}  // namespace
}  // namespace sw
