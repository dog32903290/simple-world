// ChromaticDistortion image-filter texture op (lane image_filter).
// TiXL authority: external/tixl/Operators/Lib/image/fx/distort/ChromaticDistortion.cs (ports) +
// ChromaticDistortion.t3 (defaults) + Assets/shaders/img/fx/ChromaticDistortion.hlsl (kernel).
//
// Single-pass port: cookChromaticDistortion reads c.inputTexture (upstream Texture2D via the
// gather direct-through), runs one fullscreen pass of chromaticdistortion_vs/_fs, writes c.output.
// No upstream texture wired: clear output to black.
//
// The op binds ONE constant buffer (b0 = ChromaticDistortionParams). The HLSL's b1 TimeConstants
// cbuffer is framework-injected and UNUSED by the kernel — not bound, not a port.
//
// Self-contained leaf: cookChromaticDistortion + registerChromaticDistortionOp() +
// runChromaticDistortionSelfTest. Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h).
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/chromaticdistortion_params.h"  // ChromaticDistortionParams, CHROMADIST_Params
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"                 // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"                // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// ChromaticDistortion texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookChromaticDistortion(TexCookCtx& c) {
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

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "chromaticdistortion_vs", "chromaticdistortion_fs", fmt);  // D2-2
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see .metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL ChromaticDistortion.cs defaults from ChromaticDistortion.t3:
  // Center (0,0), Size 0.05, Colorize 0.1, Distort 0.1, DistortOffset 0.5, ScaleImage 1.0,
  // SampleCount 16.
  ChromaticDistortionParams p{};
  p.CenterX       = cookParam(c, "Center.x", 0.0f);
  p.CenterY       = cookParam(c, "Center.y", 0.0f);
  p.Size          = cookParam(c, "Size", 0.05f);
  p.Colorize      = cookParam(c, "Colorize", 0.1f);
  p.Distort       = cookParam(c, "Distort", 0.1f);
  p.DistortOffset = cookParam(c, "DistortOffset", 0.5f);
  p.ScaleImage    = cookParam(c, "ScaleImage", 1.0f);
  p.SampleCount   = cookParam(c, "SampleCount", 16.0f);  // int in TiXL; cast in shader

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
  enc->setFragmentBytes(&p, sizeof(ChromaticDistortionParams), CHROMADIST_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. NodeSpec literal moved verbatim from node_registry_image_filter.cpp.
static const ImageFilterOp _reg_chromaticdistortion{
    // ChromaticDistortion (TiXL Lib.image.fx.distort.ChromaticDistortion): radial bulge warp +
    // N-sample chromatic radial blur. Single Texture2D in → Texture2D out
    // (point_ops_chromaticdistortion.cpp). Kernel: ChromaticDistortion.hlsl — chromaShift()
    // splits R/B from opposite ends of the radial sample line, lerp blurred<->chromarized by
    // Colorize. Params mirror ChromaticDistortion.cs/.t3: Texture2d/Center(Vec2)/Size/Colorize/
    // Distort/DistortOffset/ScaleImage/SampleCount(int). FORKS (named): b1 TimeConstants cbuffer
    // unused -> omitted; fixed clamp sampler; SampleCount Int modeled as Float.
    {"ChromaticDistortion", "ChromaticDistortion",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Center (Vec2, TiXL t3 default (0,0)).
      {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Size (float, TiXL t3 default 0.05).
      {"Size", "Size", "Float", true, 0.05f, 0.0f, 1.0f},
      // Colorize (float, TiXL t3 default 0.1).
      {"Colorize", "Colorize", "Float", true, 0.1f, 0.0f, 1.0f, Widget::Slider},
      // Distort (float, TiXL t3 default 0.1).
      {"Distort", "Distort", "Float", true, 0.1f, -2.0f, 2.0f},
      // DistortOffset (float, TiXL t3 default 0.5).
      {"DistortOffset", "DistortOffset", "Float", true, 0.5f, 0.0f, 2.0f},
      // ScaleImage (float, TiXL t3 default 1.0).
      {"ScaleImage", "ScaleImage", "Float", true, 1.0f, 0.1f, 4.0f},
      // SampleCount (int, TiXL t3 default 16; clamped to even 1..100 in shader).
      {"SampleCount", "SampleCount", "Float", true, 16.0f, 1.0f, 100.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ChromaticDistortion", cookChromaticDistortion, "chromaticdistortion",
    runChromaticDistortionSelfTest};

// --- ChromaticDistortion MATH golden --------------------------------------------------------
// Source: left half white (x < W/2), right half black — a strong vertical edge at x=W/2, so the
// radial blur smears horizontally across it. The chromaShift() RGB weighting (R centred at the
// -1 sample, B at the +1 sample) pulls R and B from OPPOSITE ends of the radial sample line, so
// ALONG THE SMEARED RAMP (the partially-blended pixels straddling the edge) the output R and B
// channels separate. Fully-white and fully-black pixels stay achromatic (R==B); the separation
// only appears where the blur mixes the two — so we scan the centre row for the MAXIMUM |R-B|.
// Assert: maxSep ( max over the centre row of |R-B| ) > 12 — chromatic fringe present.
// injectBug Colorize=0: lerp picks the pure (grey) blurredSum -> R==B for every pixel -> maxSep=0
// -> assertion FAILS (teeth).
int runChromaticDistortionSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-chromaticdistortion] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: a vertical edge at x=W/4 (left quarter white, rest black). The edge is placed OFF the
  // image centre on purpose: ChromaticDistortion's radial smear `dir = Size*fromCenter*...` is
  // ~zero at the centre (fromCenter→0 there) and grows outward, so an edge at the centre would
  // show no fringe. At x≈0.25 the radial direction is strongly horizontal and the smear is large.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (x < W / 4) ? 255 : 0;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Use a large radial spread + off-centre focus so the smear is strong and horizontal.
  std::map<std::string, float> params;
  params["Center.x"] = 0.0f; params["Center.y"] = 0.0f;
  params["Size"] = 0.3f;            // strong spread
  params["Colorize"] = injectBug ? 0.0f : 1.0f;  // bug: no chromatic split -> R==B
  params["Distort"] = 0.0f;         // keep geometry plain so smear stays horizontal across edge
  params["DistortOffset"] = 0.5f;
  params["ScaleImage"] = 1.0f;
  params["SampleCount"] = 32.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookChromaticDistortion(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Scan the centre row (y=H/2) for the maximum chromatic separation |R-B|.
  const uint32_t scanY = H / 2;
  int maxSep = 0, sepX = 0, sR = 0, sB = 0;
  for (uint32_t x = 0; x < W; ++x) {
    size_t i = ((size_t)scanY * W + x) * 4;
    int R = out[i], B = out[i + 2];
    int sep = std::abs(R - B);
    if (sep > maxSep) { maxSep = sep; sepX = (int)x; sR = R; sB = B; }
  }

  bool pass = maxSep > 12;  // injectBug Colorize=0 -> R==B everywhere -> maxSep=0 -> fails
  printf("[selftest-chromaticdistortion] maxSep=%d at x=%d (R=%d B=%d, need>12) -> %s\n",
         maxSep, sepX, sR, sB, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
