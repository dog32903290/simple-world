// pointsonmesh — faithful 1:1 port of external/tixl's two PointsOnMesh kernels:
//   pointsonmesh_calccdf    <- .../Assets/shaders/points/_internal/PointsOnMesh-CalcCdf2.hlsl
//   pointsonmesh_distribute <- .../Assets/shaders/points/onmesh/DistributePointsOnMesh.hlsl
// PointsOnMesh scatters `Count` points across a mesh surface, area-weighted: CalcCdf2 builds a
// per-face cumulative distribution (serial, [numthreads(1,1,1)]); Distribute draws 3 RNG samples per
// output point → binary-searches the CDF for a face → barycentric-interpolates Position/N/B/T/UV.
//
// ★MSL packing (metal-cpp-discipline): SwVertex/SwTriIndex come from sw_mesh.h's __METAL_VERSION__
// branch (packed_float3 12-byte members + int3 indices) so the 80-byte PbrVertex stride / 12-byte Int3
// stride match the host byte-exactly. SwPoint (tixl_point.h) is the 64-byte output point — TiXL's
// LegacyPoint maps: p.Selected→FX1, p.Stretch→Scale(broadcast), p.W→FX2.
//
// NAMED FORKS vs the .cs/.hlsl:
//   • PARITY (CalcCdf2): TiXL's first loop accumulates `sum += faceArea` into an UNINITIALIZED `sum`,
//     then immediately resets `sum = 0` before the real normalization sum — so that line is DEAD (no
//     output effect). Omitted here (the surviving math is the selection-weighted normalization).
//   • PARITY (Distribute): the CDF binary search is VERBATIM — `width = faceCount-2`, `cdfIndex` is
//     declared uninitialized, `steps = log2(width)+1`. On a 2-triangle mesh width=0 → steps collapses
//     to 0 → the loop body (which would set cdfIndex via the `right==left` branch) never runs and the
//     read of the uninitialized cdfIndex selects face 1 (TiXL edge behavior — a 2-tri mesh always
//     scatters onto face 1). Ported faithfully, NOT "fixed" (the area-weighting test uses ≥4 tris).
//   • Rotation matrix convention (the meshverticestopoints.metal:50-53 / addnoise.metal proof): TiXL
//     HLSL `float3x3(T,B,N)` builds the basis as ROWS, then transpose() flips it to basis-as-COLUMNS.
//     MSL `float3x3(T,B,N)` already builds basis-as-COLUMNS. So HLSL `transpose(float3x3(T,B,N))` ≡ MSL
//     `float3x3(T,B,N)` with NO transpose — exactly the form qFromMatrix3Precise expects.
//   • ColorMap: when a ColorMap texture is bound the kernel samples it at uv*(1,-1) (TiXL :127); the
//     host always binds a 1×1 white fallback when no Texture2D is wired → Color=(1,1,1,1) (TiXL's
//     UseFallbackTexture white.png). The second `Colors` output (ResultColors u1) is DEFERRED (the
//     color lives in p.Color); named fork.
#include <metal_stdlib>
#include "tixl_point.h"          // SwPoint (64B output)
#include "sw_mesh.h"             // SwVertex (80B) + SwTriIndex (12B) — MSL-shareable (packed_float3)
#include "pointsonmesh_params.h" // PomFaceProperties / PomCdfParams / PomDistributeParams + bindings
#include "shared/quat.metal.h"   // qFromMatrix3Precise (1:1 TiXL port)
using namespace metal;

// wang_hash — bit-exact port of DistributePointsOnMesh.hlsl:14-22 (in-out seed; MSL uint wraparound is
// defined, same as HLSL). Returns the mutated seed AND writes it back through `seed`.
static inline uint wang_hash(thread uint& seed) {
  seed = (seed ^ 61u) ^ (seed >> 16);
  seed *= 9u;
  seed = seed ^ (seed >> 4);
  seed *= 0x27d4eb2du;
  seed = seed ^ (seed >> 15);
  return seed;
}

// CalcCdf2 (PointsOnMesh-CalcCdf2.hlsl): ONE thread, ONE threadgroup — the serial CDF builder. Faithful
// to TiXL [numthreads(1,1,1)]. Per face: area = base×height (project p2 onto the p0→p1 base), isnan→0,
// times selection (sum of the 3 verts' Selection if UseVertexSelection, else 1). Then normalize so the
// running cdf ends at 1.0.
kernel void pointsonmesh_calccdf(device const SwVertex*      verts    [[buffer(POM_CDF_Vertices)]],
                                 device const SwTriIndex*    faces    [[buffer(POM_CDF_FaceIndices)]],
                                 device PomFaceProperties*   faceData [[buffer(POM_CDF_FaceData)]],
                                 constant PomCdfParams&      P        [[buffer(POM_CDF_Params)]],
                                 constant uint&              faceCount[[buffer(5)]],
                                 uint3                       i        [[thread_position_in_grid]]) {
  if (i.x >= faceCount) return;

  // Per-face area × selection (CalcCdf2.hlsl:28-52). The `sum += faceArea` line is DEAD in TiXL (see
  // the named PARITY fork) so it is omitted.
  for (uint j = 0; j < faceCount; j++) {
    SwTriIndex f = faces[j];
    float3 p0 = float3(verts[f.X].Position.x, verts[f.X].Position.y, verts[f.X].Position.z);
    float3 p1 = float3(verts[f.Y].Position.x, verts[f.Y].Position.y, verts[f.Y].Position.z);
    float3 p2 = float3(verts[f.Z].Position.x, verts[f.Z].Position.y, verts[f.Z].Position.z);

    float3 baseDir = p1 - p0;
    float a = length(baseDir);
    baseDir = normalize(baseDir);

    float3 heightStart = p0 + dot(p2 - p0, baseDir) * baseDir;
    float b = length(p2 - heightStart);
    float faceArea = a * b * 0.5;
    faceArea = isnan(faceArea) ? 0.0 : faceArea;

    float selection = (P.UseVertexSelection > 0.5)
                        ? (verts[f.X].Selection + verts[f.Y].Selection + verts[f.Z].Selection)
                        : 1.0;

    faceData[j].normalizedFaceArea = faceArea * selection;
    // TEST-ONLY (golden RED tooth, ForceUniformArea default 0 in production): force uniform weights so
    // the area-proportionality assertion FAILS (points spread evenly across faces, not by area).
    if (P.ForceUniformArea > 0.5) faceData[j].normalizedFaceArea = 1.0;
  }

  // Normalize: sum the weights, reciprocal, then running cdf (CalcCdf2.hlsl:54-67).
  float sum = 0.0;
  for (uint j = 0; j < faceCount; j++) sum += faceData[j].normalizedFaceArea;
  sum = 1.0 / sum;

  float cdf = 0.0;
  for (uint j = 0; j < faceCount; j++) {
    cdf += faceData[j].normalizedFaceArea * sum;
    faceData[j].cdf = cdf;
  }
}

// Distribute (DistributePointsOnMesh.hlsl): ONE thread per output point. RNG → CDF search → barycentric.
kernel void pointsonmesh_distribute(device const SwVertex*      verts  [[buffer(POM_DIST_Vertices)]],
                                    device const SwTriIndex*    faces  [[buffer(POM_DIST_FaceIndices)]],
                                    device const PomFaceProperties* CDFs[[buffer(POM_DIST_CDFs)]],
                                    device SwPoint*             pts    [[buffer(POM_DIST_ResultPoints)]],
                                    constant PomDistributeParams& P    [[buffer(POM_DIST_Params)]],
                                    constant uint&              faceCount[[buffer(5)]],
                                    texture2d<float>            colorMap[[texture(POM_DIST_ColorMap)]],
                                    sampler                     texSampler[[sampler(POM_DIST_TexSampler)]],
                                    uint3                       i      [[thread_position_in_grid]]) {
  if (i.x >= P.Count) return;

  // RNG seed (DistributePointsOnMesh.hlsl:51): rng = i.x * (uint)(Seed*10317). (uint)(float) truncates
  // toward zero — same as HLSL. PARITY: the (uint) cast is verbatim.
  uint rng_state = i.x * (uint)(P.Seed * 10317.0);
  float xi = (float(wang_hash(rng_state)) * (1.0 / 4294967296.0));

  // CDF binary search — VERBATIM (DistributePointsOnMesh.hlsl:54-78). PARITY: width=faceCount-2 +
  // uninitialized cdfIndex; a 2-tri mesh (width=0 → steps=0 → loop never runs) selects face 1 (the
  // uninitialized read; TiXL edge behavior, NOT a bug to fix).
  uint left = 0;
  uint width = faceCount - 2;
  uint right = width;
  uint steps = uint(log2((float)width)) + 1;
  uint cdfIndex;
  for (uint j = 0; j < steps; ++j) {
    uint middle = (right + left) / 2;
    float cdfSegStart = CDFs[middle].cdf;
    float cdfSegEnd = CDFs[middle + 1].cdf;
    if (right == left || (cdfSegStart <= xi && cdfSegEnd > xi)) {
      cdfIndex = middle + 1;
    } else {
      if (xi < cdfSegStart) right = middle;
      else                  left = middle + 1;
    }
  }

  uint faceIndex = cdfIndex;
  if (faceIndex >= faceCount) return;

  float xi1 = (float(wang_hash(rng_state)) * (1.0 / 4294967296.0));
  float xi2 = float(wang_hash(rng_state)) * (1.0 / 4294967296.0);

  SwTriIndex fI = faces[faceIndex];

  // Barycentric (DistributePointsOnMesh.hlsl:95-102): cosine-uniform over the triangle.
  float xi1Sqrt = sqrt(xi1);
  float u = 1.0 - xi1Sqrt;
  float v = xi2 * xi1Sqrt;
  float w = 1.0 - u - v;

  float3 v0 = float3(verts[fI.X].Position.x, verts[fI.X].Position.y, verts[fI.X].Position.z);
  float3 v1 = float3(verts[fI.Y].Position.x, verts[fI.Y].Position.y, verts[fI.Y].Position.z);
  float3 v2 = float3(verts[fI.Z].Position.x, verts[fI.Z].Position.y, verts[fI.Z].Position.z);
  float3 position = v0 * u + v1 * v + v2 * w;

  float3 n0 = float3(verts[fI.X].Normal.x, verts[fI.X].Normal.y, verts[fI.X].Normal.z);
  float3 n1 = float3(verts[fI.Y].Normal.x, verts[fI.Y].Normal.y, verts[fI.Y].Normal.z);
  float3 n2 = float3(verts[fI.Z].Normal.x, verts[fI.Z].Normal.y, verts[fI.Z].Normal.z);
  float3 normal = normalize(n0 * u + n1 * v + n2 * w);

  float3 b0 = float3(verts[fI.X].Bitangent.x, verts[fI.X].Bitangent.y, verts[fI.X].Bitangent.z);
  float3 b1 = float3(verts[fI.Y].Bitangent.x, verts[fI.Y].Bitangent.y, verts[fI.Y].Bitangent.z);
  float3 b2 = float3(verts[fI.Z].Bitangent.x, verts[fI.Z].Bitangent.y, verts[fI.Z].Bitangent.z);
  float3 binormal = normalize(b0 * u + b1 * v + b2 * w);

  float3 t0 = float3(verts[fI.X].Tangent.x, verts[fI.X].Tangent.y, verts[fI.X].Tangent.z);
  float3 t1 = float3(verts[fI.Y].Tangent.x, verts[fI.Y].Tangent.y, verts[fI.Y].Tangent.z);
  float3 t2 = float3(verts[fI.Z].Tangent.x, verts[fI.Z].Tangent.y, verts[fI.Z].Tangent.z);
  float3 tangent = normalize(t0 * u + t1 * v + t2 * w);

  // HLSL transpose(float3x3(tangent,binormal,normal)) ≡ MSL float3x3(tangent,binormal,normal) — the
  // named convention fork (no explicit transpose). DistributePointsOnMesh.hlsl:116-118.
  float3x3 orientationDest = float3x3(tangent, binormal, normal);
  float4 rotation = normalize(qFromMatrix3Precise(orientationDest));

  float2 uv0 = float2(verts[fI.X].Texcoord.x, verts[fI.X].Texcoord.y);
  float2 uv1 = float2(verts[fI.Y].Texcoord.x, verts[fI.Y].Texcoord.y);
  float2 uv2 = float2(verts[fI.Z].Texcoord.x, verts[fI.Z].Texcoord.y);
  float2 uv = uv0 * u + uv1 * v + uv2 * w;
  float4 color = colorMap.sample(texSampler, uv * float2(1.0, -1.0), level(0));

  // LegacyPoint → SwPoint (DistributePointsOnMesh.hlsl:91-130): Selected→FX1, Stretch→Scale(broadcast),
  // W→FX2 (all 1).
  SwPoint p;
  p.Position = position;
  p.FX1      = 1.0;             // p.Selected = 1
  p.Rotation = rotation;
  p.Color    = color;
  p.Scale    = float3(1.0);    // p.Stretch = 1 (float3 broadcast)
  p.FX2      = 1.0;            // p.W = 1
  pts[i.x] = p;
}
