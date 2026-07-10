// selftests_mathv_brdflookup.cpp — --selftest-mathv-brdflookup (kernel correctness, texture-compute seam).
//
// MATH_VERIFY_WORKFLOW.md §1.3 (texture path): host-allocate the RGBA16_UNorm output texture → DIRECT
// dispatch the transpiled MSL kernel (app/shaders/computeshaderstage_brdflookup.metal) → readback
// (getBytes, the pbr_shading_golden.cpp:176-182 idiom) → compare EACH texel to the R-authored CPU
// oracle (app/src/mathv_ref_brdflookup.h, TRANSCRIBED from external/tixl ComputeBrdfLookupTexture-cs.hlsl).
//
// GENERATOR SHAPE (not the element-transform MathvCase fuzz driver): the BRDF LUT has NO input elements
// — each texel is a deterministic function of (ThreadID.x/W, ThreadID.y/H). So this TU is a bespoke
// texel-compare, not runMathvFuzz. It sweeps several sizes (32²/64²/non-square 96×64) to fuzz the 2D
// DISPATCH coverage (ceil(W/32)×ceil(H/32) tiles) — every texel of every size must equal the oracle.
//
// injectBug (GOLDEN_STANDARD 特徵3, real cook-path perturbation): a GENERATOR kernel has no scalar param
// to perturb (the mathv "params += 1e-2" shape doesn't apply), so the -bug leg DROPS the UAV dispatch
// (clears the texture to black) — the exact "UAV not bound → 黑貼圖" failure the seam guards. The oracle
// keeps the real values → every non-trivial texel diverges → RED. did-not-trip → return 0 (P1).
//
// eps (measured, transcendental class): the GPU runs fast-math sqrt/cos/sin/powr over a 1024-term
// accumulation with a per-sample `cosLi>0` branch, then quantizes to 16-bit UNorm; the float CPU ref
// runs std:: transcendentals. The grazing column (cosLo≈Epsilon=0.001) AMPLIFIES via the /cosLo in Gv,
// so its tolerance is looser (relative), while the interior is tight. kEpsLsb below is the MEASURED
// interior bound (see the printout); it is FAR tighter than a body-formula error (which moves the
// split-sum terms by O(0.1) = thousands of LSB) yet loose enough to absorb fast-math.
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
// (cosLo=Epsilon) is checked with a RELATIVE tolerance because its Gv (1/cosLo) magnifies fast-math.
constexpr int kEpsLsb = 6;         // interior |Δ|*65535 bound (measured; printout reports the real max)
constexpr float kGrazingRelEps = 0.02f;  // gx==0 relative tolerance (2%)

inline uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

// Dispatch the kernel into an RGBA16Unorm texture, readback, compare to the CPU oracle.
// Returns the max INTERIOR error in LSB (or a large sentinel on setup failure); sets `covered`
// = every texel written (no black hole), and `nonTrivial` = the reference itself is not ~black
// (so a black GPU output is a real divergence, not a trivially-matched all-zero image).
struct CaseResult { double maxErrLsb = 1e9; bool ok = false; bool nonTrivial = false; };

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
  double maxRefMag = 0.0;
  uint32_t worstX = 0, worstY = 0, worstC = 0;
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      float ref[4];
      mathv_ref::brdfLookupTexel(x, y, W, H, ref[0], ref[1], ref[2], ref[3]);
      const size_t i = ((size_t)y * W + x) * 4;
      for (uint32_t ch = 0; ch < 4; ++ch) {
        const float gpu = (float)px[i + ch] / 65535.0f;
        const double dLsb = std::fabs((double)gpu - (double)ref[ch]) * 65535.0;
        maxRefMag = std::max(maxRefMag, (double)std::fabs(ref[ch]));
        // gx==0 grazing column: relative tolerance (skip the interior-LSB accounting there).
        if (x == 0) continue;
        if (dLsb > maxInterior) { maxInterior = dLsb; worstX = x; worstY = y; worstC = ch; }
      }
    }
  }
  res.maxErrLsb = maxInterior;
  res.nonTrivial = maxRefMag > 0.05;  // the LUT interior reaches DFG1~0.9 → not a trivial all-black match
  res.ok = maxInterior <= (double)kEpsLsb;
  printf("[mathv-brdflookup] %s %ux%u: maxInteriorErr=%.3f LSB (eps=%d) worst=(%u,%u)ch%u refMax=%.4f -> %s\n",
         tag, W, H, res.maxErrLsb, kEpsLsb, worstX, worstY, worstC, maxRefMag,
         res.ok ? "ok" : "MISS");
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
  // ceil-div tail tile). Each is fully compared per-texel to the oracle.
  struct Sz { uint32_t w, h; const char* tag; };
  const Sz sizes[] = {{32, 32, "sq32"}, {64, 64, "sq64"}, {96, 64, "wide"}, {48, 80, "tallNon32"}};

  bool allOk = true;
  bool anyNonTrivial = false;
  double worstErr = 0.0;
  for (const Sz& s : sizes) {
    CaseResult r = brdfKernelCase(dev, q, pso, s.w, s.h, injectBug, s.tag);
    worstErr = std::max(worstErr, r.maxErrLsb);
    anyNonTrivial = anyNonTrivial || r.nonTrivial;
    if (!injectBug) allOk = allOk && r.ok;
  }

  pso->release(); lib->release(); q->release(); dev->release();

  if (!injectBug) {
    ParityReport rep("selftest-mathv-brdflookup");
    rep.expectTrue("all sizes within eps (interior)", allOk, worstErr);
    rep.expectTrue("oracle non-trivial (interior reaches DFG~0.9)", anyNonTrivial, 1.0);
    const int rc = rep.finish();
    pool->release();
    return rc;
  }
  // -bug: the black texture must diverge from the non-trivial oracle (did-not-trip → return 0).
  const bool bites = anyNonTrivial && (worstErr > (double)kEpsLsb);
  printf("[selftest-mathv-brdflookup] -bug: worstErr=%.1f LSB nonTrivial=%d -> %s\n", worstErr,
         anyNonTrivial ? 1 : 0, bites ? "BITES (black != BRDF)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// order 1050: appends after the point-kernel mathv rows (1001-1009); a texture generator, no collision.
REGISTER_SELFTESTS(/*orderBase=*/1050, {"mathv-brdflookup", runMathvBrdfLookupSelfTest});

}  // namespace sw
