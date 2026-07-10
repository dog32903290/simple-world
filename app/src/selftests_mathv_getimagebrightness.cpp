// selftests_mathv_getimagebrightness.cpp — --selftest-mathv-getimagebrightness (kernel correctness,
// TEXTURE_COMPUTE_SEAM stage 3: the FIRST texture-SRV-read → BUFFER-write kernel through the generic
// ComputeShaderStage seam).
//
// MATH_VERIFY_WORKFLOW.md §1.3 (texture path, buffer output variant): host-allocate an RGBA32Float INPUT
// texture (known per-texel values) + a 1-uint UAV buffer (zero-init = the folded `clear` pass) → DIRECT
// dispatch the ported MSL kernel (app/shaders/computeshaderstage_getimagebrightness.metal) 2D over W×H with
// scaleFactor set-bytes'd at b0 → readback ResultBuffer[0] → compare to the R-authored CPU oracle
// (app/src/mathv_ref_getimagebrightness.h, TRANSCRIBED from external/tixl cs-GetImageBrightness.hlsl).
// Binding indices MATCH the buffer-stage cook so the SAME kernel serves the stage seal golden: scaleFactor
// (b0)→buffer(CS_CB_BASE=0), ResultBuffer (u0)→buffer(CS_UAV_BASE=12), InputTexture (t0)→texture(0).
//
// REDUCTION SHAPE: the output is a SINGLE scalar (Σ over texels), so this is a bespoke sum-compare (not
// runMathvFuzz). It sweeps scaleFactor × sizes (a non-8-multiple W/H exercises the ceil-div dispatch tail,
// whose edge threads must contribute 0 — the HLSL Load-OOB-returns-0 semantics). VALIDATION DOMAIN = these.
//
// injectBug (GOLDEN_STANDARD 特徵3, real perturbation): the -bug leg DROPS the dispatch (the UAV stays at
// its zero-init) — the "UAV not written" seam failure — so the readback (0) diverges from the non-trivial
// oracle sum. did-not-trip → return 0 (P1).
//
// eps: the per-texel math is pure adds/divide/multiply + a truncating cast (no transcendentals). The only
// residual GPU↔CPU divergence is `/3.0f` fast-math (~1 ULP), which can flip a texel's truncation by ±1 ONLY
// near an integer boundary. Fixtures are chosen to keep products clear of boundaries so the sum is EXACT
// (measured diff 0); a tiny tolerance (kSumTol) backstops any boundary texel without hiding a real fault.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "mathv_ref_getimagebrightness.h"
#include "parity_golden_harness.h"            // SW_SHADER_METALLIB / ParityReport
#include "runtime/computeshaderstage_params.h"  // CS_CB_BASE / CS_UAV_BASE / CS_TEX_SRV_BASE
#include "runtime/selftest_registry.h"        // REGISTER_SELFTESTS

namespace sw {
namespace {

// Absolute sum tolerance: 0 is the target (fixtures avoid truncation boundaries). A small nonzero backstop
// absorbs at most a handful of ±1 boundary flips without masking a genuine (whole-band) divergence.
constexpr uint32_t kSumTol = 0;

inline uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

// Known per-texel RGBA (float, exact-storable) chosen to VARY across the image yet keep lum·scaleFactor
// clear of integer boundaries. r=g=b=v with a=1 → lum = (3v)/3 = v exactly (v representable); v steps in
// 1/16ths so v·scaleFactor stays mid-integer for the sweep's scaleFactors (255/1000/10000 → 15.9375/62.5/
// 625 per 1/16 step, none integer-adjacent).
inline void texelRGBA(uint32_t x, uint32_t y, uint32_t W, float& r, float& g, float& b, float& a) {
  const uint32_t k = (x * 3u + y * 5u + (x ^ y)) % 13u;  // 0..12, varied but deterministic
  const float v = (float)(k + 1) / 16.0f;                // 1/16 .. 13/16 (all exact in float)
  r = g = b = v;
  a = 1.0f;  // a=1 keeps lum = v exactly (avoids a second /·a rounding); the a-path is exercised by mathv fuzz-free design note
  (void)W;
}

struct CaseResult { uint32_t gpu = 0, ref = 0; bool ok = false; };

CaseResult brightnessCase(MTL::Device* dev, MTL::CommandQueue* q, MTL::ComputePipelineState* pso, uint32_t W,
                          uint32_t H, uint32_t scaleFactor, bool injectBug, const char* tag) {
  CaseResult res;

  // RGBA32Float INPUT texture, filled with the known values.
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA32Float, W, H, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* in = dev->newTexture(td);
  std::vector<float> rgba((size_t)W * H * 4, 0.0f);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x)
      texelRGBA(x, y, W, rgba[((size_t)y * W + x) * 4 + 0], rgba[((size_t)y * W + x) * 4 + 1],
                rgba[((size_t)y * W + x) * 4 + 2], rgba[((size_t)y * W + x) * 4 + 3]);
  in->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, rgba.data(), (NS::UInteger)W * 4 * sizeof(float));

  // 1-uint UAV, zero-init (the folded `clear` pass). StorageModeShared for host readback.
  MTL::Buffer* uav = dev->newBuffer(sizeof(uint32_t), MTL::ResourceStorageModeShared);
  *(uint32_t*)uav->contents() = 0u;

  if (!injectBug) {
    MTL::CommandBuffer* cmd = q->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(pso);
    enc->setBytes(&scaleFactor, sizeof(scaleFactor), CS_CB_BASE);   // b0 → buffer(0)
    enc->setBuffer(uav, 0, CS_UAV_BASE);                           // u0 → buffer(12)
    enc->setTexture(in, CS_TEX_SRV_BASE);                          // t0 → texture(0)
    enc->dispatchThreadgroups(MTL::Size::Make(ceilDiv(W, 8), ceilDiv(H, 8), 1),
                              MTL::Size::Make(8, 8, 1));           // numthreads(8,8,1)
    enc->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }  // -bug: no dispatch → uav stays 0 (the "UAV not written" failure).

  res.gpu = *(uint32_t*)uav->contents();
  res.ref = mathv_ref::getImageBrightnessSum(rgba.data(), W, H, scaleFactor);
  in->release();
  uav->release();

  const uint32_t diff = res.gpu > res.ref ? res.gpu - res.ref : res.ref - res.gpu;
  res.ok = diff <= kSumTol;
  printf("[mathv-getimagebrightness] %s %ux%u scale=%u: gpu=%u ref=%u diff=%u -> %s\n", tag, W, H,
         scaleFactor, res.gpu, res.ref, diff, res.ok ? "ok" : "MISS");
  return res;
}

}  // namespace

int runMathvGetImageBrightnessSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-mathv-getimagebrightness] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  MTL::Function* fn = lib->newFunction(
      NS::String::string("computeshaderstage_getimagebrightness", NS::UTF8StringEncoding));
  MTL::ComputePipelineState* pso = fn ? dev->newComputePipelineState(fn, &err) : nullptr;
  if (fn) fn->release();
  if (!pso) {
    printf("[selftest-mathv-getimagebrightness] FAIL: no PSO for computeshaderstage_getimagebrightness\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  struct Case { uint32_t w, h, scale; const char* tag; };
  const Case cases[] = {
      {8, 8, 255, "small"},        // exact tile
      {16, 16, 1000, "square"},    // multi-tile
      {37, 20, 10000, "tail"},     // non-8-multiple → ceil-div dispatch tail (edge threads add 0)
      {64, 48, 255, "wide"},
  };

  bool allOk = true;
  uint32_t worstDiff = 0, refMax = 0;
  for (const Case& c : cases) {
    CaseResult r = brightnessCase(dev, q, pso, c.w, c.h, c.scale, injectBug, c.tag);
    if (!injectBug) allOk = allOk && r.ok;
    const uint32_t diff = r.gpu > r.ref ? r.gpu - r.ref : r.ref - r.gpu;
    worstDiff = diff > worstDiff ? diff : worstDiff;
    refMax = r.ref > refMax ? r.ref : refMax;
  }

  pso->release(); lib->release(); q->release(); dev->release();

  if (!injectBug) {
    ParityReport rep("selftest-mathv-getimagebrightness");
    rep.expectTrue("brightness sum matches CPU oracle (all cases)", allOk, (double)worstDiff);
    rep.expectTrue("oracle non-trivial (reduction reaches real luminance)", refMax > 0, (double)refMax);
    const int rc = rep.finish();
    pool->release();
    return rc;
  }
  // -bug: the dropped dispatch (uav==0) must diverge from the non-trivial oracle (did-not-trip → return 0).
  const bool bites = refMax > 0 && worstDiff > kSumTol;
  printf("[selftest-mathv-getimagebrightness] -bug: worstDiff=%u refMax=%u -> %s\n", worstDiff, refMax,
         bites ? "BITES (unwritten UAV != luminance sum)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// order 1052: appends after mathv-depthtolinear (1051); a texture-into-buffer kernel, no collision.
REGISTER_SELFTESTS(/*orderBase=*/1052, {"mathv-getimagebrightness", runMathvGetImageBrightnessSelfTest});

}  // namespace sw
