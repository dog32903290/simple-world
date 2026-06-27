// GrowStrains — STATELESS 2-input transform (point/sim/experimental family). Faithful port of TiXL.
// PointsA (t0) × PointsB (t1) cartesian product gated by a GrowthMap texture (t2) → ResultPoints.
//
// Reference:
//   external/tixl/Operators/Lib/point/sim/experimental/GrowStrains.cs        (11 inputs + slots)
//   external/tixl/Operators/Lib/point/sim/experimental/GrowStrains.t3        (.t3 defaults)
//   external/tixl/Operators/Lib/Assets/shaders/points/sim/GrowStrains.hlsl   (per-point math, ported 1:1)
//
// NO cross-frame state, NO self-read: a plain per-frame transform that rides the SAME multi-Points-input
// gather as SetAttributesWithPointFields (inputs[0]=PointsA, inputs[1]=PointsB) PLUS the texture-into-
// points rail (inputTextures[0]=GrowthMap, the SamplePointAttributes precedent).
//
// COUNT-PRODUCT DRIVER SEAM (static-stash, proven RepeatAtPoints pattern): ResultCount = (CountA+1)*CountB
// (the +1 is the NaN-separator row per source loop, GrowStrains.hlsl:81). The cook writes a file-static
// the countTransform reads; single-threaded sequential cook → safe. Two-frame seeding is production-faithful.
//
// GrowthMap GATE: an unwired (→ 1×1 white fallback) GrowthMap samples r=1 → d=saturate(0.95)=0.95 (live).
// A black/zero GrowthMap → d=saturate(-0.05)=0 < 0.001 → d=NaN → every point degenerates to NaN (faithful
// to TiXL: GrowStrains needs a real growth map to produce geometry). We bind a 1×1 WHITE fallback when
// unwired so the op is a live transform by default (NOT silent NaN) — fork[white-growthmap-fallback].
//
// ★.t3 DEFAULTS (verified): Variation=0, NoiseAmount=0, Frequency=0, NoisePhase=0,
//   NoiseDistribution=(1,1,1), NoiseRotationLookUp=0.5, Length=0.13, Width=0.25, NoiseDensity=1.0.
//
// SwPoint == TiXL LegacyPoint (byte-identical 64B; W↔FX1@12, Stretch↔Scale@48, Selected↔FX2@60).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"        // calcDispatchCount
#include "runtime/graph.h"           // Graph/Node/pinId/findSpec
#include "runtime/point_graph.h"     // PointCookCtx, registerPointOp
#include "runtime/tex_op_cache.h"
#include "runtime/growstrains_params.h"
#include "runtime/tixl_point.h"      // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

static uint32_t g_growStrainsResultCount = 0;
uint32_t growStrainsCountTransform(uint32_t /*naturalCount*/) { return g_growStrainsResultCount; }

// A 1×1 white texture as the unwired-GrowthMap fallback (cached per device cook; tiny). fork[white-fallback].
MTL::Texture* makeWhite1x1(MTL::Device* dev) {
  MTL::TextureDescriptor* td = MTL::TextureDescriptor::alloc()->init();
  td->setTextureType(MTL::TextureType2D);
  td->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  td->setWidth(1); td->setHeight(1);
  td->setUsage(MTL::TextureUsageShaderRead);
  MTL::Texture* t = dev->newTexture(td);
  td->release();
  uint8_t white[4] = {255, 255, 255, 255};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, white, 4);
  return t;
}

void cookGrowStrains(PointCookCtx& c) {
  const MTL::Buffer* aBuf = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const MTL::Buffer* bBuf = (c.inputCount > 1) ? c.inputs[1] : nullptr;
  const uint32_t countA = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;
  const uint32_t countB = (c.inputCounts && c.inputCount > 1) ? c.inputCounts[1] : 0u;
  const uint32_t resultCount = (countA > 0 && countB > 0) ? (countA + 1u) * countB : 0u;
  g_growStrainsResultCount = resultCount;  // seed the NEXT cook's countTransform

  if (resultCount == 0 || !aBuf || !bBuf || !c.output || !c.lib) return;
  if (c.count < resultCount) return;  // frame-1 seeding; output not yet sized to the product

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "growstrains");
  if (!pso) return;

  GrowStrainsParams P{};
  P.Variation = cookParam(c, "Variation", 0.0f);
  P.NoiseAmount = cookParam(c, "NoiseAmount", 0.0f);
  P.Frequency = cookParam(c, "Frequency", 0.0f);
  P.Phase = cookParam(c, "NoisePhase", 0.0f);
  float dist[3] = {1.0f, 1.0f, 1.0f};
  cookVecN(c, "NoiseDistribution", dist, 3, dist);
  P.NoiseDistributionX = dist[0]; P.NoiseDistributionY = dist[1]; P.NoiseDistributionZ = dist[2];
  P.RotationLookupDistance = cookParam(c, "NoiseRotationLookUp", 0.5f);
  P.Length = cookParam(c, "Length", 0.13f);
  P.Width = cookParam(c, "Width", 0.25f);
  P.NoiseDensity = cookParam(c, "NoiseDensity", 1.0f);

  // GrowthMap (t2): wired → inputTextures[0]; unwired → 1×1 white (live by default).
  const MTL::Texture* growth = (c.inputTextureCount > 0 && c.inputTextures[0]) ? c.inputTextures[0] : nullptr;
  MTL::Texture* whiteFallback = growth ? nullptr : makeWhite1x1(c.dev);
  const MTL::Texture* growthTex = growth ? growth : whiteFallback;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(aBuf), 0, GROWSTRAINS_PointsA);
  enc->setBuffer(const_cast<MTL::Buffer*>(bBuf), 0, GROWSTRAINS_PointsB);
  enc->setBuffer(c.output, 0, GROWSTRAINS_Result);
  enc->setBytes(&P, sizeof(P), GROWSTRAINS_Params);
  enc->setBytes(&countA, sizeof(uint32_t), GROWSTRAINS_CountA);
  enc->setBytes(&countB, sizeof(uint32_t), GROWSTRAINS_CountB);
  enc->setBytes(&resultCount, sizeof(uint32_t), GROWSTRAINS_ResultCount);
  enc->setTexture(const_cast<MTL::Texture*>(growthTex), 0);
  enc->setSamplerState(samp, 0);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(resultCount, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  samp->release();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
  if (whiteFallback) whiteFallback->release();
}

}  // namespace

void registerGrowStrainsOp() {
  registerPointOp("GrowStrains", cookGrowStrains,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  growStrainsCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// ===================== Golden: direct-cook cartesian product + GrowthMap gate ================
// ★Drives the GPU kernel DIRECTLY with a 1×1 WHITE GrowthMap. CountA=1, CountB=1 → ResultCount=(1+1)*1=2.
// PointsA[0]=(2,0,0) (W=0, identity rot); PointsB[0]=(0,5,0) (W=0, identity rot). Params=.t3 defaults
// (NoiseAmount=0 → no noise offset, Length=0.13, Width=0.25). GrowthMap=white → r=1 → d=saturate(0.95)=0.95.
//   i=0 (sourceIndex=0, NOT separator):
//     pLocal = qRotateVec3(A.Pos, B.Rot=identity) = (2,0,0)
//     out.Position = pLocal*Length + B.Position + offset(=0) = (0.26, 5, 0)
//     out.W(FX1)   = d*Width = 0.95*0.25 = 0.2375
//   i=1 (sourceIndex=1 == sourceCount-1): SEPARATOR → out.W(FX1) = NaN
// Assert out[0].Position≈(0.26,5,0), out[0].FX1≈0.2375, and out[1].FX1 is NaN (the separator).
// injectBug: sever the GrowthMap (bind a BLACK 1×1 → r=0 → d=saturate(-0.05)=0 → NaN). Then out[0].FX1
// becomes NaN (not 0.2375) → the GrowthMap-gate tooth bites (RED).
int runGrowStrainsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-growstrains] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::Function* fn = lib->newFunction(NS::String::string("growstrains", NS::UTF8StringEncoding));
  if (!fn) { printf("[selftest-growstrains] FAIL: no kernel\n");
    lib->release(); q->release(); dev->release(); pool->release(); return 1; }
  NS::Error* psoErr = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &psoErr);
  fn->release();
  if (!pso) { printf("[selftest-growstrains] FAIL: no PSO\n");
    lib->release(); q->release(); dev->release(); pool->release(); return 1; }

  const uint32_t countA = 1, countB = 1, resultCount = (countA + 1) * countB;  // 2
  MTL::Buffer* aBuf = dev->newBuffer(countA * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* bBuf = dev->newBuffer(countB * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBuf = dev->newBuffer(resultCount * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* ap = static_cast<SwPoint*>(aBuf->contents());
  SwPoint* bp = static_cast<SwPoint*>(bBuf->contents());
  ap[0] = SwPoint{}; ap[0].Position = {2, 0, 0}; ap[0].Rotation = {0, 0, 0, 1}; ap[0].FX1 = 0.0f;
  bp[0] = SwPoint{}; bp[0].Position = {0, 5, 0}; bp[0].Rotation = {0, 0, 0, 1}; bp[0].FX1 = 0.0f;

  // GrowthMap: white (live) or black (bug, severs the gate).
  MTL::TextureDescriptor* td = MTL::TextureDescriptor::alloc()->init();
  td->setTextureType(MTL::TextureType2D); td->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  td->setWidth(1); td->setHeight(1); td->setUsage(MTL::TextureUsageShaderRead);
  MTL::Texture* growth = dev->newTexture(td); td->release();
  uint8_t texel[4] = {injectBug ? (uint8_t)0 : (uint8_t)255,
                      injectBug ? (uint8_t)0 : (uint8_t)255,
                      injectBug ? (uint8_t)0 : (uint8_t)255, 255};
  growth->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, texel, 4);

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear); sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = dev->newSamplerState(sd); sd->release();

  GrowStrainsParams P{};
  P.Variation = 0; P.NoiseAmount = 0; P.Frequency = 0; P.Phase = 0;
  P.NoiseDistributionX = 1; P.NoiseDistributionY = 1; P.NoiseDistributionZ = 1;
  P.RotationLookupDistance = 0.5f; P.Length = 0.13f; P.Width = 0.25f; P.NoiseDensity = 1.0f;

  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(aBuf, 0, GROWSTRAINS_PointsA);
  enc->setBuffer(bBuf, 0, GROWSTRAINS_PointsB);
  enc->setBuffer(outBuf, 0, GROWSTRAINS_Result);
  enc->setBytes(&P, sizeof(P), GROWSTRAINS_Params);
  enc->setBytes(&countA, sizeof(uint32_t), GROWSTRAINS_CountA);
  enc->setBytes(&countB, sizeof(uint32_t), GROWSTRAINS_CountB);
  enc->setBytes(&resultCount, sizeof(uint32_t), GROWSTRAINS_ResultCount);
  enc->setTexture(growth, 0);
  enc->setSamplerState(samp, 0);
  enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(64, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();

  SwPoint* o = static_cast<SwPoint*>(outBuf->contents());
  bool posOK = std::fabs(o[0].Position.x - 0.26f) < 1e-3f &&
               std::fabs(o[0].Position.y - 5.0f) < 1e-3f &&
               std::fabs(o[0].Position.z - 0.0f) < 1e-3f;
  bool wOK = std::fabs(o[0].FX1 - 0.2375f) < 1e-3f;       // d*Width = 0.95*0.25
  bool sepOK = std::isnan(o[1].FX1);                       // separator row W = NaN
  // Same load-bearing assertion in BOTH modes. injectBug binds a BLACK GrowthMap → d=saturate(-0.05)=0 →
  // d=NaN → out0.W is NaN not 0.2375 → wOK false → the test FAILS (returns non-zero) = the GrowthMap-gate
  // tooth BITES (--bite contract).
  bool pass = posOK && wOK && sepOK;
  printf("[selftest-growstrains] out0.Pos=(%.3f,%.3f,%.3f) out0.W=%.4f out1.W=%s "
         "pos=%d w=%d sep=%d -> %s%s\n",
         o[0].Position.x, o[0].Position.y, o[0].Position.z, o[0].FX1,
         std::isnan(o[1].FX1) ? "NaN" : "finite", posOK ? 1 : 0, wOK ? 1 : 0, sepOK ? 1 : 0,
         pass ? "PASS" : "FAIL", injectBug ? " (bug: black GrowthMap → gate must NaN out0.W)" : "");

  pso->release(); aBuf->release(); bBuf->release(); outBuf->release();
  growth->release(); samp->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
