// Sharpen image-filter texture op (lane image_filter) — 3x3 desaturated-Laplacian unsharp mask.
// TiXL authority: Operators/Lib/image/fx/blur/Sharpen.cs (Image/SampleRadius/Strength/Clamping
// inputs) + Sharpen.t3 (defaults SampleRadius=1.0, Strength=1.0, Clamping=false, Wrap=MirrorOnce,
// OutputFormat=R16G16B16A16_Float) + Assets/shaders/img/fx/Sharpen.hlsl (the single-pass kernel:
// final = col + col*Strength*(8*L(center) - sum of 8 neighbour luminances), optional saturate).
//
// Single-pass port: cookSharpen reads c.inputTexture (upstream Texture2D), runs one fullscreen
// pass of sharpen_vs/sharpen_fs, writes c.output. Binds b0 = SharpenParams (SampleRadius/Strength/
// Clamping), b1 = SharpenResolution (source image dims = the HLSL's GetDimensions for pX/pY).
//
// FORKS (named): see sharpen.metal — (1) GetDimensions via cbuffer, (2) fixed clamp sampler vs
// TiXL Wrap=MirrorOnce (1px edge ring only), (3) OutputFormat R16F not adopted (our pipeline uses
// the output texture's own format, RGBA8 in the golden; HDR overshoot still measurable pre-clamp).
//
// Self-contained leaf: cookSharpen + registerSharpenOp() + runSharpenSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace/Tint/ChromaB/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"     // TexCookCtx, cookParam, registerTexOp
#include "runtime/sharpen_params.h"  // SharpenParams, SharpenResolution, SHARPEN_Params/Resolution
#include "runtime/tex_op_cache.h"    // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Sharpen texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookSharpen(TexCookCtx& c) {
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

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "sharpen_vs", "sharpen_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (TiXL Wrap=MirrorOnce)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params: Sharpen.t3 defaults — SampleRadius=1.0, Strength=1.0, Clamping=false.
  SharpenParams p{};
  p.SampleRadius = cookParam(c, "SampleRadius", 1.0f);
  p.Strength     = cookParam(c, "Strength", 1.0f);
  p.Clamping     = cookParam(c, "Clamping", 0.0f);

  // SharpenResolution carries the SOURCE image dims = HLSL Image.GetDimensions (pX/pY step).
  SharpenResolution res{};
  res.TargetWidth  = (float)c.inputTexture->width();
  res.TargetHeight = (float)c.inputTexture->height();

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
  enc->setFragmentBytes(&p,   sizeof(SharpenParams),     SHARPEN_Params);
  enc->setFragmentBytes(&res, sizeof(SharpenResolution), SHARPEN_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. NodeSpec literal moved verbatim from node_registry_image_filter.cpp.
static const ImageFilterOp _reg_sharpen{
    // Sharpen (TiXL Lib.image.fx.blur.Sharpen): 3x3 desaturated-Laplacian unsharp mask. Single
    // Texture2D in → Texture2D out (point_ops_sharpen.cpp). Kernel: Sharpen.hlsl —
    // final = col + col*Strength*(8*L(center) - 8 neighbour luminances), optional Clamping
    // saturate. Params mirror Sharpen.cs/.t3: Image/SampleRadius/Strength/Clamping. FORKS
    // (named): fixed clamp sampler vs TiXL Wrap=MirrorOnce (1px edge ring); TiXL OutputFormat
    // R16F not adopted (uses output texture's own format).
    {"Sharpen", "Sharpen",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // SampleRadius (float, TiXL t3 default 1.0).
      {"SampleRadius", "SampleRadius", "Float", true, 1.0f, 0.0f, 10.0f},
      // Strength (float, TiXL t3 default 1.0).
      {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 4.0f},
      // Clamping (bool, TiXL t3 default false).
      {"Clamping", "Clamping", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Sharpen", cookSharpen, "sharpen", runSharpenSelfTest};

// --- Sharpen MATH golden ----------------------------------------------------------------------
// Sharpen = unsharp mask: it OVERSHOOTS at edges (ringing — the bright side of an edge becomes
// brighter than the source, the dark side darker) and leaves flat regions unchanged (Laplacian=0).
// We assert pixel-level on a single vertical step edge:
//   (a) EDGE OVERSHOOT: a pixel on the BRIGHT side just past the edge is BRIGHTER in the output
//       than it was in the source (col + col*Strength*positive_laplacian > src). Clamping=false
//       so the overshoot is preserved (not clipped to 1.0).
//   (b) FLAT INTERIOR UNCHANGED: a pixel deep in the uniform bright region (no neighbour gradient)
//       stays ≈ source (Laplacian over equal neighbours = 0).
// Source: left half = mid-grey (100), right half = bright grey (200), full 128x128. The step is at
// x=W/2; the bright-side edge pixel (x = W/2) has darker neighbours to its left -> positive
// Laplacian -> overshoots up. A pixel at x = W-4 (deep bright interior) has equal neighbours -> 0.
// injectBug: Strength=0 -> final = col (passthrough) -> NO overshoot at the edge -> (a) FAILS.
// (This realizes the "center-weight cancels the neighbour subtraction" failure mode as a param
//  lever, matching the param-based injectBug convention of the other image-filter goldens.)
int runSharpenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-sharpen] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: left half mid-grey (100), right half bright grey (200). One vertical step edge at W/2.
  const uint8_t LO = 100, HI = 200;
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (x < W / 2) ? LO : HI;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Sharpen: SampleRadius=1, Strength=2 (strong, to make overshoot clearly above rounding),
  // Clamping=false. injectBug -> Strength=0 (passthrough).
  {
    std::map<std::string, float> pp;
    pp["SampleRadius"] = 1.0f;
    pp["Strength"]     = injectBug ? 0.0f : 2.0f;  // bug: passthrough -> no edge overshoot
    pp["Clamping"]     = 0.0f;                      // keep overshoot (don't saturate)
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &pp;
    cookSharpen(c);
  }

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // (a) edge overshoot: bright-side edge pixel (x = W/2, the first bright column) has darker
  //     left neighbours -> positive Laplacian -> output brighter than source HI(200).
  const uint32_t eY = 64, eX = W / 2;
  size_t ei = ((size_t)eY * W + eX) * 4;
  int edgeOutR = out[ei];
  int edgeSrcR = in[ei];  // = HI = 200
  bool edgeOvershoots = edgeOutR > edgeSrcR + 4;  // strictly brighter than source (beyond rounding)

  // (b) flat interior unchanged: deep bright region pixel (x = W-4) ≈ source HI.
  const uint32_t fY = 64, fX = W - 4;
  size_t fi = ((size_t)fY * W + fX) * 4;
  int flatOutR = out[fi];
  int flatSrcR = in[fi];  // = HI = 200
  bool flatUnchanged = std::abs(flatOutR - flatSrcR) <= 4;

  bool pass = edgeOvershoots && flatUnchanged;
  printf("[selftest-sharpen] edge(out=%d src=%d overshoot=%d)  flat(out=%d src=%d unchanged=%d) -> %s\n",
         edgeOutR, edgeSrcR, edgeOvershoots ? 1 : 0, flatOutR, flatSrcR, flatUnchanged ? 1 : 0,
         pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
