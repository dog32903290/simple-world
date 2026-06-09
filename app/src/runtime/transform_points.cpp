#include "runtime/transform_points.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/Particle.h"
#include "runtime/dispatch.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

MTL::ComputePipelineState* makePSO(MTL::Device* dev, MTL::Library* lib, const char* name) {
  NS::Error* err = nullptr;
  MTL::Function* fn = lib->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  return pso;
}

void dispatch1D(MTL::CommandQueue* q, MTL::ComputePipelineState* pso, uint32_t count,
                MTL::Buffer* particles, const void* slot1, size_t slot1Size, int slot1Index,
                const void* slot2, size_t slot2Size, int slot2Index) {
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(particles, 0, BI_Particles);
  if (slot1) enc->setBytes(slot1, slot1Size, slot1Index);
  if (slot2) enc->setBytes(slot2, slot2Size, slot2Index);
  const uint32_t tg = 64;
  const uint32_t groups = calcDispatchCount(count, tg);
  enc->dispatchThreadgroups(MTL::Size::Make(groups, 1, 1), MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

}  // namespace

int runTransformPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 256;
  const float kRadius = 2.0f, kSpeed = 1.0f, expectedDt = 0.5f;
  const float dt = injectBug ? 0.0f : expectedDt;  // bug: nothing moves

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-transform] FAIL: metallib '%s': %s\n", SW_SHADER_METALLIB,
           err ? err->localizedDescription()->utf8String() : "(null)");
    q->release();
    dev->release();
    pool->release();
    return 1;
  }
  MTL::ComputePipelineState* psoR = makePSO(dev, lib, "radial_points");
  MTL::ComputePipelineState* psoT = makePSO(dev, lib, "transform_points");
  if (!psoR || !psoT) {
    printf("[selftest-transform] FAIL: pipeline build\n");
    return 1;
  }

  MTL::Buffer* buf = dev->newBuffer(N * sizeof(Particle), MTL::ResourceStorageModeShared);

  // 1) generate radial points (position + tangential velocity)
  RadialParams rp{N, kRadius, kSpeed};
  dispatch1D(q, psoR, N, buf, &rp, sizeof(rp), BI_GenParams, nullptr, 0, 0);

  std::vector<Particle> before(N);
  std::memcpy(before.data(), buf->contents(), N * sizeof(Particle));

  // 2) evolve once with a known frame context (audio fields unused on the GPU side)
  EvaluationContext ctx{1u, dt, dt, 0.0f, 0.0f};
  uint32_t count = N;
  dispatch1D(q, psoT, N, buf, &ctx, sizeof(ctx), BI_EvalContext, &count, sizeof(count), BI_GenParams);

  const Particle* after = static_cast<const Particle*>(buf->contents());
  float maxErr = 0.0f;
  int worst = 0;
  for (uint32_t i = 0; i < N; ++i) {
    float ex = before[i].position.x + before[i].velocity.x * expectedDt;
    float ey = before[i].position.y + before[i].velocity.y * expectedDt;
    float dx = after[i].position.x - ex;
    float dy = after[i].position.y - ey;
    float e = std::sqrt(dx * dx + dy * dy);
    if (e > maxErr) {
      maxErr = e;
      worst = (int)i;
    }
  }
  bool pass = maxErr <= 1e-3f;
  printf("[selftest-transform] N=%u dt=%.3f assert(pos+=vel*dt) maxErr=%.5f (worst %d) -> %s\n", N,
         expectedDt, maxErr, worst, pass ? "PASS" : "FAIL");

  buf->release();
  psoT->release();
  psoR->release();
  lib->release();
  q->release();
  dev->release();
  pool->release();
  return pass ? 0 : 1;
}
}  // namespace sw
