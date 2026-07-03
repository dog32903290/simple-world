// randomjumpforce_field_golden — --selftest-randomjumpforce-field. PF field-into-force FIELD-GATE golden
// (NOT a curlNoise closed form — curlNoise is an already-ported/verified helper; this golden proves the
// field-INTO-force bridge + the amount-scaling closed form, curlNoise-DIRECTION-agnostically). Seeds one
// particle, wires a SphereSDF field, dispatches the runtime-compiled field-into-force COMPUTE kernel
// (random_jump_force_template.metal) ONCE per run, and reads back POSITION (the named
// fork-RandomJump-position-write — every other ported force writes Velocity; RandomJumpForce writes
// Position, RandomJumpForceTemplate.hlsl:77).
//
// ── THE GATE, hand-derived from TiXL source (repo tixl3d/tixl @ locked SHA 395c4c55) ─────────────────
// RandomJumpForceTemplate.hlsl:
//   :62  pos = Position * 0.9                  -> seed (1,0,0) gives pos = (0.9, 0, 0)
//   :67  field = GetField(float4(pos, 0))      -> p.w = 0 = field-eval mode
//   :68  fieldAmount = (f.r + f.g + f.b) / 3
//   :70  amount = Amount / 100 * fieldAmount
//   :71  noise3 = curlNoise(noiseLookup)       -> noise-functions.hlsl:282 `return normalize(...)`
//                                                 => |noise3| == 1 EXACTLY (unit vector)
//   :74  noise3 *= AmountDistribution          -> (1,1,1) default => length preserved
//   :75  noise3 = qRotateVec3(noise3, normalize(p.Rotation)) -> unit-quat rotation = isometry => length 1
//   :77  Position += noise3 * amount
// SphereSDF.cs:35-36 (GetPreShaderCode; sw mirror field_ops_spheresdf.cpp preShaderCode):
//   f.w   = length(p.xyz - Center) - Radius;
//   f.xyz = p.w < 0.5 ? p.xyz : 1;   // field-eval mode (p.w=0) SAVES THE SAMPLE POS into f.rgb
// => with SphereSDF wired, f.rgb = pos = (0.9, 0, 0), so fieldAmount = (0.9+0+0)/3 = 0.3 (a KNOWN
//    constant — NOT 1; the template seed float4(1) survives only when NO field call is emitted).
// CLOSED FORM (independent of curlNoise's direction, which cancels in |.| and in the ratio):
//   |delta| = |noise3 * amount| = amount = Amount/100 * 0.3
//   A1 = 50  -> |delta1| = 0.5 * 0.3 = 0.15
//   A2 = 100 -> |delta2| = 1.0 * 0.3 = 0.30
// ASSERTS (one batch, same expectations in BOTH legs — no want-flip):
//   (1) DRIVEN:  |delta1| > eps (the field-modulated jump actually moved the particle).
//   (2) SCALES:  delta2 == delta1 * (A2/A1) componentwise (the linear Amount gate; direction cancels).
//   (3) ABS-1:   |delta1| == 0.15  — pins the /100 AND the fieldAmount=0.3 constants ABSOLUTELY
//   (4) ABS-2:   |delta2| == 0.30    (a ratio-only gate is mathematically blind to constant factors:
//                                     dropping /100 or mis-seeding fieldAmount scales BOTH runs and
//                                     still passes (1)+(2); (3)+(4) are the constant-factor teeth).
// injectBug LEG (REAL injection, GOLDEN_STANDARD 特徵3): configureSphereSdfBug(field, 1) — the SphereSDF
// leaf's test seam (field_ops_spheresdf.cpp) drops the REAL preShaderCode emit, so assembleFieldMSL
// produces an empty FIELD_CALL -> f stays the all-ones seed -> fieldAmount = (1+1+1)/3 = 1 -> the REAL
// compiled+dispatched kernel jumps |delta1| = 0.5, |delta2| = 1.0 -> asserts (3)+(4) diverge -> RED.
// (1)+(2) stay green under this bug — living proof the old ratio-only gate could not see it. If the
// injection does not trip (all asserts still pass), the leg returns 0 so the --bite NO-BITE list
// catches the dead tooth.
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

// Test seam owned by field_ops_spheresdf.cpp (leaf type TU-private; mirrors the vectorfieldforce_field
// golden's configureToroidalVortexField forward-decl). injectBug: 0 = none, 1 = drop the field-call emit
// -> f stays the all-ones template seed (the severed-field regression this golden's -bug leg models).
void configureSphereSdfBug(FieldNode& node, int injectBug);

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

  // SphereSDF (Center=0, Radius=0.5 — ctor defaults are the .t3 values). In field-eval mode (p.w=0)
  // SphereSDF writes f.xyz = p.xyz (SphereSDF.cs:36 "save local space") -> f.rgb = pos = (0.9,0,0) ->
  // fieldAmount = (0.9+0+0)/3 = 0.3 (the KNOWN gate constant; see the header derivation).
  std::shared_ptr<FieldNode> field = makeFieldNode("SphereSDF", "rjfsphere0");
  if (!field) {
    std::printf("[selftest-randomjumpforce-field] FAIL: SphereSDF factory not registered\n");
    if (q) q->release(); if (dev) dev->release(); pool->release(); return 1;
  }
  // -bug REAL injection (before assembly — the corruption flows through the REAL codegen->compile->
  // dispatch path): drop the SphereSDF field-call emit -> f stays seed (1,1,1,1) -> fieldAmount = 1.
  configureSphereSdfBug(*field, injectBug ? 1 : 0);

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

  // Two runs, same seed/field/lookup, only Amount differs -> the per-particle jump direction J cancels in
  // the ratio AND in the magnitudes (|J| == 1, noise-functions.hlsl:282 normalize).
  const float A1 = 50.0f, A2 = 100.0f;  // A2/A1 = 2 (Amount/100 -> 0.5 and 1.0)
  float d1[3] = {0, 0, 0}, d2[3] = {0, 0, 0};
  runOnce(dev, q, pso, fieldBuf, A1, d1);
  runOnce(dev, q, pso, fieldBuf, A2, d2);

  const float mag1 = std::sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]);
  const float mag2 = std::sqrt(d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2]);
  const float ratio = A2 / A1;  // expected delta2 == delta1 * ratio

  // TiXL-derived constants (header derivation; RandomJumpForceTemplate.hlsl:62,67-70,77 +
  // SphereSDF.cs:36 + noise-functions.hlsl:282):
  const float kFieldAmount = 0.3f;                    // (0.9+0+0)/3 — SphereSDF saves pos into f.rgb
  const float kExpMag1 = A1 / 100.0f * kFieldAmount;  // = 0.15
  const float kExpMag2 = A2 / 100.0f * kFieldAmount;  // = 0.30

  // Gate checks — ONE batch, identical expectations in BOTH legs (no want-flip):
  const float kMoveEps = 1e-4f;   // delta1 must be a real move, not numerical dust
  const float kRatioEps = 1e-3f;  // delta2 ≈ delta1 * (A2/A1) componentwise
  const float kMagEps = 1e-4f;    // |delta| ≈ Amount/100 * 0.3 (float budget « eps; bug diverges 3.3×)
  bool moved = mag1 > kMoveEps;
  bool scales = std::fabs(d2[0] - d1[0] * ratio) < kRatioEps &&
                std::fabs(d2[1] - d1[1] * ratio) < kRatioEps &&
                std::fabs(d2[2] - d1[2] * ratio) < kRatioEps;
  bool mag1ok = std::fabs(mag1 - kExpMag1) < kMagEps;  // ABS pin: /100 and fieldAmount=0.3
  bool mag2ok = std::fabs(mag2 - kExpMag2) < kMagEps;
  bool pass = moved && scales && mag1ok && mag2ok;

  std::printf("[selftest-randomjumpforce-field] fieldAmount=0.3 (SphereSDF.cs:36 field-eval f.rgb=pos=(0.9,0,0), avg=0.3)\n"
              "  A1=%.0f delta1=(% .6f,% .6f,% .6f) |delta1|=%.6f expected=%.6f -> %s\n"
              "  A2=%.0f delta2=(% .6f,% .6f,% .6f) |delta2|=%.6f expected=%.6f -> %s\n"
              "  moved=%s  scales(ratio=%.1f)=%s%s -> %s\n",
              A1, d1[0], d1[1], d1[2], mag1, kExpMag1, mag1ok ? "ok" : "RED",
              A2, d2[0], d2[1], d2[2], mag2, kExpMag2, mag2ok ? "ok" : "RED",
              moved ? "yes" : "NO", ratio, scales ? "yes" : "NO",
              injectBug ? " [BUG: SphereSDF field-call emit dropped -> fieldAmount=1 -> |delta| 3.3x]" : "",
              pass ? "PASS" : "FAIL");

  fieldBuf->release();
  if (q) q->release(); if (dev) dev->release(); pool->release();

  if (injectBug) {
    if (!pass) return 1;  // the severed-field corruption diverged the ABS pins -> RED (tooth bit)
    std::printf("[selftest-randomjumpforce-field] injectBug did not trip (asserts all green under a "
                "severed field) -> NO-BITE\n");
    return 0;  // dead tooth surfaces on the --bite NO-BITE list (GOLDEN_STANDARD 特徵3)
  }
  return pass ? 0 : 1;
}

}  // namespace sw
