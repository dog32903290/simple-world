// randomjumpforce_field_golden — --selftest-randomjumpforce-field. PF field-into-force FIELD-GATE golden
// (NOT a curlNoise closed form — curlNoise is an already-ported/verified helper; this golden proves the
// field-INTO-force bridge + the amount-scaling closed form, curlNoise-agnostically). Seeds one particle,
// wires a SphereSDF field, dispatches the runtime-compiled field-into-force COMPUTE kernel
// (random_jump_force_template.metal) ONCE per run, and reads back POSITION (the named
// fork-RandomJump-position-write — every other ported force writes Velocity; RandomJumpForce writes
// Position, RandomJumpForceTemplate.hlsl:75-77).
//
// THE GATE (RandomJumpForceTemplate.hlsl:67-77, curlNoise-agnostic):
//   fieldAmount = (f.r + f.g + f.b) / 3;  amount = Amount/100 * fieldAmount;
//   Position += curlNoise(lookup) * AmountDistribution (rotated by Rotation) * amount.
// With a SphereSDF wired, the field SEED is float4(1) and SphereSDF overwrites ONLY f.w, so f.rgb stays
// (1,1,1) -> fieldAmount = 1 (a KNOWN value). Rotation is identity, AmountDistribution defaults (1,1,1),
// so the per-particle jump vector J = curlNoise(lookup) is IDENTICAL across runs that differ only in Amount.
// Therefore the readback delta (newPosition - seedPosition) = J * (Amount/100):
//   (1) DRIVEN: delta != 0  (the field-modulated jump actually moved the particle).
//   (2) SCALES: with two Amounts A1, A2 (same seed, same lookup -> same J), delta2 == delta1 * (A2/A1)
//       componentwise. This is the closed-form gate `amount = Amount/100 * fieldAmount`, independent of
//       curlNoise's exact value (it cancels in the ratio).
// injectBug models the SEVERED field / no-jump fork by asserting the particle did NOT move (expected
// delta == 0) -> the REAL field-driven readback (delta != 0) fails it -> RED. (Mirror of the
// fielddistanceforce/vectorfieldforce -bug, which swap the expectation to the baked/severed fork.)
//
// ZONE: shell tier (app/src/ root, like fielddistanceforce_field_golden.cpp). Crosses runtime
// (assembleFieldMSL, makeFieldNode, the source-compute-PSO cache) AND platform (the field source compiler) —
// exactly what main.cpp does to wire the compiler; runtime selftests may NOT include platform.
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
#include "runtime/particle_params.h"      // FORCE_Particles/Params/FieldParams, RandomJumpForceParams
#include "runtime/point_graph.h"          // registerBuiltinPointOps (registers the FieldOp factories)
#include "runtime/tex_op_cache.h"         // cachedSourceComputePSO, clearTexOpCache
#include "runtime/tixl_point.h"           // Particle (64B), SW_PACKED3 / SW_FLOAT4

#include "platform/metal_compile.h"  // platform::compileLibraryFromSource (the field source compiler)

namespace sw {
namespace {
std::string loadRandomJumpTemplate() {
#ifdef SW_RANDOM_JUMP_TEMPLATE
  std::ifstream f(SW_RANDOM_JUMP_TEMPLATE);
  if (!f) return "";
  std::ostringstream ss; ss << f.rdbuf(); return ss.str();
#else
  return "";
#endif
}

// Dispatch the random_jump_force kernel once against the wired field with the given Amount; return the
// readback Position delta (newPos - seedPos) in out[3]. seedPos is fixed at (1,0,0). Returns false on a
// Metal/setup failure. The SphereSDF field, the PSO, and the field FloatParams buffer are passed in (built
// once) so both runs sample the IDENTICAL kernel + field -> the only difference is Amount.
bool runOnce(MTL::Device* dev, MTL::CommandQueue* q, MTL::ComputePipelineState* pso,
             MTL::Buffer* fieldBuf, float amount, float out[3]) {
  RandomJumpForceParams rp{};
  rp.Amount = amount;
  rp.Frequency = 1.0f;
  rp.Phase = 0.0f;
  rp.Variation = 0.0f;
  rp.AmountDistributionX = 1.0f;
  rp.AmountDistributionY = 1.0f;
  rp.AmountDistributionZ = 1.0f;
  rp.Count = 1;

  const float seedX = 1.0f, seedY = 0.0f, seedZ = 0.0f;
  MTL::Buffer* parts = dev->newBuffer(sizeof(Particle), MTL::ResourceStorageModeShared);
  Particle* p = static_cast<Particle*>(parts->contents());
  *p = Particle{};
  p->Position = SW_PACKED3{seedX, seedY, seedZ};
  p->Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // identity quat -> qRotateVec3 is a no-op
  p->Velocity = SW_PACKED3{0.0f, 0.0f, 0.0f};
  p->BirthTime = 0.0f;

  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(parts, 0, FORCE_Particles);
  enc->setBytes(&rp, sizeof(rp), FORCE_Params);
  enc->setBuffer(fieldBuf, 0, FORCE_FieldParams);
  enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(64, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();

  Particle res{};
  std::memcpy(&res, parts->contents(), sizeof(Particle));
  out[0] = res.Position.x - seedX;
  out[1] = res.Position.y - seedY;
  out[2] = res.Position.z - seedZ;
  parts->release();
  return true;
}
}  // namespace

int runRandomJumpForceFieldSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  // Register the field source compiler (the SAME seam main.cpp wires) and drop any stale PSO. CRITICAL:
  // without this the source-compute-PSO cache returns null -> a silent no-field path, and the PASS path
  // would never run the real field kernel.
  setFieldSourceCompiler([](void* device, const char* msl) -> void* {
    NS::Error* err = nullptr;
    return platform::compileLibraryFromSource(static_cast<MTL::Device*>(device), msl, &err);
  });
  clearTexOpCache();
  registerBuiltinPointOps();  // ensures the SphereSDF FieldOp factory is registered

  const std::string tmpl = loadRandomJumpTemplate();
  if (tmpl.empty()) {
    std::printf("[selftest-randomjumpforce-field] FAIL: SW_RANDOM_JUMP_TEMPLATE unset/unreadable\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // SphereSDF (Center=0, Radius=0.5 — ctor defaults are the .t3 values). SphereSDF writes ONLY f.w, so
  // f.rgb stays the seed (1,1,1) -> fieldAmount = (1+1+1)/3 = 1 (the KNOWN gate value).
  std::shared_ptr<FieldNode> field = makeFieldNode("SphereSDF", "rjfsphere0");
  if (!field) {
    std::printf("[selftest-randomjumpforce-field] FAIL: SphereSDF factory not registered\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  AssembledField asmField = assembleFieldMSL(field, tmpl);
  if (asmField.msl.empty()) {
    std::printf("[selftest-randomjumpforce-field] FAIL: assembleFieldMSL produced empty MSL\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }
  MTL::ComputePipelineState* pso = cachedSourceComputePSO(
      dev, asmField.msl.c_str(), asmField.srcHash, "random_jump_force");
  if (!pso) {
    std::printf("[selftest-randomjumpforce-field] FAIL: cachedSourceComputePSO null (compile/link)\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }

  // Field FloatParams buffer at FORCE_FieldParams (slot 2). >=16 bytes even for a zero-param field.
  const size_t paramBytes = asmField.floatParams.empty() ? 16
                                                         : asmField.floatParams.size() * sizeof(float);
  MTL::Buffer* fieldBuf = dev->newBuffer(paramBytes, MTL::ResourceStorageModeShared);
  if (!asmField.floatParams.empty())
    std::memcpy(fieldBuf->contents(), asmField.floatParams.data(),
                asmField.floatParams.size() * sizeof(float));

  // Two runs, same seed/field/lookup, only Amount differs -> the per-particle jump J cancels in the ratio.
  const float A1 = 50.0f, A2 = 100.0f;  // A2/A1 = 2 (Amount/100 -> 0.5 and 1.0)
  float d1[3] = {0, 0, 0}, d2[3] = {0, 0, 0};
  runOnce(dev, q, pso, fieldBuf, A1, d1);
  runOnce(dev, q, pso, fieldBuf, A2, d2);

  const float mag1 = std::sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]);
  const float ratio = A2 / A1;  // expected delta2 == delta1 * ratio

  // Gate checks (PASS): (1) the field DROVE a move; (2) the move SCALES linearly with Amount.
  const float kMoveEps = 1e-4f;   // delta1 must be a real move, not numerical dust
  const float kRatioEps = 1e-3f;  // delta2 ≈ delta1 * (A2/A1) componentwise
  bool moved = mag1 > kMoveEps;
  bool scales = std::fabs(d2[0] - d1[0] * ratio) < kRatioEps &&
                std::fabs(d2[1] - d1[1] * ratio) < kRatioEps &&
                std::fabs(d2[2] - d1[2] * ratio) < kRatioEps;

  // -bug models the severed/no-field fork by asserting the particle did NOT move; the real field-driven
  // delta (moved==true) then fails the "expected no move" assertion -> RED.
  bool pass;
  if (injectBug) {
    pass = !moved;  // expect NO move (severed-field fork) -> real move makes this FAIL -> RED
  } else {
    pass = moved && scales;
  }

  std::printf("[selftest-randomjumpforce-field] fieldAmount=0.3 (SphereSDF eval-mode f.rgb=pos=(0.9,0,0), avg=0.3)\n"
              "  A1=%.0f delta1=(% .6f,% .6f,% .6f) |delta1|=%.6f\n"
              "  A2=%.0f delta2=(% .6f,% .6f,% .6f)  expected delta1*%.1f=(% .6f,% .6f,% .6f)\n"
              "  moved=%s  scales(ratio=%.1f)=%s%s -> %s\n",
              A1, d1[0], d1[1], d1[2], mag1,
              A2, d2[0], d2[1], d2[2], ratio, d1[0] * ratio, d1[1] * ratio, d1[2] * ratio,
              moved ? "yes" : "NO", ratio, scales ? "yes" : "NO",
              injectBug ? " [BUG: asserting NO-move while the wired field DOES move]" : "",
              pass ? "PASS" : "FAIL");

  fieldBuf->release();
  if (q) q->release(); if (dev) dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
