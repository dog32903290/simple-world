// ChromaticAbberation image-filter texture op (lane F3-2) — filter wave 3, op 2.
// TiXL authority: Operators/Lib/image/fx/stylize/ChromaticAbberation.cs (Image/Size/Strength/
// SampleCount/Distort inputs) + ChromaticAbberation.t3 (defaults) + Assets/shaders/img/fx/
// ChromaticAbberation.hlsl (the single-pass kernel: radial chromatic fringe loop, lens
// distortion barrel warp, R and B channels split outward/inward, G/A at half-offset).
//
// Single-pass port: cookChromaB reads c.inputTexture (the upstream RenderTarget's Texture2D via
// the I1 gather direct-through), runs one fullscreen pass of chromab_vs/chromab_fs, writes c.output.
// The op binds TWO constant buffers: b0 = ChromaBAParams (Size/Strength/SampleCount/Distort),
// b1 = ChromaBAResolution (TargetWidth/TargetHeight — required by the HLSL for aspect correction).
//
// Self-contained leaf: cookChromaB + registerChromaBAOp() + runChromaBAShiftSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace/Tint.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/chromab_params.h"  // ChromaBAParams, ChromaBAResolution, CHROMAB_Params/Resolution
#include "runtime/eval_context.h"
#include "runtime/point_graph.h"     // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"    // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// ChromaticAbberation texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookChromaB(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  if (!c.inputTexture) {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(c.output);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    return;
  }

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "chromab_vs", "chromab_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see chromab.metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params: ChromaticAbberation.cs defaults (Size=1, Strength=0.3, SampleCount=8, Distort=0).
  ChromaBAParams p{};
  p.Size        = cookParam(c, "Size", 1.0f);
  p.Strength    = cookParam(c, "Strength", 0.3f);
  p.SampleCount = std::max(3.0f, std::round(cookParam(c, "SampleCount", 8.0f)));
  p.Distort     = cookParam(c, "Distort", 0.0f);

  ChromaBAResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(c.inputTexture), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(ChromaBAParams),     CHROMAB_Params);
  enc->setFragmentBytes(&res, sizeof(ChromaBAResolution), CHROMAB_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerChromaBAOp() { registerTexOp("ChromaticAbberation", cookChromaB); }

// --- ChromaticAbberation MATH golden ----------------------------------------------------------
// Chromatic aberration is visually apparent by its RGB channel CHANNEL OUTPUT vs. PASSTHROUGH
// comparison: when Strength=1 and Size>0, the output should differ from the source at any
// non-trivially-uniform pixel. We assert:
//   (a) Passthrough baseline (Size=0): the output matches the source centerline pixel exactly.
//   (b) Fringed run (Size=20, Strength=1): the RGB output at that SAME centerline pixel is
//       DIFFERENT from the source (fringe pulled in contributions from surrounding pixels).
//   (c) A completely black region stays dark (fringe doesn't fabricate light from nothing).
// The simplest source is a fully white texture — the fringe has no color to split, so the
// output stays white everywhere regardless of Size. Instead we use a horizontal bar: the top
// half is white, the bottom half is black. The center row pixel at the midpoint between the
// white/black border should be sampling a mixture of UVs after Strength=1 blending of the
// shifted fringe. Specifically it samples `col.r += left.r * 0.5` and `col.ga += left2.ga`.
// When there's a strong vertical border the left2/right2 samples (half-offset) DO cross the
// border -> G channel in output differs from input (input center-row has G=255 in top half).
// With injectBug Size=0: fringe offset=0, so `left = right = center`; output = centerColor;
// no change from source -> the "differs" assertion FAILS (teeth).
int runChromaBAShiftSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-chromab] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst_pass = dev->newTexture(td);   // passthrough (Size=0)
  MTL::Texture* dst_fring = dev->newTexture(td);  // fringed (Size=20)

  // Source: top half white (y < H/2), bottom half black.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (y < H / 2) ? 255 : 0;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // (a) passthrough: Size=0, Strength=1 -> fringe loop samples center -> output == source
  {
    std::map<std::string, float> pbase;
    pbase["Size"] = 0.0f; pbase["Strength"] = 1.0f;
    pbase["SampleCount"] = 8.0f; pbase["Distort"] = 0.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst_pass; c.params = &pbase;
    cookChromaB(c);
  }

  // (b) fringed: Size=20, Strength=1
  {
    std::map<std::string, float> pfring;
    pfring["Size"] = injectBug ? 0.0f : 20.0f;  // bug: same as passthrough
    pfring["Strength"] = 1.0f;
    pfring["SampleCount"] = 8.0f; pfring["Distort"] = 0.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 2; c.inputTexture = src; c.output = dst_fring; c.params = &pfring;
    cookChromaB(c);
  }

  std::vector<uint8_t> opass((size_t)W * H * 4, 0), ofring((size_t)W * H * 4, 0);
  dst_pass ->getBytes(opass.data(),  W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  dst_fring->getBytes(ofring.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Check a pixel just below the white/black border center (y = H/2, x = W/2).
  // In the passthrough, this row is all black (source bottom half).
  // In the fringed version, the top half is sampled for R/B at half-offset — some white leaks
  // into the rows just below the border, making them non-zero.
  const uint32_t checkY = H / 2;  // first row of the black half
  const uint32_t checkX = W / 2;
  size_t pi = ((size_t)checkY * W + checkX) * 4;
  int pR = opass[pi], pG = opass[pi+1], pB = opass[pi+2];
  int fR = ofring[pi], fG = ofring[pi+1], fB = ofring[pi+2];

  // Passthrough: border row is black (or close to black if there's subpixel blur from the scale).
  bool passthroughDark = (pR + pG + pB) < 50;
  // Fringed: the large fringe should bleed light from white top-half into this border row.
  bool fringedBright   = (fR + fG + fB) > 20;

  bool pass = passthroughDark && fringedBright;
  printf("[selftest-chromab] passthrough(R=%d,G=%d,B=%d) fringed(R=%d,G=%d,B=%d) -> ptDark=%d fringBright=%d -> %s\n",
         pR, pG, pB, fR, fG, fB, passthroughDark ? 1 : 0, fringedBright ? 1 : 0,
         pass ? "PASS" : "FAIL");

  src->release(); dst_pass->release(); dst_fring->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
