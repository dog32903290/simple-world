// PointsOnMesh — the FIRST Points op that does an AREA-WEIGHTED barycentric SURFACE SCATTER (Count
// points sprinkled over a mesh, more on bigger triangles). Faithful port of external/tixl
// .../point/generate/PointsOnMesh.cs (.cs ports) + two kernels:
//   .../Assets/shaders/points/_internal/PointsOnMesh-CalcCdf2.hlsl  (serial per-face CDF)
//   .../Assets/shaders/points/onmesh/DistributePointsOnMesh.hlsl     (per-point sampler)
//
// SEAMS it rides: the mesh-into-points seam (PointCookCtx::meshVtx + meshIdx, 5d1f8bd — the FIRST op
// to consume meshIdx) AND the texture-into-points seam (PointCookCtx::inputTextures[0] = the optional
// ColorMap). Two GPU dispatches over ONE command buffer: CalcCdf2 (1 thread → a scratch CDF buffer)
// then Distribute (one thread per output point, samples the CDF + the mesh + ColorMap).
//
// Count fork: PointsOnMesh sizes its output to the Count Float input (the .t3 default is 10000), NOT
// to the mesh vertex count — so it registers with the DEFAULT count policy (countFromMeshVtx=false);
// the Count Float port drives the bag size through the value spine (point_graph.cpp:440-445).
//
// NAMED TiXL forks (see also pointsonmesh.metal headers):
//   • CalcCdf2 dead `sum += faceArea` line omitted (no output effect; the surviving normalization is
//     the selection-weighted one).
//   • Distribute CDF search VERBATIM: width=faceCount-2 + uninitialized cdfIndex → a 2-tri mesh always
//     scatters onto face 1 (TiXL edge behavior, ported faithfully, NOT fixed).
//   • Threadgroup size: TiXL uses [numthreads(160,1,1)] for Distribute; we use the project's standard
//     64 (independent threads, not load-bearing — same total dispatch via calcDispatchCount).
//   • Second `Colors` output (ResultColors u1) DEFERRED — the color already lives in p.Color.
//   • Unwired ColorMap → a 1×1 white fallback (TiXL's UseFallbackTexture white.png) → Color=(1,1,1,1).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                  // calcDispatchCount
#include "runtime/eval_context.h"
#include "runtime/point_graph.h"               // PointCookCtx, registerPointOp, cookParam
#include "runtime/pointsonmesh_params.h"       // PomFaceProperties / PomCdfParams / PomDistributeParams
#include "runtime/sw_mesh.h"                   // SwVertex / SwTriIndex
#include "runtime/tixl_point.h"                // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// A 1×1 opaque-white RGBA8 texture — the unwired-ColorMap fallback (TiXL UseFallbackTexture white.png).
MTL::Texture* makeWhite1x1(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  uint8_t px[4] = {255, 255, 255, 255};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

}  // namespace

// PointsOnMesh cook (visible to the golden leaf): CalcCdf2 (scratch CDF) → Distribute (scatter Count
// points). No Mesh input (or 0 faces) → empty bag (mirror meshverticestopoints' unwired-Mesh guard).
// forceUniformArea is a TEST-ONLY knob (default 0 in production) for the area-weighting RED tooth.
void cookPointsOnMeshImpl(PointCookCtx& c, float forceUniformArea) {
  if (!c.output || !c.lib || c.count == 0) return;
  const MTL::Buffer* verts = c.meshVtx;
  const MTL::Buffer* faces = c.meshIdx;
  if (!verts || !faces || c.meshFaceCount == 0) return;  // unwired Mesh / no faces -> empty bag

  MTL::Function* fnCdf =
      c.lib->newFunction(NS::String::string("pointsonmesh_calccdf", NS::UTF8StringEncoding));
  MTL::Function* fnDist =
      c.lib->newFunction(NS::String::string("pointsonmesh_distribute", NS::UTF8StringEncoding));
  if (!fnCdf || !fnDist) { if (fnCdf) fnCdf->release(); if (fnDist) fnDist->release(); return; }
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* psoCdf = c.dev->newComputePipelineState(fnCdf, &err);
  MTL::ComputePipelineState* psoDist = c.dev->newComputePipelineState(fnDist, &err);
  fnCdf->release(); fnDist->release();
  if (!psoCdf || !psoDist) {
    if (psoCdf) psoCdf->release(); if (psoDist) psoDist->release();
    return;
  }

  // Scratch CDF buffer: one PomFaceProperties per face (CalcCdf2 out / Distribute in).
  MTL::Buffer* cdf = c.dev->newBuffer((size_t)c.meshFaceCount * sizeof(PomFaceProperties),
                                      MTL::ResourceStorageModeShared);

  // ColorMap (texture-into-points seam): the first wired Texture2D input, else a 1×1 white fallback.
  const MTL::Texture* colorMap = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;
  MTL::Texture* whiteFallback = colorMap ? nullptr : makeWhite1x1(c.dev);
  const MTL::Texture* boundMap = colorMap ? colorMap : whiteFallback;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);  // TiXL SamplerState default Wrap
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  PomCdfParams cdfP{};
  cdfP.UseVertexSelection = cookParam(c, "UseVertexSelection", 1.0f);  // .t3 default true
  cdfP.ForceUniformArea = forceUniformArea;
  PomDistributeParams distP{};
  distP.Seed = cookParam(c, "Seed", 10.0f);  // .t3 default 10.0
  distP.UseVertexSelection = cdfP.UseVertexSelection;
  distP.Count = c.count;
  uint32_t faceCount = c.meshFaceCount;

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();

  // Dispatch 1: CalcCdf2 — ONE thread (faithful to TiXL [numthreads(1,1,1)]; the loop is serial).
  enc->setComputePipelineState(psoCdf);
  enc->setBuffer(const_cast<MTL::Buffer*>(verts), 0, POM_CDF_Vertices);
  enc->setBuffer(const_cast<MTL::Buffer*>(faces), 0, POM_CDF_FaceIndices);
  enc->setBuffer(cdf, 0, POM_CDF_FaceData);
  enc->setBytes(&cdfP, sizeof(cdfP), POM_CDF_Params);
  enc->setBytes(&faceCount, sizeof(faceCount), 5);
  enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(1, 1, 1));

  // Dispatch 2: Distribute — one thread per output point.
  enc->setComputePipelineState(psoDist);
  enc->setBuffer(const_cast<MTL::Buffer*>(verts), 0, POM_DIST_Vertices);
  enc->setBuffer(const_cast<MTL::Buffer*>(faces), 0, POM_DIST_FaceIndices);
  enc->setBuffer(cdf, 0, POM_DIST_CDFs);
  enc->setBuffer(c.output, 0, POM_DIST_ResultPoints);
  enc->setBytes(&distP, sizeof(distP), POM_DIST_Params);
  enc->setBytes(&faceCount, sizeof(faceCount), 5);
  enc->setTexture(const_cast<MTL::Texture*>(boundMap), POM_DIST_ColorMap);
  enc->setSamplerState(samp, POM_DIST_TexSampler);
  const uint32_t tg = 64;  // PARITY: TiXL uses 160; independent threads, not load-bearing.
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  if (whiteFallback) whiteFallback->release();
  samp->release();
  cdf->release();
  psoCdf->release();
  psoDist->release();
}

namespace {
void cookPointsOnMesh(PointCookCtx& c) { cookPointsOnMeshImpl(c, /*forceUniformArea=*/0.0f); }
}  // namespace

void registerPointsOnMeshOp() {
  // DEFAULT count policy (countFromMeshVtx=false): the Count Float port drives the output bag size
  // (the value spine resolves it via point_graph.cpp:440-445), NOT the mesh vertex count.
  registerPointOp("PointsOnMesh", cookPointsOnMesh);
}

}  // namespace sw
