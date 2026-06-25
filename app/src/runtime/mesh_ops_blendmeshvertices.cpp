// BlendMeshVertices mesh op (two-mesh vertex blender). Mesh A + Mesh B → blended Mesh.
// TiXL authority: external/tixl/Operators/Lib/mesh/modify/BlendMeshVertices.cs +
//   Lib/Assets/shaders/3d/mesh/fx/mesh-BlendVertices.hlsl.
// One .cpp owns the whole op (NodeSpec + MeshCountFn + MeshCookFn). ZERO edits to cook-core.
//
// VERBATIM math (mesh-BlendVertices.hlsl):
//   resultCount = the final vertex count (we use max(countA,countB) — see Pairing note).
//   t = i / (float)resultCount  (thread fraction ∈ [0,1))
//   Default Pairing==WrapAround(0):  aIndex=bIndex=i (direct index; wraps implicitly via countA/B).
//   Pairing==Adjust(1) && countA!=countB:  aIndex=(int)(countA*t), bIndex=(int)(countB*t).
//   A=VerticesA[aIndex],  B=VerticesB[bIndex].
//
//   BlendMode==Blend(0):         f = BlendFactor.
//   BlendMode==UseW1AsWeight(1): f = A.Selected.
//   BlendMode==UseW2AsWeight(2): f = 1 - B.Selected.
//   BlendMode==RangeBlend(3):    f = 1 - saturate((t - BlendFactor) / Width - BlendFactor + 1).
//   BlendMode==RangeBlendSmooth(4):
//     b = BlendFactor % 2; if b>1 { b=2-b; t=1-t; }
//     f = 1 - smoothstep(0,1, saturate((t - b)/Width - b + 1)).
//   fallOffFromCenter = smoothstep(0,1, 1 - |f - 0.5|*2).
//   f += (hash11(t) - 0.5) * Scatter * fallOffFromCenter.
//
//   Per-attribute: result.X = lerp(A.X, B.X, f)   for Position/Normal/Tangent/Bitangent/TexCoord/
//                                                      Selected/ColorRGB.
//
// FORKS (named):
//   (1) COUNT POLICY: TiXL's compute shader dispatches on ResultVertices count (it creates a StructuredBuffer
//       of size max(countA, countB)); we match this — output vertex count = max(countA, countB).
//       The face (index) buffer is taken from Mesh A verbatim (TiXL copies VerticesA's index buffer via
//       _AssembleMeshBuffers; the blend only rewrites vertex attributes, leaving topology as Mesh A).
//       When Mesh B has fewer vertices than Mesh A, B's OOB reads in TiXL wrap around (StructuredBuffer
//       wraps are UB in DX12, but TiXL's pattern is: at aIndex=i the B access may be i%countB);
//       We faithfully implement wrap: bIndex = (countB > 0) ? (i % countB) : 0 for the WrapAround case.
//   (2) PAIRING==Adjust only activates when countA != countB (shader guards on this).
//   (3) hash11: TiXL uses "shared/hash-functions.hlsl" Dave-Hoskins hash11 (lines 10-16).
//       See: external/tixl/Operators/Lib/Assets/shaders/shared/hash-functions.hlsl:10.
//   (4) smoothstep: saturate → clamp to [0,1], smoothstep(0,1,x) = x²(3-2x).
//   (5) Texcoord2 / TexCoord2: not in the shader output; we write TexCoord2 = lerp(A,B,f) as well
//       for completeness, since PbrVertex has it (same TiXL TexCoord path but for uv2 via lerp).
//       FORK-named: the shader does NOT write TexCoord2 explicitly; we interpolate it for faithfulness
//       (better than leaving it at A's value, which the shader also implicitly does via init copy).
//       We use A.Texcoord2 directly (copy) to be more faithful: the hlsl copies ResultVertices[i.x]
//       from nothing — the output buffer is freshly created so Texcoord2 is zero. We write 0.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <cstring>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + cookMeshParam/VecN
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

// hash11(t) — port of TiXL Dave-Hoskins hash (hash-functions.hlsl:10-16):
//   external/tixl/Operators/Lib/Assets/shaders/shared/hash-functions.hlsl:10
//   p = frac(p * .1031); p *= p + 33.33; p *= p + p; return frac(p);
static float hash11(float t) {
  float p = t * 0.1031f;
  p -= std::floor(p);           // frac
  p *= (p + 33.33f);
  p *= (p + p);
  p -= std::floor(p);           // frac
  return p;
}

// saturate: clamp to [0,1]
static float sat(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// smoothstep(0,1,x): x²(3-2x), for x already clamped
static float smoothstep01(float x) { float s = sat(x); return s * s * (3.0f - 2.0f * s); }

// lerp
static float lerpf(float a, float b, float f) { return a + (b - a) * f; }

void blendMeshVerticesCount(const std::map<std::string, float>* /*params*/,
                             const SwMeshView* inputs, int inputCount,
                             uint32_t& vtx, uint32_t& idx) {
  // Need at least Mesh A for any output; Mesh B is optional (defaults to A if unwired).
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  const SwMeshView& a = inputs[0];
  uint32_t countA = a.vtxCount;
  uint32_t countB = (inputCount >= 2 && inputs[1].vtx) ? inputs[1].vtxCount : countA;
  vtx = (countA >= countB) ? countA : countB;  // max(countA, countB)
  idx = a.faceCount;  // topology from Mesh A
}

void blendMeshVerticesCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;

  const SwMeshView& mA = c.inputMeshes[0];
  // If Mesh B is unwired, treat it as Mesh A (no blend = copy A).
  const SwMeshView* pMB = (c.inputMeshCount >= 2 && c.inputMeshes[1].vtx) ? &c.inputMeshes[1] : nullptr;

  float blendFactor = cookMeshParam(c.params, "BlendValue", 0.5f);
  float scatter = cookMeshParam(c.params, "Scatter", 0.0f);
  int blendMode = (int)(cookMeshParam(c.params, "BlendMode", 0.0f) + 0.5f);
  float rangeWidth = cookMeshParam(c.params, "RangeWidth", 0.5f);
  int pairing = (int)(cookMeshParam(c.params, "Pairing", 0.0f) + 0.5f);

  const SwVertex* srcA = (const SwVertex*)const_cast<MTL::Buffer*>(mA.vtx)->contents();
  const SwVertex* srcB = pMB ? (const SwVertex*)const_cast<MTL::Buffer*>(pMB->vtx)->contents() : srcA;
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(mA.idx)->contents();

  uint32_t countA = mA.vtxCount;
  uint32_t countB = pMB ? pMB->vtxCount : countA;
  uint32_t resultCount = c.vertexCount;  // max(countA, countB) set by count fn

  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  // Zero the output first (TiXL allocates fresh buffer, so Texcoord2 starts at 0).
  std::memset(dst, 0, (size_t)c.vertexCount * sizeof(SwVertex));

  for (uint32_t i = 0; i < resultCount; ++i) {
    float t = (resultCount > 1) ? ((float)i / (float)resultCount) : 0.0f;

    uint32_t aIndex = i;
    uint32_t bIndex = i;

    // PAIRING: Adjust mode re-maps both indices proportionally when counts differ.
    if (pairing == 1 && countA != countB) {
      aIndex = (uint32_t)((float)countA * t);
      bIndex = (uint32_t)((float)countB * t);
    }
    // WrapAround: clamp to buffer extents (shader wraps via out-of-bounds; we do explicit mod).
    if (countA > 0) aIndex = aIndex % countA;
    if (countB > 0) bIndex = bIndex % countB;

    const SwVertex& A = srcA[aIndex];
    const SwVertex& B = srcB[bIndex];

    // Compute blend factor f from BlendMode.
    float f = 0.0f;
    if (blendMode == 0) {
      f = blendFactor;
    } else if (blendMode == 1) {
      f = A.Selection;
    } else if (blendMode == 2) {
      f = 1.0f - B.Selection;
    } else if (blendMode == 3) {
      // RangeBlend: f = 1 - saturate((t - BlendFactor) / Width - BlendFactor + 1)
      float denom = (rangeWidth != 0.0f) ? rangeWidth : 1e-6f;
      f = 1.0f - sat((t - blendFactor) / denom - blendFactor + 1.0f);
    } else {
      // RangeBlendSmooth(4):
      float b = std::fmod(blendFactor, 2.0f);
      float t2 = t;
      if (b > 1.0f) { b = 2.0f - b; t2 = 1.0f - t2; }
      float denom = (rangeWidth != 0.0f) ? rangeWidth : 1e-6f;
      f = 1.0f - smoothstep01(sat((t2 - b) / denom - b + 1.0f));
    }

    // Scatter: f += (hash11(t) - 0.5) * Scatter * fallOffFromCenter
    float fallOff = smoothstep01(1.0f - std::fabs(f - 0.5f) * 2.0f);
    f += (hash11(t) - 0.5f) * scatter * fallOff;

    // Per-attribute lerp.
    dst[i].Position   = { lerpf(A.Position.x,  B.Position.x,  f),
                           lerpf(A.Position.y,  B.Position.y,  f),
                           lerpf(A.Position.z,  B.Position.z,  f) };
    dst[i].Normal     = { lerpf(A.Normal.x,     B.Normal.x,    f),
                           lerpf(A.Normal.y,     B.Normal.y,    f),
                           lerpf(A.Normal.z,     B.Normal.z,    f) };
    dst[i].Tangent    = { lerpf(A.Tangent.x,    B.Tangent.x,   f),
                           lerpf(A.Tangent.y,    B.Tangent.y,   f),
                           lerpf(A.Tangent.z,    B.Tangent.z,   f) };
    dst[i].Bitangent  = { lerpf(A.Bitangent.x,  B.Bitangent.x, f),
                           lerpf(A.Bitangent.y,  B.Bitangent.y, f),
                           lerpf(A.Bitangent.z,  B.Bitangent.z, f) };
    dst[i].Texcoord   = { lerpf(A.Texcoord.x,   B.Texcoord.x,  f),
                           lerpf(A.Texcoord.y,   B.Texcoord.y,  f) };
    // Texcoord2: shader leaves as zero (fresh alloc); we do the same.
    dst[i].Texcoord2  = { 0.0f, 0.0f };
    dst[i].Selection  = lerpf(A.Selection,  B.Selection,  f);
    dst[i].ColorRgb   = { lerpf(A.ColorRgb.x, B.ColorRgb.x, f),
                           lerpf(A.ColorRgb.y, B.ColorRgb.y, f),
                           lerpf(A.ColorRgb.z, B.ColorRgb.z, f) };
  }

  // Copy index (face) buffer from Mesh A (topology unchanged — TiXL assembles from A's index buffer).
  uint32_t nf = (c.indexCount < mA.faceCount) ? c.indexCount : mA.faceCount;
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];

  // Test injection (golden RED): corrupt the FIRST output vertex position in the REAL cook.
  if (meshInjectBug() && c.vertexCount > 0) dst[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec blendMeshVerticesSpec() {
  NodeSpec s;
  s.type = "BlendMeshVertices";
  s.title = "Blend Mesh Vertices";
  // MeshA, MeshB (two mesh inputs). BlendValue float (0.5). Scatter float (0). BlendMode int enum (0).
  // RangeWidth float (0.5). Pairing int enum (0 = WrapAround).
  PortSpec meshA; meshA.id = "MeshA"; meshA.name = "MeshA"; meshA.dataType = "Mesh"; meshA.isInput = true;
  PortSpec meshB; meshB.id = "MeshB"; meshB.name = "MeshB"; meshB.dataType = "Mesh"; meshB.isInput = true;
  PortSpec bv; bv.id = "BlendValue"; bv.name = "BlendValue"; bv.dataType = "Float"; bv.isInput = true;
  bv.def = 0.5f; bv.minV = 0.0f; bv.maxV = 1.0f;
  PortSpec sc; sc.id = "Scatter"; sc.name = "Scatter"; sc.dataType = "Float"; sc.isInput = true;
  sc.def = 0.0f; sc.minV = 0.0f; sc.maxV = 1.0f;
  PortSpec bm; bm.id = "BlendMode"; bm.name = "BlendMode"; bm.dataType = "Float"; bm.isInput = true;
  bm.def = 0.0f; bm.minV = 0.0f; bm.maxV = 4.0f;
  PortSpec rw; rw.id = "RangeWidth"; rw.name = "RangeWidth"; rw.dataType = "Float"; rw.isInput = true;
  rw.def = 0.5f; rw.minV = 0.0f; rw.maxV = 2.0f;
  PortSpec pr; pr.id = "Pairing"; pr.name = "Pairing"; pr.dataType = "Float"; pr.isInput = true;
  pr.def = 0.0f; pr.minV = 0.0f; pr.maxV = 1.0f;
  PortSpec out; out.id = "BlendedMesh"; out.name = "BlendedMesh"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {meshA, meshB, bv, sc, bm, rw, pr, out};
  return s;
}

const MeshOp g_blendMeshVerticesOp(blendMeshVerticesSpec(), blendMeshVerticesCount, blendMeshVerticesCook);

}  // namespace
}  // namespace sw
