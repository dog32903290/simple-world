#include "runtime/radial_points.h"

#include <cmath>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/Particle.h"
#include "runtime/dispatch.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runRadialPointsSelfTest(bool injectBug) {
  using NS::StringEncoding::UTF8StringEncoding;
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  const uint32_t N = 256;
  const float kRadius = 2.0f;
  // The bug: generate at half radius while still asserting kRadius -> points
  // land on the wrong circle -> the eye must catch it.
  const float genRadius = injectBug ? kRadius * 0.5f : kRadius;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();  // owned
  MTL::CommandQueue* q = dev->newCommandQueue();        // owned

  // ---- load precompiled metallib (built from radial_points.metal) ----
  NS::Error* err = nullptr;
  NS::String* libPath = NS::String::string(SW_SHADER_METALLIB, UTF8StringEncoding);
  MTL::Library* lib = dev->newLibrary(libPath, &err);  // owned
  if (!lib) {
    printf("[selftest-radial] FAIL: cannot load metallib '%s': %s\n", SW_SHADER_METALLIB,
           err ? err->localizedDescription()->utf8String() : "(null)");
    q->release();
    dev->release();
    pool->release();
    return 1;
  }

  MTL::Function* fn = lib->newFunction(NS::String::string("radial_points", UTF8StringEncoding));
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);  // owned
  fn->release();
  if (!pso) {
    printf("[selftest-radial] FAIL: cannot build pipeline: %s\n",
           err ? err->localizedDescription()->utf8String() : "(null)");
    lib->release();
    q->release();
    dev->release();
    pool->release();
    return 1;
  }

  MTL::Buffer* buf = dev->newBuffer(N * sizeof(Particle), MTL::ResourceStorageModeShared);  // owned
  RadialParams params{N, genRadius, 1.0f};

  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(buf, 0, BI_Particles);
  enc->setBytes(&params, sizeof(params), BI_GenParams);
  const uint32_t tg = 64;
  const uint32_t groups = calcDispatchCount(N, tg);
  enc->dispatchThreadgroups(MTL::Size::Make(groups, 1, 1), MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  // ---- readback: assert every point lies on radius kRadius ----
  const Particle* pts = static_cast<const Particle*>(buf->contents());
  float maxErr = 0.0f;
  int worst = 0;
  for (uint32_t i = 0; i < N; ++i) {
    float x = pts[i].position.x;
    float y = pts[i].position.y;
    float r = std::sqrt(x * x + y * y);
    float e = std::fabs(r - kRadius);
    if (e > maxErr) {
      maxErr = e;
      worst = (int)i;
    }
  }
  const float tol = 1e-3f;
  bool pass = maxErr <= tol;
  printf("[selftest-radial] N=%u expectR=%.3f maxErr=%.5f (worst pt %d) -> %s\n", N, kRadius, maxErr,
         worst, pass ? "PASS" : "FAIL");

  buf->release();
  pso->release();
  lib->release();
  q->release();
  dev->release();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
