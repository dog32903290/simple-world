#include "runtime/particle_system.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"
#include "runtime/particle_params.h"
#include "runtime/tixl_point.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr MTL::PixelFormat kTargetFormat = MTL::PixelFormatRGBA8Unorm;
constexpr float kRadius = 2.0f;
constexpr uint32_t kTG = 64;

MTL::ComputePipelineState* makeCompute(MTL::Device* dev, MTL::Library* lib, const char* name) {
  NS::Error* err = nullptr;
  MTL::Function* fn = lib->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
  if (!fn) return nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  return pso;
}

void dispatch1D(MTL::ComputeCommandEncoder* enc, uint32_t count) {
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(count, kTG), 1, 1),
                            MTL::Size::Make(kTG, 1, 1));
}

}  // namespace

ParticleSystem::ParticleSystem(MTL::Device* dev, MTL::Library* lib, uint32_t count, uint32_t width,
                               uint32_t height)
    : dev_(dev->retain()),
      count_(count),
      width_(width),
      height_(height),
      viewExtent_(kRadius * 1.75f) {
  psoEmit_ = makeCompute(dev_, lib, "radial_emit");
  psoTurb_ = makeCompute(dev_, lib, "turbulence_force");
  psoSim_ = makeCompute(dev_, lib, "particle_sim");

  NS::Error* err = nullptr;
  MTL::Function* vs = lib->newFunction(NS::String::string("draw_points_vs", NS::UTF8StringEncoding));
  MTL::Function* fs = lib->newFunction(NS::String::string("draw_points_fs", NS::UTF8StringEncoding));
  MTL::RenderPipelineDescriptor* rpd = MTL::RenderPipelineDescriptor::alloc()->init();
  rpd->setVertexFunction(vs);
  rpd->setFragmentFunction(fs);
  rpd->colorAttachments()->object(0)->setPixelFormat(kTargetFormat);
  rpsDraw_ = dev_->newRenderPipelineState(rpd, &err);
  rpd->release();
  if (vs) vs->release();
  if (fs) fs->release();

  emitPoints_ = dev_->newBuffer(count_ * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  particles_ = dev_->newBuffer(count_ * sizeof(Particle), MTL::ResourceStorageModeShared);
  resultPoints_ = dev_->newBuffer(count_ * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(kTargetFormat, width_, height_, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  target_ = dev_->newTexture(td);
}

ParticleSystem::~ParticleSystem() {
  if (target_) target_->release();
  if (resultPoints_) resultPoints_->release();
  if (particles_) particles_->release();
  if (emitPoints_) emitPoints_->release();
  if (rpsDraw_) rpsDraw_->release();
  if (psoSim_) psoSim_->release();
  if (psoTurb_) psoTurb_->release();
  if (psoEmit_) psoEmit_->release();
  dev_->release();
}

bool ParticleSystem::valid() const {
  return psoEmit_ && psoTurb_ && psoSim_ && rpsDraw_ && emitPoints_ && particles_ && resultPoints_ &&
         target_;
}

void ParticleSystem::runEmit(MTL::CommandQueue* q) {
  EmitParams ep{count_, kRadius, 0.0f, 0.0f};
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(psoEmit_);
  enc->setBuffer(emitPoints_, 0, EMIT_Points);
  enc->setBytes(&ep, sizeof(ep), EMIT_Params);
  dispatch1D(enc, count_);
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void ParticleSystem::runTurbulence(MTL::CommandQueue* q, float time) {
  TurbParams tp{};
  tp.Amount = turbAmount_;
  tp.Frequency = turbFrequency_;
  tp.Phase = time;
  tp.Variation = 0.0f;
  tp.SpeedFactor = 1.0f;
  tp.VariationGroupCount = 0.0f;
  tp.Count = count_;
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(psoTurb_);
  enc->setBuffer(particles_, 0, FORCE_Particles);
  enc->setBytes(&tp, sizeof(tp), FORCE_Params);
  dispatch1D(enc, count_);
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void ParticleSystem::runSim(MTL::CommandQueue* q, float time, bool emit, bool reset) {
  SimParams P{};
  P.Speed = speed_;
  P.Drag = drag_;
  P.InitialVelocity = 0.0f;  // let turbulence drive the flow
  P.Time = time;
  P.OrientTowardsVelocity = 0.15f;
  P.RadiusFromW = 0.01f;
  P.LifeTime = -1.0f;

  SimIntParams I{};
  I.TriggerEmit = emit ? 1 : 0;
  I.TriggerReset = reset ? 1 : 0;
  I.CollectCycleIndex = 0;
  I.SetFx1To = 0;
  I.SetFx2To = 0;
  I.EmitMode = 0;
  I.IsAutoCount = 1;  // persistent (never tooOld)
  I.EmitVelocityFactor = 0;
  I.EmitCount = (int32_t)count_;
  I.MaxParticleCount = (int32_t)count_;

  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(psoSim_);
  enc->setBuffer(emitPoints_, 0, SIM_EmitPoints);
  enc->setBuffer(particles_, 0, SIM_Particles);
  enc->setBuffer(resultPoints_, 0, SIM_ResultPoints);
  enc->setBytes(&P, sizeof(P), SIM_Params);
  enc->setBytes(&I, sizeof(I), SIM_IntParams);
  dispatch1D(enc, count_);
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void ParticleSystem::generate(MTL::CommandQueue* q) {
  runEmit(q);
  runSim(q, /*time=*/0.0f, /*emit=*/true, /*reset=*/true);  // seed particles + result from emit ring
}

void ParticleSystem::update(MTL::CommandQueue* q, float time, float /*dt*/) {
  runTurbulence(q, time);                                    // vel += curlNoise
  runSim(q, time, /*emit=*/false, /*reset=*/false);          // drag + integrate -> result
}

MTL::Texture* ParticleSystem::render(MTL::CommandQueue* q, uint32_t drawCount) {
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = rpd->colorAttachments()->object(0);
  ca->setTexture(target_);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);

  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rpd);
  enc->setRenderPipelineState(rpsDraw_);
  enc->setVertexBuffer(resultPoints_, 0, DRAW_Points);
  enc->setVertexBytes(&viewExtent_, sizeof(viewExtent_), DRAW_ViewExtent);
  if (drawCount > 0)
    enc->drawPrimitives(MTL::PrimitiveTypePoint, NS::UInteger(0), NS::UInteger(drawCount));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  return target_;
}

MTL::Texture* ParticleSystem::render(MTL::CommandQueue* q) { return render(q, count_); }

// ---------------------------------------------------------------------------
// Self-tests
// ---------------------------------------------------------------------------
namespace {
MTL::Library* loadLib(MTL::Device* dev) {
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib)
    printf("FAIL: metallib '%s': %s\n", SW_SHADER_METALLIB,
           err ? err->localizedDescription()->utf8String() : "(null)");
  return lib;
}
}  // namespace

int runParticleFlowSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 1024, W = 256, H = 256, STEPS = 30;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) { q->release(); dev->release(); pool->release(); return 1; }

  ParticleSystem ps(dev, lib, N, W, H);
  if (!ps.valid()) {
    printf("[selftest-flow] FAIL: pipeline build\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  ps.setTurbulenceAmount(injectBug ? 0.0f : 15.0f);  // bug: no force -> no flow

  ps.generate(q);
  for (uint32_t s = 0; s < STEPS; ++s) ps.update(q, 0.05f * (float)s, 1.0f / 60.0f);

  std::vector<SwPoint> out(N);
  std::memcpy(out.data(), ps.resultBuffer()->contents(), N * sizeof(SwPoint));
  float maxDev = 0.0f;
  for (uint32_t i = 0; i < N; ++i) {
    float x = out[i].Position.x, y = out[i].Position.y;
    float r = std::sqrt(x * x + y * y);
    float dev = std::fabs(r - kRadius);
    if (!std::isnan(dev) && dev > maxDev) maxDev = dev;
  }
  bool pass = maxDev > 0.1f;  // turbulence pushed points off the perfect ring
  printf("[selftest-flow] steps=%u maxDevFromRing=%.4f (need>0.1) turbAmount=%.0f -> %s\n", STEPS,
         maxDev, injectBug ? 0.0f : 15.0f, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

int runDrawPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 512, W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) { q->release(); dev->release(); pool->release(); return 1; }

  ParticleSystem ps(dev, lib, N, W, H);
  if (!ps.valid()) {
    printf("[selftest-draw] FAIL: pipeline build\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  ps.generate(q);  // emit ring only (no flow steps) -> clean ring
  MTL::Texture* tex = ps.render(q, injectBug ? 0 : N);

  std::vector<uint8_t> px(W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto lit = [&](uint32_t x, uint32_t y) {
    const uint8_t* p = &px[(y * W + x) * 4];
    return p[0] > 30 || p[1] > 30 || p[2] > 30;
  };
  int nonBlack = 0;
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x)
      if (lit(x, y)) ++nonBlack;
  bool centerBlack = true;
  for (uint32_t y = H / 2 - 8; y < H / 2 + 8; ++y)
    for (uint32_t x = W / 2 - 8; x < W / 2 + 8; ++x)
      if (lit(x, y)) centerBlack = false;
  bool pass = nonBlack > 50 && centerBlack;
  printf("[selftest-draw] nonBlack=%d (need>50) centerBlack=%d -> %s\n", nonBlack, centerBlack ? 1 : 0,
         pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}
}  // namespace sw
