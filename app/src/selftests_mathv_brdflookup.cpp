// selftests_mathv_brdflookup.cpp — --selftest-mathv-brdflookup (kernel correctness, texture-compute seam).
//
// MATH_VERIFY_WORKFLOW.md §1.3 (texture path): host-allocate the RGBA16_UNorm output texture → DIRECT
// dispatch the transpiled MSL kernel (app/shaders/computeshaderstage_brdflookup.metal) → readback
// (getBytes, the pbr_shading_golden.cpp:176-182 idiom) → compare EACH texel to the R-authored CPU
// oracle (app/src/mathv_ref_brdflookup.h, TRANSCRIBED from external/tixl ComputeBrdfLookupTexture-cs.hlsl).
//
// GENERATOR SHAPE (not the element-transform MathvCase fuzz driver): the BRDF LUT has NO input elements
// — each texel is a deterministic function of (ThreadID.x/W, ThreadID.y/H). So this TU is a bespoke
// texel-compare, not runMathvFuzz. It sweeps several sizes (32²/64²/non-square 96×64/48×80) to fuzz the
// 2D DISPATCH coverage (ceil(W/32)×ceil(H/32) tiles) — every texel of every TESTED size must equal the
// oracle (within the per-band eps below). VALIDATION DOMAIN is these four sizes; see the 512² note below
// — the eps bands are LSB-scale (not resolution-normalized), so they do not automatically extrapolate to
// arbitrary resolutions.
//
// injectBug (GOLDEN_STANDARD 特徵3, real cook-path perturbation): a GENERATOR kernel has no scalar param
// to perturb (the mathv "params += 1e-2" shape doesn't apply), so the -bug leg DROPS the UAV dispatch
// (clears the texture to black) — the exact "UAV not bound → 黑貼圖" failure the seam guards. The oracle
// keeps the real values → every non-trivial texel diverges → RED. did-not-trip → return 0 (P1).
//
// eps (measured, transcendental class): the GPU runs fast-math sqrt/cos/sin/powr over a 1024-term
// accumulation with a per-sample `cosLi>0` branch, then quantizes to 16-bit UNorm; the float CPU ref
// runs std:: transcendentals. Three bands, each with its own measured eps (all MEASURED via an
// instrumented diagnostic run — see the constants below for the actual numbers, not estimates):
//   • INTERIOR (x!=0, roughness>=0.1): tight ABSOLUTE eps in LSB (kEpsLsb).
//   • GRAZING column (x==0, roughness>=0.1, cosLo=Epsilon): Gv's 1/cosLo magnifies fast-math →
//     RELATIVE eps (kGrazingRelEps) — an LSB bound would need to be huge near ref≈0.
//   • LOW-ROUGHNESS band (roughness<0.1, any x): the GGX cosTheta=sqrt((1-u2)/(1-u2)) 0/0-shaped
//     division diverges between the GPU's fast-rcp and the CPU's exact std:: divide → a loose RELATIVE
//     eps (kLowRoughRelEps) so the band still has REAL coverage (not a silent skip).
//
// 512² QUIRK (measured, not fixed by widening the tested-size set — see S verdict 2026-07-10): at
// 512×512 the INTERIOR check (kEpsLsb=3) MISSES at (x=1, y=52, roughness≈0.1016): measured 7.258 LSB.
// The eps bands above are absolute-LSB / fixed-relative and do NOT scale with resolution: at 512² the
// x=1 column sits at cosLo=1/512≈0.00195, far closer to the grazing singularity (Epsilon=0.001) than the
// x=1 column at the tested sizes (e.g. cosLo=1/32≈0.03125 at 32²) — the fast-math taper that is
// contained inside the excluded gx==0 column at small sizes leaks into x=1 (still "interior" by the
// x==0 exclusion rule) as resolution grows. This is a continuum property of the fast-math taper, not a
// body-formula divergence (S: the math itself is zero-fork-aligned end to end). A 512² leg was measured
// at ~20s/case (single-threaded CPU oracle, 512×512×1024 samples) — too slow to add to this sweep — so
// the VALIDATION DOMAIN is deliberately scoped to the four sizes above; 512²+ resolutions are OUT OF
// SCOPE for this golden (production BRDF LUTs are typically 128-256px square; if a future caller needs a
// verified 512²+ path, widen the x==0 exclusion to a cosLo threshold and re-measure eps for that band).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "mathv_ref_brdflookup.h"
#include "parity_golden_harness.h"       // SW_SHADER_METALLIB
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS

namespace sw {
namespace {

// Interior interior-column tolerance in 16-bit UNorm LSB (measured). The grazing column gx==0
// (cosLo=Epsilon) and the roughness<0.1 knife-edge band are each checked with their own RELATIVE
// tolerance (see the header comment for why an LSB bound doesn't work for either band).
constexpr int kEpsLsb = 3;         // interior |Δ|*65535 bound (MEASURED interior max ~1.0 LSB across 32²/64²/96×64/48×80; 3x fast-math margin). Domain: the four tested sizes only — see 512² note above.
constexpr float kGrazingRelEps = 0.02f;   // gx==0 (roughness>=0.1) relative tolerance (2%). MEASURED max rel err ~3.1e-4 across the four tested sizes -> ~64x margin.
constexpr float kLowRoughRelEps = 0.07f;  // roughness<0.1 (any x) relative tolerance (7%), deliberately loose. MEASURED max rel err ~6.53e-2 (sq64, row0) across the four tested sizes -> ~7% margin. Not zero-coverage: still catches a real body-formula break (which moves terms by O(0.1), i.e. ~40x this eps), just not the fast-rcp taper documented below.

inline uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

// Relative error helper for the grazing/low-roughness bands: an LSB (absolute) bound blows up when
// ref≈0, so these bands compare relative-to-magnitude instead, with a floor to avoid a divide-by-~0.
inline double relErr(float gpu, float ref) {
  const double denom = std::max((double)std::fabs(ref), 1e-3);
  return std::fabs((double)gpu - (double)ref) / denom;
}

// Dispatch the kernel into an RGBA16Unorm texture, readback, compare to the CPU oracle.
// Returns the max error per band (or a large sentinel on setup failure); `nonTrivial` = the reference
// itself is not ~black (so a black GPU output is a real divergence, not a trivially-matched all-zero
// image).
struct CaseResult {
  double maxErrLsb = 1e9;        // interior band (absolute LSB)
  double maxGrazingRel = 1e9;    // gx==0, roughness>=0.1 band (relative)
  double maxLowRoughRel = 1e9;   // roughness<0.1 band, any x (relative)
  bool ok = false;
  bool nonTrivial = false;
};

CaseResult brdfKernelCase(MTL::Device* dev, MTL::CommandQueue* q, MTL::ComputePipelineState* pso,
                          uint32_t W, uint32_t H, bool injectBug, const char* tag) {
  CaseResult res;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA16Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageShaderWrite | MTL::TextureUsageShaderRead |
               MTL::TextureUsageRenderTarget);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);
  if (!tex) return res;

  if (injectBug) {
    // -bug: UAV dispatch dropped → clear to black (the exact "UAV not bound" failure).
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(tex);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = q->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  } else {
    MTL::CommandBuffer* cmd = q->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setTexture(tex, 0);  // RWTexture2D<float4> LUT : register(u0) → texture(0)
    enc->dispatchThreadgroups(MTL::Size::Make(ceilDiv(W, 32), ceilDiv(H, 32), 1),
                              MTL::Size::Make(32, 32, 1));  // numthreads(32,32,1)
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }

  std::vector<uint16_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), (NS::UInteger)W * 4 * sizeof(uint16_t),
                MTL::Region::Make2D(0, 0, W, H), 0);
  tex->release();

  double maxInterior = 0.0;
  double maxGrazingRel = 0.0;
  double maxLowRoughRel = 0.0;
  double maxRefMag = 0.0;
  uint32_t worstX = 0, worstY = 0, worstC = 0;
  uint32_t worstGX = 0, worstGY = 0, worstLX = 0, worstLY = 0;
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      float ref[4];
      mathv_ref::brdfLookupTexel(x, y, W, H, ref[0], ref[1], ref[2], ref[3]);
      const size_t i = ((size_t)y * W + x) * 4;
      // THREE bands, each with a REAL check (none is a silent skip) — see the header comment for the
      // measured numbers behind each eps:
      //  • roughness<0.1 (any x): 0/0-shaped GGX knife-edge → loose RELATIVE check (kLowRoughRelEps).
      //  • gx==0, roughness>=0.1 (grazing column, cosLo=Epsilon): Gv's 1/cosLo magnifies fast-math →
      //    RELATIVE check (kGrazingRelEps).
      //  • everything else (interior): tight ABSOLUTE check (kEpsLsb).
      const float roughness = (float)y / (float)H;
      const bool lowRough = roughness < 0.1f;
      const bool grazing = (x == 0) && !lowRough;
      for (uint32_t ch = 0; ch < 4; ++ch) {
        const float gpu = (float)px[i + ch] / 65535.0f;
        maxRefMag = std::max(maxRefMag, (double)std::fabs(ref[ch]));
        if (lowRough) {
          const double dRel = relErr(gpu, ref[ch]);
          if (dRel > maxLowRoughRel) { maxLowRoughRel = dRel; worstLX = x; worstLY = y; }
          continue;
        }
        if (grazing) {
          const double dRel = relErr(gpu, ref[ch]);
          if (dRel > maxGrazingRel) { maxGrazingRel = dRel; worstGX = x; worstGY = y; }
          continue;
        }
        const double dLsb = std::fabs((double)gpu - (double)ref[ch]) * 65535.0;
        if (dLsb > maxInterior) { maxInterior = dLsb; worstX = x; worstY = y; worstC = ch; }
      }
    }
  }
  res.maxErrLsb = maxInterior;
  res.maxGrazingRel = maxGrazingRel;
  res.maxLowRoughRel = maxLowRoughRel;
  res.nonTrivial = maxRefMag > 0.05;  // the LUT interior reaches DFG1~0.9 → not a trivial all-black match
  res.ok = (maxInterior <= (double)kEpsLsb) && (maxGrazingRel <= (double)kGrazingRelEps) &&
           (maxLowRoughRel <= (double)kLowRoughRelEps);
  printf("[mathv-brdflookup] %s %ux%u: interior=%.3fLSB(eps=%d)@(%u,%u)ch%u grazingRel=%.5f(eps=%.2f)@(%u,%u) "
         "lowRoughRel=%.5f(eps=%.2f)@(%u,%u) refMax=%.4f -> %s\n",
         tag, W, H, maxInterior, kEpsLsb, worstX, worstY, worstC, maxGrazingRel, kGrazingRelEps, worstGX,
         worstGY, maxLowRoughRel, kLowRoughRelEps, worstLX, worstLY, maxRefMag, res.ok ? "ok" : "MISS");
  return res;
}

}  // namespace

int runMathvBrdfLookupSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-mathv-brdflookup] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::Function* fn =
      lib->newFunction(NS::String::string("computeshaderstage_brdflookup", NS::UTF8StringEncoding));
  MTL::ComputePipelineState* pso = fn ? dev->newComputePipelineState(fn, &err) : nullptr;
  if (fn) fn->release();
  if (!pso) {
    printf("[selftest-mathv-brdflookup] FAIL: no PSO for computeshaderstage_brdflookup\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  // Sizes: square powers + a NON-square to fuzz the 2D dispatch (W≠H, non-32-multiple H exercises the
  // ceil-div tail tile). Each is fully compared per-texel to the oracle. VALIDATION DOMAIN — see the
  // 512² note in the header comment for why this sweep stops here (measured ~20s/case at 512², too slow
  // for this golden; the eps bands above are absolute-LSB/fixed-relative and don't auto-extrapolate).
  struct Sz { uint32_t w, h; const char* tag; };
  const Sz sizes[] = {{32, 32, "sq32"}, {64, 64, "sq64"}, {96, 64, "wide"}, {48, 80, "tallNon32"}};

  bool anyNonTrivial = false;
  double worstErr = 0.0;
  double worstGrazingRel = 0.0;
  double worstLowRoughRel = 0.0;
  for (const Sz& s : sizes) {
    CaseResult r = brdfKernelCase(dev, q, pso, s.w, s.h, injectBug, s.tag);
    worstErr = std::max(worstErr, r.maxErrLsb);
    worstGrazingRel = std::max(worstGrazingRel, r.maxGrazingRel);
    worstLowRoughRel = std::max(worstLowRoughRel, r.maxLowRoughRel);
    anyNonTrivial = anyNonTrivial || r.nonTrivial;
  }

  pso->release(); lib->release(); q->release(); dev->release();

  if (!injectBug) {
    // Each row checks its OWN band independently (not r.ok, which ANDs all three per-case — that would
    // make the "interior" row go red on a pure grazing/low-rough miss and obscure which band broke).
    ParityReport rep("selftest-mathv-brdflookup");
    rep.expectTrue("all sizes within eps (interior)", worstErr <= (double)kEpsLsb, worstErr);
    rep.expectTrue("grazing column within relative eps", worstGrazingRel <= (double)kGrazingRelEps,
                    worstGrazingRel);
    rep.expectTrue("low-roughness band within relative eps", worstLowRoughRel <= (double)kLowRoughRelEps,
                    worstLowRoughRel);
    rep.expectTrue("oracle non-trivial (interior reaches DFG~0.9)", anyNonTrivial, 1.0);
    const int rc = rep.finish();
    pool->release();
    return rc;
  }
  // -bug: the black texture must diverge from the non-trivial oracle (did-not-trip → return 0). Any of
  // the three bands catching the divergence counts as a bite.
  const bool bites = anyNonTrivial && ((worstErr > (double)kEpsLsb) ||
                                        (worstGrazingRel > (double)kGrazingRelEps) ||
                                        (worstLowRoughRel > (double)kLowRoughRelEps));
  printf("[selftest-mathv-brdflookup] -bug: worstErr=%.1fLSB worstGrazingRel=%.4f worstLowRoughRel=%.4f "
         "nonTrivial=%d -> %s\n",
         worstErr, worstGrazingRel, worstLowRoughRel, anyNonTrivial ? 1 : 0,
         bites ? "BITES (black != BRDF)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// order 1050: appends after the point-kernel mathv rows (1001-1009); a texture generator, no collision.
REGISTER_SELFTESTS(/*orderBase=*/1050, {"mathv-brdflookup", runMathvBrdfLookupSelfTest});

}  // namespace sw
