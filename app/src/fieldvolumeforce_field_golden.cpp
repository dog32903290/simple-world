// fieldvolumeforce_field_golden — --selftest-fieldvolumeforce-field. PF field-into-force CLOSED-FORM golden
// (NOT self-consistency). Seeds ONE particle at (1,0,0) with ZERO velocity, dispatches the runtime-compiled
// field-into-force COMPUTE kernel (field_volume_force_template.metal) ONCE against a wired SphereSDF field,
// reads back Velocity, and asserts it == the TiXL-derived closed form (external/tixl
// .../particles/FieldVolumeForce.hlsl:91-151 + SphereSDF.cs distance math + the FieldVolumeForce.t3
// FloatsToBuffer *0.425 Attraction routing fork).
//
// Ground truth (SphereSDF Center=(0,0,0), Radius=0.5; particle at (1,0,0), velocity=0; .cs Attraction=1,
// Amount=A=4, AttractionDecay=0, Repulsion=0.1, InvertVolume=false, ReflectOnCollision=true). Derivation —
// every step is FieldVolumeForce.hlsl, NOT read off our own kernel:
//   distance     = GetDistance((1,0,0)) = |(1,0,0)| - 0.5 = 0.5            (hlsl:103 / SphereSDF: |p-C|-R)
//   surfaceN     = GetNormal((1,0,0))   = (1,0,0)                          (hlsl:104; sphere SDF gradient = radial
//                  unit; the 4 tetrahedral taps converge to the outward radial, same as FieldDistance's normal)
//   velocity     = (0,0,0)                                                 (seed)
//   posNext      = (1,0,0) + velocity*SpeedFactor*0.01*2 = (1,0,0)         (hlsl:108; velocity is 0 -> no move)
//   distanceNext = GetDistance((1,0,0)) = 0.5                              (hlsl:109)
//   surfaceN    *= InvertVolumeFactor(=+1, InvertVolume=false)  -> (1,0,0) (hlsl:112)
//   crossing?    sign(distance*distanceNext) = sign(0.25) = +1  (NOT < 0) -> NO reflection -> else branch (hlsl:115)
//   distance*InvertVolumeFactor = 0.5 (NOT < 0) -> ATTRACT branch (hlsl:143-145):
//     force = -surfaceN * Attraction / (1 + distance*AttractionDecay) = -(1,0,0)*Attraction / (1+0)
//   *** ROUTING FORK (FieldVolumeForce.t3) ***: shader Attraction = (.cs Attraction) * 0.425 (a Multiply node
//   B=0.425 sits on the Attraction path; the cook applies it in fillFieldVolumeForceParams). So with .cs
//   Attraction=1 -> shader Attraction = 0.425 -> force = (-0.425, 0, 0).
//   velocity += force * Amount * SpeedFactor = (0,0,0) + (-0.425,0,0)*4*1 = (-1.7, 0, 0)   (hlsl:146)
//   NaN guard (hlsl:149) passes -> Velocity = (-1.7, 0, 0).
//   With A=4 -> Velocity = (-0.425*A, 0, 0) = (-1.7, 0, 0).
// Fingerprint: Velocity.x ≈ -1.7, Velocity.y ≈ 0, Velocity.z ≈ 0. This bites TWO ways: (1) the BAKED (.w=1
// everywhere) severed-field fork can NEVER produce it (a constant distance field has zero gradient ->
// GetNormal = normalize(0) = NaN -> force NaN -> the final isnan(velocity) guard returns with NO change ->
// Velocity stays (0,0,0)); (2) a naive 1:1 port that FORGOT the *0.425 Attraction fork would compute
// (-4,0,0), failing the (-1.7,0,0) assertion. injectBug models the SEVERED field / baked fallback by asserting
// against the BAKED expectation (0,0,0) instead -> the REAL field readback (-1.7,0,0) fails it -> RED.
// (Mirror of fielddistanceforce_field_golden's -bug, which likewise swaps the expectation to the baked fork
// rather than re-running with no field, since assembleFieldMSL requires a non-null tree to emit the kernel.)
//
// ZONE: shell tier (app/src/ root, like fielddistanceforce_field_golden.cpp). Crosses runtime (assembleFieldMSL,
// makeFieldNode, the source-compute-PSO cache) AND platform (the field source compiler) — exactly what main.cpp
// does to wire the compiler; runtime selftests may NOT include platform.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_graph.h"          // setFieldSourceCompiler, assembleFieldMSL, AssembledField, FieldNode
#include "runtime/field_node_registry.h"  // makeFieldNode (SphereSDF factory)
#include "runtime/particle_params.h"      // FORCE_Particles/Params/FieldParams, FieldVolumeForceParams (via force_params.h)
#include "runtime/point_graph.h"          // registerBuiltinPointOps (registers the FieldOp factories)
#include "runtime/tex_op_cache.h"         // cachedSourceComputePSO, clearTexOpCache
#include "runtime/tixl_point.h"           // Particle (64B), SW_PACKED3 / SW_FLOAT4

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the field source compiler)

namespace sw {
namespace {
std::string loadFieldVolumeTemplate() {
#ifdef SW_FIELD_VOLUME_TEMPLATE
  std::ifstream f(SW_FIELD_VOLUME_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss; ss << f.rdbuf(); return ss.str();
#else
  return "";
#endif
}
}  // namespace

int runFieldVolumeForceFieldSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Register the field source compiler (the SAME seam main.cpp wires) and drop any stale PSO. CRITICAL:
  // without this the source-compute-PSO cache returns null -> a silent baked fallback (no field), and the
  // PASS path would never run the real field kernel.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();
  registerBuiltinPointOps();  // ensures the SphereSDF FieldOp factory is registered

  const std::string tmpl = loadFieldVolumeTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-fieldvolumeforce-field] FAIL: SW_FIELD_VOLUME_TEMPLATE unset/unreadable\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // SphereSDF (Center=0, Radius=0.5 — the SphereSDFNode ctor defaults ARE the closed-form params, mirroring
  // SphereSDF.t3, so no override is needed).
  std::shared_ptr<FieldNode> field = makeFieldNode("SphereSDF", "fvfsphere0");
  if (!field) {
    std::printf("[selftest-fieldvolumeforce-field] FAIL: SphereSDF factory not registered\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // Always assemble against the REAL SphereSDF tree so the kernel compiles and SAMPLES the field (both PASS
  // and -bug run the identical field kernel). injectBug bites later by swapping the EXPECTED value to the
  // baked (severed-field) fork (0,0,0) — the real readback (-0.425A,0,0) then fails it -> RED.
  AssembledField asmField = assembleFieldMSL(field, tmpl);
  if (asmField.msl.empty()) {
    std::printf("[selftest-fieldvolumeforce-field] FAIL: assembleFieldMSL produced empty MSL\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }
  MTL::ComputePipelineState* pso = cachedSourceComputePSO(
      dev, asmField.msl.c_str(), asmField.srcHash, "field_volume_force");
  if (!pso) {
    std::printf("[selftest-fieldvolumeforce-field] FAIL: cachedSourceComputePSO null (compile/link)\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // Force params at FORCE_Params (slot 1). NOTE these are the ALREADY-FORKED kernel values (the cook applies
  // the .t3 routing forks host-side, so the golden mirrors that): Attraction is the SHADER value 0.425 (==
  // .cs Attraction 1.0 * 0.425), InvertVolumeFactor=+1 (InvertVolume=false), SpeedFactor=1.0. Repulsion is
  // never reached (the attract branch fires) but set to the .t3 default 0.1 for fidelity.
  const float AMOUNT = 4.0f;
  const float SHADER_ATTRACTION = 1.0f * 0.425f;  // .cs Attraction (1.0) * the .t3 Multiply fork (0.425)
  FieldVolumeForceParams fp{};
  fp.Amount = AMOUNT;
  fp.Attraction = SHADER_ATTRACTION;
  fp.AttractionDecay = 0.0f;
  fp.Repulsion = 0.1f;
  fp.Bounciness = 1.0f;
  fp.RandomizeBounce = 0.0f;
  fp.RandomizeReflection = 0.0f;
  fp.InvertVolumeFactor = 1.0f;          // InvertVolume=false -> +1
  fp.NormalSamplingDistance = 0.001f;    // small h -> the 4-tap normal converges to the exact radial (1,0,0)
  fp.SpeedFactor = 1.0f;                 // fork-FieldVolume-speedfactor
  fp.EnableBounce = 1.0f;                // ReflectOnCollision=true (irrelevant: no crossing this step)
  fp.ApplyColorOnCollision = 0.0f;
  fp.Count = 1;

  // Field FloatParams buffer at FORCE_FieldParams (slot 2). >=16 bytes even for a zero-param field.
  const size_t paramBytes = asmField.floatParams.empty() ? 16
                                                         : asmField.floatParams.size() * sizeof(float);
  MTL::Buffer* fieldBuf = dev->newBuffer(paramBytes, MTL::ResourceStorageModeShared);
  if (!asmField.floatParams.empty())
    std::memcpy(fieldBuf->contents(), asmField.floatParams.data(),
                asmField.floatParams.size() * sizeof(float));

  // One particle at (1,0,0), zero velocity, emitted (BirthTime=0 -> passes NaN guard).
  MTL::Buffer* parts = dev->newBuffer(sizeof(Particle), MTL::ResourceStorageModeShared);
  Particle* p = static_cast<Particle*>(parts->contents());
  *p = Particle{};
  p->Position = SW_PACKED3{1.0f, 0.0f, 0.0f};
  p->Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  p->Velocity = SW_PACKED3{0.0f, 0.0f, 0.0f};
  p->BirthTime = 0.0f;

  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(parts, 0, FORCE_Particles);
  enc->setBytes(&fp, sizeof(fp), FORCE_Params);
  enc->setBuffer(fieldBuf, 0, FORCE_FieldParams);
  enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(64, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();

  Particle out{};
  std::memcpy(&out, parts->contents(), sizeof(Particle));
  const float vx = out.Velocity.x, vy = out.Velocity.y, vz = out.Velocity.z;

  // Closed-form expectation (PASS): Velocity = (-0.425*A, 0, 0) = (-shaderAttraction*A, 0, 0).
  float ex = -SHADER_ATTRACTION * AMOUNT, ey = 0.0f, ez = 0.0f;
  if (injectBug) {  // assert against the BAKED (severed-field) fork (0,0,0) -> the real field readback fails it.
    ex = 0.0f; ey = 0.0f; ez = 0.0f;
  }
  const float kEps = 1e-3f;
  bool pass = std::fabs(vx - ex) < kEps && std::fabs(vy - ey) < kEps && std::fabs(vz - ez) < kEps;

  std::printf("[selftest-fieldvolumeforce-field] Amount=%.1f shaderAttraction=%.3f (=.cs 1.0 * 0.425 fork)  "
              "readback Velocity=(% .5f,% .5f,% .5f)  expected=(% .5f,% .5f,% .5f)%s  -> %s\n",
              AMOUNT, SHADER_ATTRACTION, vx, vy, vz, ex, ey, ez,
              injectBug ? " [BAKED expectation -bug (severed field -> (0,0,0))]"
                        : " [closed form (-0.425*A,0,0)]",
              pass ? "PASS" : "FAIL");

  parts->release(); fieldBuf->release();
  if (q) q->release(); if (dev) dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
