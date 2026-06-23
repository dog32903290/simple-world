// fielddistanceforce_field_golden — --selftest-fielddistanceforce-field. PF field-into-force CLOSED-FORM
// golden (NOT self-consistency). Seeds ONE particle at (1,0,0), dispatches the runtime-compiled
// field-into-force COMPUTE kernel (field_distance_force_template.metal) ONCE against a wired SphereSDF
// field, reads back Velocity, and asserts it == the TiXL-derived closed form
// (external/tixl .../particles/FieldDistanceForce.hlsl:74-101 + SphereSDF.cs distance math).
// This is the DIRECT verification of the SDF-distance attract/repel that the field op's render golden
// could NOT do — the 2D field render template only visualizes f.w (the field generator VERIFICATION CEILING).
//
// Ground truth (SphereSDF Center=(0,0,0), Radius=0.5; particle at (1,0,0); Amount=A=4, Attraction=1,
// Repulsion=0, DecayWithDistance=0, NormalSamplingDistance small):
//   d = GetDistance((1,0,0)) = |(1,0,0)| - 0.5 = 0.5 > 0  -> ATTRACT.
//   GetFieldNormal is the tetrahedral 4-tap finite difference of the SDF distance; for a sphere SDF the
//   gradient is the outward radial unit vector, so n((1,0,0)) = (1,0,0) (the 4 symmetric taps converge to it).
//   decay = pow(d+1, -DecayWithDistance) = pow(1.5, -0) = 1.
//   velocity -= n * Attraction * Amount * decay  ->  Velocity = (0,0,0) - (1,0,0)*1*A*1 = (-A, 0, 0).
//   With A=4 -> Velocity = (-4, 0, 0).
// Fingerprint: Velocity.x ≈ -4, Velocity.y ≈ 0, Velocity.z ≈ 0 — the BAKED (.w=1 everywhere) fallback can
// NEVER produce this (a constant distance field has zero gradient -> normalize(0)=NaN -> the isnan(n.x)
// guard returns with NO velocity change -> Velocity stays (0,0,0)). injectBug models the SEVERED field /
// baked fallback by asserting against the BAKED expectation (0,0,0) instead -> the REAL field readback
// (-A,0,0) fails it -> RED (the closed-form bites the bake). (Mirror of vectorfieldforce_field_golden's
// -bug, which likewise swaps the expectation to the baked fork rather than re-running with no field, since
// assembleFieldMSL requires a non-null tree to emit the kernel.)
//
// ZONE: shell tier (app/src/ root, like vectorfieldforce_field_golden.cpp). Crosses runtime (assembleFieldMSL,
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
#include "runtime/particle_params.h"      // FORCE_Particles/Params/FieldParams, FieldDistForceParams
#include "runtime/point_graph.h"          // registerBuiltinPointOps (registers the FieldOp factories)
#include "runtime/tex_op_cache.h"         // cachedSourceComputePSO, clearTexOpCache
#include "runtime/tixl_point.h"           // Particle (64B), SW_PACKED3 / SW_FLOAT4

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the field source compiler)

namespace sw {
namespace {
std::string loadFieldDistanceTemplate() {
#ifdef SW_FIELD_DISTANCE_TEMPLATE
  std::ifstream f(SW_FIELD_DISTANCE_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss; ss << f.rdbuf(); return ss.str();
#else
  return "";
#endif
}
}  // namespace

int runFieldDistanceForceFieldSelfTest(bool injectBug) {
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

  const std::string tmpl = loadFieldDistanceTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-fielddistanceforce-field] FAIL: SW_FIELD_DISTANCE_TEMPLATE unset/unreadable\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // Build a SphereSDF (Center=0, Radius=0.5 — the SphereSDFNode ctor defaults ARE the closed-form params,
  // mirroring SphereSDF.t3, so no override is needed).
  std::shared_ptr<FieldNode> field = makeFieldNode("SphereSDF", "fdfsphere0");
  if (!field) {
    std::printf("[selftest-fielddistanceforce-field] FAIL: SphereSDF factory not registered\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // Always assemble against the REAL SphereSDF tree so the kernel compiles and SAMPLES the field (both PASS
  // and -bug run the identical field kernel). injectBug bites later by swapping the EXPECTED value to the
  // baked (severed-field) fork (0,0,0) — the real readback (-A,0,0) then fails it -> RED.
  AssembledField asmField = assembleFieldMSL(field, tmpl);
  if (asmField.msl.empty()) {
    std::printf("[selftest-fielddistanceforce-field] FAIL: assembleFieldMSL produced empty MSL\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }
  MTL::ComputePipelineState* pso = cachedSourceComputePSO(
      dev, asmField.msl.c_str(), asmField.srcHash, "field_distance_force");
  if (!pso) {
    std::printf("[selftest-fielddistanceforce-field] FAIL: cachedSourceComputePSO null (compile/link)\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // Force params at FORCE_Params (slot 1): Amount=4, Attraction=1, Repulsion=0, DecayWithDistance=0.
  const float AMOUNT = 4.0f;
  FieldDistForceParams fp{};
  fp.Amount = AMOUNT;
  fp.Attraction = 1.0f;
  fp.Repulsion = 0.0f;
  fp.NormalSamplingDistance = 0.001f;
  fp.DecayWithDistance = 0.0f;
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

  // Closed-form expectation (PASS): Velocity = (-A, 0, 0).
  float ex = -AMOUNT, ey = 0.0f, ez = 0.0f;
  if (injectBug) {  // assert against the BAKED (severed-field) fork (0,0,0) -> the real field readback fails it.
    ex = 0.0f; ey = 0.0f; ez = 0.0f;
  }
  const float kEps = 1e-3f;
  bool pass = std::fabs(vx - ex) < kEps && std::fabs(vy - ey) < kEps && std::fabs(vz - ez) < kEps;

  std::printf("[selftest-fielddistanceforce-field] Amount=%.1f  readback Velocity=(% .5f,% .5f,% .5f)  "
              "expected=(% .5f,% .5f,% .5f)%s  -> %s\n",
              AMOUNT, vx, vy, vz, ex, ey, ez,
              injectBug ? " [BAKED expectation -bug (severed field -> (0,0,0))]" : " [closed form (-A,0,0)]",
              pass ? "PASS" : "FAIL");

  parts->release(); fieldBuf->release();
  if (q) q->release(); if (dev) dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
