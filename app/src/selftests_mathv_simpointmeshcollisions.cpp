// selftests_mathv_simpointmeshcollisions.cpp — --selftest-mathv-simpointmeshcollisions (hand-rolled TU).
// MATH_VERIFY_WORKFLOW.md §10 wave-3 transpiler量產批 (candidate 6/6, replacing mesh-CollapseVertices.
// hlsl — see runtime/simpointmeshcollisions_params.h header for why): fuzz the TRANSPILED GPU
// "simpointmeshcollisions" kernel (app/shaders/simpointmeshcollisions.metal — glslang+spirv-cross
// output of TiXL SimPointMeshCollisions.hlsl, §10.1 recipe) against the R-authored CPU oracle
// (app/src/mathv_ref_simpointmeshcollisions.h) via direct-kernel dispatch (§1.3 — NOT buildEvalGraph).
// TRANSCENDENTAL+BRANCHY class (length/normalize + closestPointOnTriangle's 7-way Ericson region
// branch, exercised via a controlled single-triangle mesh whose regions are geometrically pinned).
//
// ── SCOPE ────────────────────────────────────────────────────────────────────────────────────────
// This kernel WRITES CONDITIONALLY: two early-return guards (`isnan(distance)||distance<0.001` and
// `distance>Radius`) mean Velocity is UNTOUCHED most of the time (in-place RWStructuredBuffer, no
// separate result buffer). Every scenario therefore checks ONE of two things: a "hit" case compares
// the written Velocity against the ref (transcendental gate); a "miss"/"too-close" case asserts
// Velocity survives UNCHANGED (exact sentinel check, same untouched-field discipline as flipnormals'
// TexCoord2 tooth). Single-triangle mesh (p0=(0,0,0), p1=(1,0,0), p2=(0,1,0)) geometrically pins which
// Ericson region each scenario lands in (interior-projection vs vertex-clamp vs edge-clamp).
//
// ZONE: shell tier; crosses runtime only for SwVertex/SwTriIndex (runtime/sw_mesh.h) + Particle
// (runtime/tixl_point.h, TiXL-faithful 64B layout) + the kernel's params ABI header.
#include "mathv_compare.h"
#include "mathv_harness.h"  // mathv::mathvVerdictToExit only (hand-rolled TU)
#include "mathv_ref_simpointmeshcollisions.h"
#include "parity_golden_harness.h"
#include "runtime/simpointmeshcollisions_params.h"
#include "runtime/sw_mesh.h"
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

struct SpmcDispatch {
  MTL::Device* dev;
  MTL::CommandQueue* queue;
  MTL::ComputePipelineState* pso = nullptr;
  bool ok = false;
  SpmcDispatch(MTL::Device* d, MTL::CommandQueue* q, MTL::Library* lib) : dev(d), queue(q) {
    MTL::Function* fn = lib->newFunction(NS::String::string("simpointmeshcollisions", NS::UTF8StringEncoding));
    if (!fn) return;
    NS::Error* err = nullptr;
    pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    ok = pso != nullptr;
  }
  ~SpmcDispatch() { if (pso) pso->release(); }
  SpmcDispatch(const SpmcDispatch&) = delete;

  // particles is mutated IN PLACE (matches the kernel's RWStructuredBuffer<Particle> single-buffer shape).
  bool dispatch(const std::vector<SwTriIndex>& faces, const std::vector<SwVertex>& verts,
                std::vector<Particle>& particles, float bounciness) const {
    if (!ok) return false;
    const uint32_t nP = (uint32_t)particles.size();
    if (nP == 0 || faces.empty() || verts.empty()) return false;
    MTL::Buffer* bufFaces = dev->newBuffer(faces.data(), (NS::UInteger)(faces.size() * sizeof(SwTriIndex)),
                                           MTL::ResourceStorageModeShared);
    MTL::Buffer* bufVerts = dev->newBuffer(verts.data(), (NS::UInteger)(verts.size() * sizeof(SwVertex)),
                                           MTL::ResourceStorageModeShared);
    MTL::Buffer* bufParticles = dev->newBuffer(particles.data(), (NS::UInteger)(nP * sizeof(Particle)),
                                               MTL::ResourceStorageModeShared);
    SimPointMeshCollisionsParams prm{bounciness, 0.0f, (uint32_t)faces.size(), nP};
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBuffer(bufFaces, 0, SPMC_Indices);
    enc->setBuffer(bufVerts, 0, SPMC_Vertices);
    enc->setBuffer(bufParticles, 0, SPMC_Particles);
    enc->setBytes(&prm, sizeof(prm), SPMC_Params);
    const uint32_t tg = 64;
    enc->dispatchThreadgroups(MTL::Size::Make((nP + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    std::memcpy(particles.data(), bufParticles->contents(), (size_t)nP * sizeof(Particle));
    bufFaces->release();
    bufVerts->release();
    bufParticles->release();
    return true;
  }
};

SwVertex makeVertex(mathv_ref::Vec3d pos) {
  SwVertex v{};
  v.Position = {pos.x, pos.y, pos.z};
  return v;
}

Particle makeParticle(mathv_ref::Vec3d pos, float radius, mathv_ref::Vec3d vel) {
  Particle p{};
  p.Position = {pos.x, pos.y, pos.z};
  p.Radius = radius;
  p.Rotation = {0, 0, 0, 1};
  p.Color = {1, 1, 1, 1};
  p.Velocity = {vel.x, vel.y, vel.z};
  p.BirthTime = 0.0f;
  return p;
}

}  // namespace

int runMathvSimPointMeshCollisionsSelfTest(bool injectBug) {
  ParityHarness h;
  if (!h.ok()) { printf("[selftest-mathv-simpointmeshcollisions] FAIL: no metallib\n"); return 1; }
  SpmcDispatch disp(h.dev, h.queue, h.lib);
  if (!disp.ok) { printf("[selftest-mathv-simpointmeshcollisions] FAIL: no kernel\n"); return 1; }

  // Single right-triangle mesh: p0=(0,0,0), p1=(1,0,0), p2=(0,1,0), normal=+Z.
  std::vector<SwVertex> verts = {makeVertex({0, 0, 0}), makeVertex({1, 0, 0}), makeVertex({0, 1, 0})};
  std::vector<SwTriIndex> faces = {{0, 1, 2}};
  std::vector<mathv_ref::Vec3d> refVertPos = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  std::vector<mathv_ref::SpmcFace> refFaces = {{0, 1, 2}};

  Comparator cmpHit("mathv-simpointmeshcollisions-hit", EpsSpec::transcendental(), 5);
  bool untouchedOk = true;
  bool dispatchOk = true;

  struct Scenario {
    mathv_ref::Vec3d pos;
    float radius;
    const char* tag;
    bool expectHit;  // whether the ref predicts distance<=radius && distance>=0.001 (no NaN)
  };
  const Scenario scenarios[] = {
      {{0.2f, 0.2f, 0.5f}, 1.0f, "interior-hit", true},         // interior-projection region, within radius
      {{0.2f, 0.2f, 5.0f}, 1.0f, "interior-miss-far", false},   // interior-projection, radius too small
      {{-1.0f, -1.0f, 0.5f}, 5.0f, "vertex-clamp-hit", true},   // clamps to p0 vertex region
      {{2.0f, 2.0f, 0.5f}, 5.0f, "edge-clamp-hit", true},       // clamps along the hypotenuse edge
      {{0.2f, 0.2f, 0.0005f}, 1.0f, "too-close-miss", false},   // distance < 0.001 early-return
  };
  for (const auto& s : scenarios) {
    Rng rng(mathv::mathvSeed(s.tag));
    mathv_ref::Vec3d vel{rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
    float bounciness = injectBug ? 0.51f : 0.5f;  // corrupt the real dispatch path
    std::vector<Particle> particles = {makeParticle(s.pos, s.radius, vel)};
    Particle before = particles[0];
    if (!disp.dispatch(faces, verts, particles, bounciness)) { dispatchOk = false; continue; }
    const Particle& after = particles[0];

    mathv_ref::Vec3d refVel{};
    // ref ALWAYS sees the ORIGINAL bounciness (0.5f) -- only the GPU-bound `bounciness` local above is
    // perturbed under injectBug, so a "hit" scenario's Velocity necessarily forks between the two sides.
    bool refHit = mathv_ref::simPointMeshCollisionsOne(s.pos, s.radius, vel, refVertPos, refFaces, 0.5f, refVel);
    if (refHit != s.expectHit && !injectBug) {
      printf("[selftest-mathv-simpointmeshcollisions] WARN: scenario '%s' expectHit=%d but ref computed %d\n",
             s.tag, s.expectHit, refHit);
    }
    if (refHit) {
      float inVec[3] = {s.pos.x, s.pos.y, s.pos.z};
      float gv[3] = {after.Velocity.x, after.Velocity.y, after.Velocity.z};
      float rv[3] = {refVel.x, refVel.y, refVel.z};
      for (int k = 0; k < 3; ++k) cmpHit.add(gv[k], rv[k], inVec, 3, k, -1.0f, s.tag);
    } else if (!injectBug) {
      if (after.Velocity.x != before.Velocity.x || after.Velocity.y != before.Velocity.y ||
          after.Velocity.z != before.Velocity.z)
        untouchedOk = false;
    }
  }

  // Broader random multi-triangle sweep (statistical coverage, no per-scenario region pinning).
  {
    Rng rng(mathv::mathvSeed("simpointmeshcollisions-random"));
    std::vector<SwVertex> rverts(12);
    std::vector<mathv_ref::Vec3d> rrefVerts(12);
    for (int i = 0; i < 12; ++i) {
      mathv_ref::Vec3d p{rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f), rng.uniform(-2.0f, 2.0f)};
      rverts[(size_t)i] = makeVertex(p);
      rrefVerts[(size_t)i] = p;
    }
    std::vector<SwTriIndex> rfaces = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {9, 10, 11}};
    std::vector<mathv_ref::SpmcFace> rrefFaces = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {9, 10, 11}};
    std::vector<Particle> rparticles(64);
    std::vector<mathv_ref::Vec3d> rpos(64), rvel(64);
    std::vector<float> rradius(64);
    for (int i = 0; i < 64; ++i) {
      rpos[(size_t)i] = {rng.uniform(-3.0f, 3.0f), rng.uniform(-3.0f, 3.0f), rng.uniform(-3.0f, 3.0f)};
      rvel[(size_t)i] = {rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f), rng.uniform(-1.0f, 1.0f)};
      rradius[(size_t)i] = rng.uniform(0.5f, 4.0f);  // wide enough to hit sometimes, miss sometimes
      rparticles[(size_t)i] = makeParticle(rpos[(size_t)i], rradius[(size_t)i], rvel[(size_t)i]);
    }
    std::vector<Particle> before = rparticles;
    float bounciness = injectBug ? 0.51f : 0.5f;
    if (disp.dispatch(rfaces, rverts, rparticles, bounciness)) {
      for (int i = 0; i < 64; ++i) {
        mathv_ref::Vec3d refVel{};
        bool refHit = mathv_ref::simPointMeshCollisionsOne(rpos[(size_t)i], rradius[(size_t)i],
                                                             rvel[(size_t)i], rrefVerts, rrefFaces, 0.5f, refVel);
        if (refHit) {
          float inVec[3] = {rpos[(size_t)i].x, rpos[(size_t)i].y, rpos[(size_t)i].z};
          float gv[3] = {rparticles[(size_t)i].Velocity.x, rparticles[(size_t)i].Velocity.y, rparticles[(size_t)i].Velocity.z};
          float rv[3] = {refVel.x, refVel.y, refVel.z};
          for (int k = 0; k < 3; ++k) cmpHit.add(gv[k], rv[k], inVec, 3, k, -1.0f, "random-hit");
        } else if (!injectBug) {
          const auto& a = rparticles[(size_t)i].Velocity;
          const auto& b = before[(size_t)i].Velocity;
          if (a.x != b.x || a.y != b.y || a.z != b.z) untouchedOk = false;
        }
      }
    } else {
      dispatchOk = false;
    }
  }

  cmpHit.print();
  bool passHit = dispatchOk && cmpHit.verdict();
  if (injectBug) return mathv::mathvVerdictToExit(passHit, true, "simpointmeshcollisions");

  ParityReport rep("selftest-mathv-simpointmeshcollisions");
  rep.expectTrue("dispatch(adapter-ok)", dispatchOk, dispatchOk ? 1.0 : 0.0);
  rep.expectTrue("compare(hit Velocity, transcendental)", passHit, passHit ? 1.0 : 0.0);
  rep.expectTrue("untouched(miss/too-close: Velocity unchanged)", untouchedOk, untouchedOk ? 1.0 : 0.0);
  return rep.finish();
}

// order 1085: transpiler-batch wave-3 (MATH_VERIFY_WORKFLOW.md §10).
REGISTER_SELFTESTS(/*orderBase=*/1085, {"mathv-simpointmeshcollisions", runMathvSimPointMeshCollisionsSelfTest});

}  // namespace sw
