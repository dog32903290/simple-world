// vectorfieldforce_field_golden — --selftest-vectorfieldforce-field. PF-a CLOSED-FORM golden (NOT
// self-consistency). Seeds ONE particle at field-space (0.25,0,0), dispatches the runtime-compiled
// field-into-force COMPUTE kernel ONCE, reads back Velocity, and asserts it == the TiXL-derived closed
// form (external/tixl .../particles/VectorFieldForce-sg.hlsl:60-66 + ToroidalVortexField.cs field math).
// This is the DIRECT verification of f.xyz that the field op's render golden could NOT do — the 2D field
// render template only visualizes f.w (the field_ops_toroidalvortexfield_golden.cpp VERIFICATION CEILING).
//
// Ground truth (field params Radius=0.5, Range=0.5, FallOffRate=2; particle at (0.25,0,0)):
//   phi=atan2(0,0.25)=0; e_r=(1,0,0); e_phi=(0,1,0); C=Radius*e_r=(0.5,0,0); r=p-C=(-0.25,0,0); rho=0.25;
//   x=rho/Range=0.5; decay=saturate(1-x^2)=0.75;
//   vSwirl=normalize(cross(e_phi,r))*(SwirlGain*decay)=normalize((0,0,0.25))*0.75=(0,0,0.75);
//   vRadial=(-r/rho)*(RadialGain*decay)=(1,0,0)*0.75=(0.75,0,0); f.xyz=vSwirl+vRadial=(0.75,0,0.75); f.w=0.75.
//   velocity = f.xyz * Amount * f.w * variationFactor (Variation=0 -> factor=1) = (0.5625*A, 0, 0.5625*A).
// Fingerprint: velocity.x ≈ velocity.z, velocity.y ≈ 0 — the baked (1,1,1) fork-VFF can NEVER produce this
// (it yields x=y=z). injectBug: assert against the BAKED (1,1,1) expectation (= (A,A,A)) instead -> the
// REAL field readback (0.5625A,0,0.5625A) fails it -> RED (the closed-form bites the bake).
//
// ZONE: shell tier (app/src/ root, like field_render_golden.cpp / particlefield_probe_golden.cpp). Crosses
// runtime (assembleFieldMSL, makeFieldNode, the source-compute-PSO cache) AND platform (the field source
// compiler) — exactly what main.cpp does to wire the compiler; runtime selftests may NOT include platform.
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
#include "runtime/field_node_registry.h"  // makeFieldNode (ToroidalVortexField factory)
#include "runtime/particle_params.h"      // FORCE_Particles/Params/FieldParams, VecFieldForceParams
#include "runtime/point_graph.h"          // registerBuiltinPointOps (registers the FieldOp factories)
#include "runtime/tex_op_cache.h"         // cachedSourceComputePSO, clearTexOpCache
#include "runtime/tixl_point.h"           // Particle (64B), SW_PACKED3 / SW_FLOAT4

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the field source compiler)

namespace sw {

// Param-cook seam owned by field_ops_toroidalvortexfield.cpp (leaf type TU-private). Forward-decl so this
// golden can set the EXACT closed-form params (Radius=0.5, Range=0.5, FallOffRate=2). The op's makeFieldNode
// ctor seeds .t3 defaults Radius=1.0/Range=1.0 — a DIFFERENT input set — so the override is required.
void configureToroidalVortexField(FieldNode& node, float centerX, float centerY, float centerZ,
                                  float radius, float range, float swirlGain, float radialGain,
                                  float fallOffRate, int axis, int injectBug);

namespace {
std::string loadVffTemplate() {
#ifdef SW_VFF_TEMPLATE
  std::ifstream f(SW_VFF_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss; ss << f.rdbuf(); return ss.str();
#else
  return "";
#endif
}
}  // namespace

int runVectorFieldForceFieldSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Register the field source compiler (the SAME seam main.cpp wires) and drop any stale PSO.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();
  registerBuiltinPointOps();  // ensures the ToroidalVortexField FieldOp factory is registered

  const std::string tmpl = loadVffTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-vectorfieldforce-field] FAIL: SW_VFF_TEMPLATE unset/unreadable\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // Build a ToroidalVortexField and set the EXACT closed-form params (Center=0, Radius=0.5, Range=0.5,
  // SwirlGain=1, RadialGain=1, FallOffRate=2, Axis=Z(2), injectBug=0).
  std::shared_ptr<FieldNode> field = makeFieldNode("ToroidalVortexField", "vfffield0");
  if (!field) {
    std::printf("[selftest-vectorfieldforce-field] FAIL: ToroidalVortexField factory not registered\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }
  configureToroidalVortexField(*field, /*center*/ 0.0f, 0.0f, 0.0f, /*radius*/ 0.5f, /*range*/ 0.5f,
                               /*swirlGain*/ 1.0f, /*radialGain*/ 1.0f, /*fallOffRate*/ 2.0f,
                               /*axis*/ 2, /*injectBug*/ 0);

  AssembledField asmField = assembleFieldMSL(field, tmpl);
  if (asmField.msl.empty()) {
    std::printf("[selftest-vectorfieldforce-field] FAIL: assembleFieldMSL produced empty MSL\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }
  MTL::ComputePipelineState* pso = cachedSourceComputePSO(
      dev, asmField.msl.c_str(), asmField.srcHash, "vector_field_force");
  if (!pso) {
    std::printf("[selftest-vectorfieldforce-field] FAIL: cachedSourceComputePSO null (compile/link)\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // Field param buffer at FORCE_FieldParams (slot 2). Force params at FORCE_Params (slot 1).
  const float AMOUNT = 4.0f;
  VecFieldForceParams vp{};
  vp.Amount = AMOUNT; vp.Variation = 0.0f; vp.SpeedFactor = 1.0f; vp.Count = 1;

  const size_t paramBytes = asmField.floatParams.empty() ? 16
                                                         : asmField.floatParams.size() * sizeof(float);
  MTL::Buffer* fieldBuf = dev->newBuffer(paramBytes, MTL::ResourceStorageModeShared);
  if (!asmField.floatParams.empty())
    std::memcpy(fieldBuf->contents(), asmField.floatParams.data(),
                asmField.floatParams.size() * sizeof(float));

  // One particle at field-space (0.25,0,0), zero velocity, emitted (BirthTime=0 -> passes NaN guard).
  MTL::Buffer* parts = dev->newBuffer(sizeof(Particle), MTL::ResourceStorageModeShared);
  Particle* p = static_cast<Particle*>(parts->contents());
  *p = Particle{};
  p->Position = SW_PACKED3{0.25f, 0.0f, 0.0f};
  p->Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  p->Velocity = SW_PACKED3{0.0f, 0.0f, 0.0f};
  p->BirthTime = 0.0f;

  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(parts, 0, FORCE_Particles);
  enc->setBytes(&vp, sizeof(vp), FORCE_Params);
  enc->setBuffer(fieldBuf, 0, FORCE_FieldParams);
  enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(64, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();

  Particle out{};
  std::memcpy(&out, parts->contents(), sizeof(Particle));
  const float vx = out.Velocity.x, vy = out.Velocity.y, vz = out.Velocity.z;

  // Closed-form expectation: (0.5625*A, 0, 0.5625*A).
  const float kFactor = 0.5625f;
  float ex = kFactor * AMOUNT, ey = 0.0f, ez = kFactor * AMOUNT;
  if (injectBug) {  // assert against the BAKED (1,1,1) fork instead -> the real field readback fails it.
    ex = AMOUNT; ey = AMOUNT; ez = AMOUNT;
  }
  const float kEps = 1e-3f;
  bool pass = std::fabs(vx - ex) < kEps && std::fabs(vy - ey) < kEps && std::fabs(vz - ez) < kEps;

  std::printf("[selftest-vectorfieldforce-field] Amount=%.1f  readback Velocity=(% .5f,% .5f,% .5f)  "
              "expected=(% .5f,% .5f,% .5f)%s  -> %s\n",
              AMOUNT, vx, vy, vz, ex, ey, ez,
              injectBug ? " [BAKED expectation -bug]" : " [closed form 0.5625A,0,0.5625A]",
              pass ? "PASS" : "FAIL");

  parts->release(); fieldBuf->release();
  if (q) q->release(); if (dev) dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
