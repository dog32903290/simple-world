// selftests_mathv_simulatepoints.cpp — --selftest-mathv-simulatepoints (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-4 transpiler量產批: fuzz the TRANSPILED GPU "simulatepoints"
// kernel (app/shaders/simulatepoints.metal — glslang+spirv-cross output of TiXL points/sim/
// simulate-points.hlsl, §10.1 recipe) against the R-authored CPU oracle
// (app/src/mathv_ref_simulatepoints.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// EXACT-class op (affine add/mul only, no transcendental/branchy math).
//
// ── ParamDomain provenance ───────────────────────────────────────────────────────────────────────
// No .t3ui (legacy internal op, external/tixl/Operators/Lib/point/sim/_legacy/
// _LegacySimForwardMovement.t3, no user-facing widget metadata in this batch). Drag domain kept in
// [0,1] (a physically meaningful damping coefficient -- TiXL's other Drag-named params, e.g.
// ParticleSystem's, use the same [0,1] convention per particle_params.h); Speed domain
// [-10,10] (bidirectional scale, no natural TiXL bound found for this legacy op) with a wide
// special-value sweep (0, +-1, +-huge) to also exercise the affine formula's linearity at scale.
//
// injectBug: corrupt the REAL GPU-side Velocity.x of particle 0 (real input flowing into BOTH the
// Position-update formula and the Velocity-decay formula) while the CPU ref keeps the original.
//
// ZONE: shell tier; crosses runtime only for the kernel's params ABI header + tixl_point.h's Particle
// struct (data layout, not math).
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_simulatepoints.h"
#include "parity_golden_harness.h"
#include "runtime/simulatepoints_params.h"
#include "runtime/selftest_registry.h"
#include "runtime/tixl_point.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

using mathv::Comparator;
using mathv::EpsSpec;
using mathv::Rng;

Particle randomParticle(Rng& rng) {
  Particle p{};
  p.Position = {rng.uniform(-1e3f, 1e3f), rng.uniform(-1e3f, 1e3f), rng.uniform(-1e3f, 1e3f)};
  p.Radius = rng.uniform(-1e3f, 1e3f);
  p.Rotation = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f),
                rng.uniform(-1.0f, 1.0f)};
  p.Color = {rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f),
             rng.uniform(0.0f, 1.0f)};
  p.Velocity = {rng.uniform(-1e3f, 1e3f), rng.uniform(-1e3f, 1e3f), rng.uniform(-1e3f, 1e3f)};
  p.BirthTime = rng.uniform(-1e3f, 1e3f);
  return p;
}

struct SimPointsDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  SimPointsDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("simulatepoints", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~SimPointsDispatch() { if (pso) pso->release(); }
  SimPointsDispatch(const SimPointsDispatch&) = delete;

  bool dispatch(const std::vector<Particle>& in, float drag, float speed,
                std::vector<Particle>& out) const {
    if (!ok) return false;
    if (in.empty()) return false;
    out = in;
    MTL::Buffer* buf = dev->newBuffer(out.data(), (NS::UInteger)(out.size() * sizeof(Particle)),
                                       MTL::ResourceStorageModeShared);
    SimulatePointsParams prm{(int32_t)in.size(), drag, speed, 0.0f};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(buf, 0, SIMULATEPOINTS_Particles);
    enc->setBytes(&prm, sizeof(prm), SIMULATEPOINTS_Params);
    const uint32_t tg = 64;  // matches numthreads(64,1,1) in the HLSL
    const uint32_t n = (uint32_t)in.size();
    enc->dispatchThreadgroups(MTL::Size::Make((n + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(out.data(), buf->contents(), out.size() * sizeof(Particle));
    buf->release();
    return true;
  }
};

void addParticleFields(Comparator& cmp, const Particle& gpu, const Particle& ref, const Particle& in,
                        const char* tag) {
  float inVec[16] = {in.Position.x, in.Position.y, in.Position.z, in.Radius,
                      in.Rotation.x, in.Rotation.y, in.Rotation.z, in.Rotation.w,
                      in.Color.x, in.Color.y, in.Color.z, in.Color.w,
                      in.Velocity.x, in.Velocity.y, in.Velocity.z, in.BirthTime};
  float gv[16] = {gpu.Position.x, gpu.Position.y, gpu.Position.z, gpu.Radius,
                   gpu.Rotation.x, gpu.Rotation.y, gpu.Rotation.z, gpu.Rotation.w,
                   gpu.Color.x, gpu.Color.y, gpu.Color.z, gpu.Color.w,
                   gpu.Velocity.x, gpu.Velocity.y, gpu.Velocity.z, gpu.BirthTime};
  float rv[16] = {ref.Position.x, ref.Position.y, ref.Position.z, ref.Radius,
                   ref.Rotation.x, ref.Rotation.y, ref.Rotation.z, ref.Rotation.w,
                   ref.Color.x, ref.Color.y, ref.Color.z, ref.Color.w,
                   ref.Velocity.x, ref.Velocity.y, ref.Velocity.z, ref.BirthTime};
  for (int k = 0; k < 16; ++k) cmp.add(gv[k], rv[k], inVec, 16, k, -1.0f, tag);
}

}  // namespace

int runMathvSimulatePointsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-simulatepoints] FAIL: no metallib\n"); return 1; }
  SimPointsDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-simulatepoints] FAIL: no kernel\n"); return 1; }

  Comparator cmpMain("mathv-simulatepoints", EpsSpec::exact(), 5);
  bool dispatchOk = true;

  struct Scenario { size_t n; float drag; float speed; const char* tag; };
  const Scenario scenarios[] = {
      {1, 0.0f, 0.0f, "n1-identity"},    // Drag=0,Speed=0 -> Position unchanged, Velocity unchanged
      {1, 1.0f, 1.0f, "n1-fulldrag"},    // Drag=1 -> Velocity zeroes out
      {63, 0.25f, 2.0f, "n63"},
      {64, 0.5f, -3.0f, "n64"},
      {65, 0.9f, 100.0f, "n65"},
      {4096, 0.1f, -1e4f, "n4096-huge-speed"},
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    std::vector<Particle> in(s.n);
    for (auto& p : in) p = randomParticle(rng);

    // injectBug: corrupt the real dispatch's source data (Velocity.x of particle 0) while the CPU
    // ref keeps the original -- real "corrupt input" lever, flows into both Position and Velocity.
    std::vector<Particle> inGpu = in;
    if (injectBug && !inGpu.empty()) inGpu[0].Velocity.x += 1e-2f;

    std::vector<Particle> gpuOut;
    if (!disp.dispatch(inGpu, s.drag, s.speed, gpuOut)) { dispatchOk = false; continue; }
    std::vector<Particle> refOut(s.n);
    mathv_ref::mathvRefSimulatePoints(in.data(), refOut.data(), s.n, s.drag, s.speed);  // UNPERTURBED
    for (size_t i = 0; i < s.n; ++i) addParticleFields(cmpMain, gpuOut[i], refOut[i], in[i], s.tag);
  }
  cmpMain.print();
  bool passMain = dispatchOk && cmpMain.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passMain, true, "simulatepoints");

  ParityReport rep("selftest-mathv-simulatepoints");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(scenarios, exact)", passMain, passMain ? 1.0 : 0.0);
  return rep.finish();
}

// order 1089: transpiler-batch wave-4 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1089, {"mathv-simulatepoints", runMathvSimulatePointsSelfTest});

}  // namespace sw
