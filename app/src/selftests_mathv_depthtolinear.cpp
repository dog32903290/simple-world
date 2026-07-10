// selftests_mathv_depthtolinear.cpp — --selftest-mathv-depthtolinear (kernel correctness, texture-compute
// seam STAGE 2: the FIRST SRV-tex-read tex-out kernel).
//
// MATH_VERIFY_WORKFLOW.md §1.3 (texture path): host-allocate an R32Float INPUT depth texture (filled with
// a known gradient that spans negative → positive so BOTH the depth<0 checker branch and the linearized
// branch are exercised) + an R32Float OUTPUT texture → DIRECT dispatch the transpiled MSL kernel
// (app/shaders/computeshaderstage_depthtolinear.metal) with the b0 CB set-bytes'd → readback (getBytes,
// the pbr_shading_golden.cpp:176-182 idiom) → compare EACH texel to the R-authored CPU oracle
// (app/src/mathv_ref_depthtolinear.h, TRANSCRIBED from external/tixl depth-to-linear.hlsl).
//
// GENERATOR-with-input SHAPE: the output at (gx,gy) is a deterministic function of the input depth texel
// + the CB. So this is a bespoke texel-compare (not runMathvFuzz). It SWEEPS the CB across the kernel's
// four real branches — Linear vs LegacyDOF mode, remap on/off, clamp on/off — and three sizes (a
// non-16-multiple W/H exercises the ceil-div dispatch tail). VALIDATION DOMAIN = these param sets × sizes.
//
// injectBug (GOLDEN_STANDARD 特徵3, real cook-path perturbation): a texture kernel has no scalar param to
// perturb, so the -bug leg DROPS the dispatch (clears the output to 0) — the exact "UAV not written" seam
// failure. The oracle keeps the real values; every non-trivial (linearized) texel diverges → RED. The
// depth<0 checker texels that the oracle maps to 0 STILL match black, so the bite is asserted on the
// linearized band's magnitude (maxRefMag), not on the checker. did-not-trip → return 0 (P1).
//
// eps (measured, division class — NOT transcendental): the kernel is pure divides + an optional
// fast::clamp, no pow/sqrt/trig. fast-math divide is ~1 ULP; R32Float storage is exact-ish. So the
// linearized band uses a tight RELATIVE eps (kRelEps), and the checker band (output is an exact 0/1 from a
// coord parity) uses an exact-ish ABSOLUTE eps (kCheckerEps). Both MEASURED via the instrumented run
// (see the constants). Depth is capped at 0.85 so no CB's denominator (depth·(f-n)-f) approaches 0.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "mathv_ref_depthtolinear.h"
#include "parity_golden_harness.h"       // SW_SHADER_METALLIB
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS

namespace sw {
namespace {

constexpr double kRelEps = 1e-3;       // linearized band relative tol. MEASURED max ~2.8e-5 across the sweep -> ~35x margin.
constexpr double kCheckerEps = 1e-4;   // depth<0 checker band (output is an exact 0/1 coord-parity) absolute tol.

inline uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

// The known input depth gradient (spans [-0.15, 0.85] so the left edge is depth<0 → the checker branch,
// the rest is the linearized branch; capped at 0.85 to stay clear of the depth·(f-n)-f singularity).
inline float depthAt(uint32_t gx, uint32_t W) {
  const float t = (W > 1) ? (float)gx / (float)(W - 1) : 0.0f;
  return -0.15f + 1.0f * t;
}

struct CaseResult {
  double maxRel = 0.0;       // linearized band (relative)
  double maxChecker = 0.0;   // depth<0 band (absolute vs the 0/1 coord-parity)
  double maxRefMag = 0.0;    // largest |ref| in the linearized band (non-triviality of the black -bug)
  bool ok = false;
};

CaseResult depthCase(MTL::Device* dev, MTL::CommandQueue* q, MTL::ComputePipelineState* pso, uint32_t W,
                     uint32_t H, const mathv_ref::DepthParams& p, bool injectBug, const char* tag) {
  CaseResult res;

  // R32Float INPUT depth texture, filled with the known gradient.
  MTL::TextureDescriptor* tdIn =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR32Float, W, H, false);
  tdIn->setUsage(MTL::TextureUsageShaderRead);
  tdIn->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* in = dev->newTexture(tdIn);
  std::vector<float> depth((size_t)W * H, 0.0f);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) depth[(size_t)y * W + x] = depthAt(x, W);
  in->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, depth.data(), (NS::UInteger)W * sizeof(float));

  // R32Float OUTPUT (shaderWrite).
  MTL::TextureDescriptor* tdOut =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR32Float, W, H, false);
  tdOut->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead |
                  MTL::TextureUsageRenderTarget);
  tdOut->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* out = dev->newTexture(tdOut);

  if (injectBug) {
    // -bug: dispatch dropped → clear to 0 (the "UAV not written" failure).
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(out);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = q->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  } else {
    const float cb[6] = {p.Near, p.Far, p.OutrangeMin, p.OutrangeMax, p.ClampRange, p.Mode};
    MTL::CommandBuffer* cmd = q->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBytes(cb, sizeof(cb), 0);   // b0 → buffer(0)
    enc->setTexture(in, 0);             // InputTexture (t0) → texture(0)
    enc->setTexture(out, 1);            // OutputTexture (u0) → texture(1)
    enc->dispatchThreadgroups(MTL::Size::Make(ceilDiv(W, 16), ceilDiv(H, 16), 1),
                              MTL::Size::Make(16, 16, 1));  // numthreads(16,16,1)
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }

  std::vector<float> px((size_t)W * H, 0.0f);
  out->getBytes(px.data(), (NS::UInteger)W * sizeof(float), MTL::Region::Make2D(0, 0, W, H), 0);
  in->release();
  out->release();

  uint32_t worstX = 0, worstY = 0;
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      const float d = depthAt(x, W);
      const float ref = mathv_ref::depthToLinearTexel(d, x, y, p);
      const float gpu = px[(size_t)y * W + x];
      if (d < 0.0f) {
        res.maxChecker = std::max(res.maxChecker, (double)std::fabs(gpu - ref));
      } else {
        res.maxRefMag = std::max(res.maxRefMag, (double)std::fabs(ref));
        const double denom = std::max((double)std::fabs(ref), 1e-6);
        const double rel = std::fabs((double)gpu - (double)ref) / denom;
        if (rel > res.maxRel) { res.maxRel = rel; worstX = x; worstY = y; }
      }
    }
  }
  res.ok = (res.maxRel <= kRelEps) && (res.maxChecker <= kCheckerEps);
  printf("[mathv-depthtolinear] %s %ux%u mode=%.0f remap=%d clamp=%.0f: rel=%.6f(eps=%.0e)@(%u,%u) "
         "checker=%.6f refMag=%.3f -> %s\n",
         tag, W, H, p.Mode, (p.OutrangeMin != 0 || p.OutrangeMax != 0) ? 1 : 0, p.ClampRange, res.maxRel,
         kRelEps, worstX, worstY, res.maxChecker, res.maxRefMag, res.ok ? "ok" : "MISS");
  return res;
}

}  // namespace

int runMathvDepthToLinearSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-mathv-depthtolinear] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::Function* fn =
      lib->newFunction(NS::String::string("computeshaderstage_depthtolinear", NS::UTF8StringEncoding));
  MTL::ComputePipelineState* pso = fn ? dev->newComputePipelineState(fn, &err) : nullptr;
  if (fn) fn->release();
  if (!pso) {
    printf("[selftest-mathv-depthtolinear] FAIL: no PSO for computeshaderstage_depthtolinear\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  // Sweep the CB across the kernel's four real branches; three sizes (48×40 is non-16-multiple → the
  // ceil-div tail tile is exercised). Every texel of every case compared per the eps bands above.
  using DP = mathv_ref::DepthParams;
  struct Case { DP p; uint32_t w, h; const char* tag; };
  const Case cases[] = {
      {DP{0.1f, 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f}, 32, 32, "linear"},        // Linear, no remap/clamp
      {DP{0.1f, 100.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 48, 40, "legacyDOF"},      // LegacyDOF mode
      {DP{0.5f, 50.0f, 0.1f, 0.9f, 0.0f, 0.0f}, 64, 48, "remap"},           // remap on (OutMin/Max != 0)
      {DP{0.1f, 10.0f, 0.2f, 0.8f, 1.0f, 0.0f}, 40, 32, "remapClamp"},      // remap + clamp (saturate)
  };

  double worstRel = 0.0, worstChecker = 0.0, worstRefMag = 0.0;
  for (const Case& c : cases) {
    CaseResult r = depthCase(dev, q, pso, c.w, c.h, c.p, injectBug, c.tag);
    worstRel = std::max(worstRel, r.maxRel);
    worstChecker = std::max(worstChecker, r.maxChecker);
    worstRefMag = std::max(worstRefMag, r.maxRefMag);
  }

  pso->release(); lib->release(); q->release(); dev->release();

  if (!injectBug) {
    ParityReport rep("selftest-mathv-depthtolinear");
    rep.expectTrue("linearized band within relative eps", worstRel <= kRelEps, worstRel);
    rep.expectTrue("depth<0 checker band exact", worstChecker <= kCheckerEps, worstChecker);
    rep.expectTrue("oracle non-trivial (linearized reaches real depth)", worstRefMag > 0.1, worstRefMag);
    const int rc = rep.finish();
    pool->release();
    return rc;
  }
  // -bug: the black output must diverge from the non-trivial linearized oracle (did-not-trip → return 0).
  const bool bites = worstRefMag > 0.1 && worstRel > kRelEps;
  printf("[selftest-mathv-depthtolinear] -bug: worstRel=%.4f worstRefMag=%.3f -> %s\n", worstRel,
         worstRefMag, bites ? "BITES (black != linear depth)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// order 1051: appends after mathv-brdflookup (1050); a texture kernel, no collision.
REGISTER_SELFTESTS(/*orderBase=*/1051, {"mathv-depthtolinear", runMathvDepthToLinearSelfTest});

}  // namespace sw
