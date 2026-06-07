#pragma once
#include <cstdint>

#include "runtime/tixl_point.h"  // Point, Particle (64B)

namespace MTL {
class Device;
class Library;
class CommandQueue;
class Buffer;
class Texture;
class ComputePipelineState;
class RenderPipelineState;
}  // namespace MTL

namespace sw {

// TiXL-ported particle system (Mac 版 TiXL). Pipeline, faithful to
// external/tixl .../particles/{ParticleSystem,TurbulanceForce}.hlsl:
//   radial_emit -> EmitPoints(Point) --emit--> Particles(Particle)
//   per frame:  TurbulenceForce(vel += curlNoise) -> ParticleSystem integrator
//               (drag + pos += vel*Speed*0.01 + orient + lifetime) -> ResultPoints(Point)
//   render:     DrawPoints(ResultPoints) -> target texture
// All Metal objects owned here (metal-cpp-discipline).
class ParticleSystem {
 public:
  ParticleSystem(MTL::Device* dev, MTL::Library* lib, uint32_t count, uint32_t width,
                 uint32_t height);
  ~ParticleSystem();
  ParticleSystem(const ParticleSystem&) = delete;
  ParticleSystem& operator=(const ParticleSystem&) = delete;

  bool valid() const;

  void generate(MTL::CommandQueue* q);                          // emit ring + seed particles
  void update(MTL::CommandQueue* q, float time, float dt);      // turbulence + integrate one step
  MTL::Texture* render(MTL::CommandQueue* q, uint32_t drawCount);
  MTL::Texture* render(MTL::CommandQueue* q);                   // draws all `count` result points

  // Live node params (read each dispatch). Faithful subset of TiXL's
  // TurbulenceForce (Amount/Frequency) + ParticleSystem (Speed/Drag).
  void setTurbulenceAmount(float a) { turbAmount_ = a; }
  void setTurbulenceFrequency(float f) { turbFrequency_ = f; }
  void setSpeed(float s) { speed_ = s; }
  void setDrag(float d) { drag_ = d; }
  float turbulenceAmount() const { return turbAmount_; }
  float turbulenceFrequency() const { return turbFrequency_; }
  float speed() const { return speed_; }
  float drag() const { return drag_; }

  MTL::Buffer* resultBuffer() const { return resultPoints_; }
  MTL::Texture* target() const { return target_; }
  uint32_t count() const { return count_; }

 private:
  void runEmit(MTL::CommandQueue* q);
  void runTurbulence(MTL::CommandQueue* q, float time);
  void runSim(MTL::CommandQueue* q, float time, bool emit, bool reset);

  MTL::Device* dev_;
  uint32_t count_, width_, height_;
  float viewExtent_;
  float turbAmount_ = 15.0f;
  float turbFrequency_ = 1.2f;
  float speed_ = 1.0f;
  float drag_ = 0.02f;

  MTL::Buffer* emitPoints_ = nullptr;    // Point[count]
  MTL::Buffer* particles_ = nullptr;     // Particle[count] (sim state)
  MTL::Buffer* resultPoints_ = nullptr;  // Point[count] (renderable)
  MTL::ComputePipelineState* psoEmit_ = nullptr;
  MTL::ComputePipelineState* psoTurb_ = nullptr;
  MTL::ComputePipelineState* psoSim_ = nullptr;
  MTL::RenderPipelineState* rpsDraw_ = nullptr;
  MTL::Texture* target_ = nullptr;
};

// Headless proofs (codex-eyes). Flow: emit ring, run N turbulence+integrate steps,
// assert points moved OFF the perfect ring (turbulence flowed). injectBug=0 amount
// -> no flow -> FAIL. Draw: emit ring, render, assert non-black + center black.
int runParticleFlowSelfTest(bool injectBug);
int runDrawPointsSelfTest(bool injectBug);
}  // namespace sw
