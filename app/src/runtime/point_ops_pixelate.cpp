// Pixelate image-filter texture op (lane image_filter) — per-pixel tile quantizer.
// TiXL authority: Operators/Lib/image/fx/stylize/Pixelate.cs (Image/Color/Divisor/TileAmount/
// Shape inputs) + Pixelate.t3 (defaults Color=(1,1,1,1), Divisor=0, TileAmount=(160,90),
// Shape=Lib:images/basic/white.png, WrapMode=Clamp) + Assets/shaders/img/fx/Pixelate.hlsl
// (the single-pass kernel: snap UV to tile center, point-sample, multiply by Color and the
// per-cell Shape).
//
// Single-pass port: cookPixelate reads c.inputTexture (upstream Texture2D), runs one fullscreen
// pass of pixelate_vs/pixelate_fs, writes c.output. Binds b0 = PixelateParams (Color/Divisor/
// TileAmount), b1 = PixelateResolution (source image dims, the HLSL's GetDimensions).
//
// FORK (named — Shape omitted): see pixelate.metal / pixelate_params.h. Default Shape is white,
// so the default node = our single-input op exactly; tileShape folded to constant 1.0.
//
// Self-contained leaf: cookPixelate + registerPixelateOp() + runPixelateSelfTest.
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
#include "runtime/pixelate_params.h"  // PixelateParams, PixelateResolution, PIXELATE_Params/Resolution
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"      // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"     // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Pixelate texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookPixelate(TexCookCtx& c) {
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

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "pixelate_vs", "pixelate_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (Pixelate.t3 WrapMode=Clamp)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params: Pixelate.t3 defaults — Color=(1,1,1,1), Divisor=0, TileAmount=(160,90).
  PixelateParams p{};
  p.ColorR = cookParam(c, "Color.r", 1.0f);
  p.ColorG = cookParam(c, "Color.g", 1.0f);
  p.ColorB = cookParam(c, "Color.b", 1.0f);
  p.ColorA = cookParam(c, "Color.a", 1.0f);
  p.Divisor = cookParam(c, "Divisor", 0.0f);
  p.TileAmountX = cookParam(c, "TileAmount.x", 160.0f);
  p.TileAmountY = cookParam(c, "TileAmount.y", 90.0f);

  // PixelateResolution carries the SOURCE image dims = HLSL Image.GetDimensions (tile math),
  // not the output size. Input/output are the same size for a fullscreen filter, but the kernel
  // reads dims of the texture it samples — use the input's.
  PixelateResolution res{};
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
  enc->setFragmentBytes(&p,   sizeof(PixelateParams),     PIXELATE_Params);
  enc->setFragmentBytes(&res, sizeof(PixelateResolution), PIXELATE_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. NodeSpec literal moved verbatim from node_registry_image_filter.cpp.
static const ImageFilterOp _reg_pixelate{
    // Pixelate (TiXL Lib.image.fx.stylize.Pixelate): tile-quantize the image into a grid, point-
    // sample each cell center, multiply by Color. Single Texture2D in → Texture2D out
    // (point_ops_pixelate.cpp). Kernel: Pixelate.hlsl — Divisor>0.5 -> floor(res/(Divisor*2))
    // tiles, else TileAmount tiles; uv snapped to tile center; SampleLevel point sample.
    // Params mirror Pixelate.cs/.t3: Image/Color(Vec4)/Divisor(int)/TileAmount(Int2). FORKS
    // (named): TiXL's Shape texture input omitted (default Shape=white.png = no-op multiplier,
    // see point_ops_pixelate.cpp); Int Divisor/TileAmount modeled as Float (same as SampleCount);
    // fixed clamp sampler (Pixelate.t3 WrapMode=Clamp verbatim).
    {"Pixelate", "Pixelate",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Divisor (int, TiXL t3 default 0 — Divisor<=0.5 path uses TileAmount).
      {"Divisor", "Divisor", "Float", true, 0.0f, 0.0f, 64.0f},
      // TileAmount (Int2, TiXL t3 default (160,90)).
      {"TileAmount.x", "TileAmount", "Float", true, 160.0f, 1.0f, 1024.0f, Widget::Vec, {}, true, 2},
      {"TileAmount.y", "TileAmount.y", "Float", true, 90.0f, 1.0f, 1024.0f, Widget::Vec, {}, true, 1},
      // Color (Vec4, TiXL t3 default (1,1,1,1) — output multiplier, identity = white).
      {"Color.r", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Color.g", "Color.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.b", "Color.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.a", "Color.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Pixelate", cookPixelate, "pixelate", runPixelateSelfTest};

// --- Pixelate MATH golden ---------------------------------------------------------------------
// Pixelate quantizes the image into a tile grid: every output pixel inside one tile samples the
// SAME tile-center color, so a finely-varying source becomes flat blocks. We assert pixel-level:
//   (a) BLOCK UNIFORMITY: with a coarse Divisor (few big tiles), two pixels that fall in the same
//       tile have the EQUAL output color, even though those two pixels are DIFFERENT in the source.
//   (b) COLOR MULTIPLIER: Color=(1,0,0,1) zeroes G and B in the output (return ... * Color).
//   (c) PASSTHROUGH SANITY: in the source the two probed pixels differ (so (a) is a real merge,
//       not a trivially-uniform image).
// Source: a high-frequency vertical stripe pattern (alternating columns full-white / mid-grey),
// 128x128. With Divisor=8 -> divisor=16 -> dimensions=floor(128/16)=8 tiles -> tileSize=1/8 ->
// each tile spans 16 px. Two pixels 4 px apart sit in the same tile -> same quantized color, but
// in the raw stripe source they land on different columns -> differ.
// injectBug: Divisor=0 -> divisor=0 -> dimensions=floor(128/0)=inf, and the Divisor>0.5 branch is
// FALSE so it uses TileAmount=(160,90) -> tiles smaller than a pixel -> NO quantization merge ->
// the two same-tile pixels stay DIFFERENT -> uniformity assertion FAILS (teeth).
int runPixelateSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-pixelate] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: high-frequency vertical stripes — even columns white, odd columns mid-grey.
  // (Two horizontally-adjacent columns differ -> a real per-pixel variation to be merged.)
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (x % 2 == 0) ? 255 : 120;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Pixelate: Divisor=8 (coarse), Color=(1,0,0,1) (red-only multiplier).
  {
    std::map<std::string, float> pp;
    pp["Divisor"]    = injectBug ? 0.0f : 8.0f;  // bug: Divisor=0 -> TileAmount(160,90) -> sub-pixel tiles
    pp["Color.r"]    = 1.0f;
    pp["Color.g"]    = 0.0f;  // multiplier zeroes green
    pp["Color.b"]    = 0.0f;  // multiplier zeroes blue
    pp["Color.a"]    = 1.0f;
    pp["TileAmount.x"] = 160.0f;
    pp["TileAmount.y"] = 90.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &pp;
    cookPixelate(c);
  }

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Two probe pixels in the SAME tile (Divisor=8 -> 8 tiles -> 16px/tile). Pick x=4 and x=8 on
  // the same row y=64: both inside tile 0 (x in [0,16)). They sit on different source columns
  // (col 4 = even=white, col 8 = even=white... pick x=4 even and x=7 odd to guarantee source diff).
  const uint32_t pY = 64;
  const uint32_t aX = 4;   // even col -> source white
  const uint32_t bX = 7;   // odd col  -> source grey (different from aX in the raw source)
  size_t ai = ((size_t)pY * W + aX) * 4;
  size_t bi = ((size_t)pY * W + bX) * 4;

  int aR = out[ai], aG = out[ai+1], aB = out[ai+2];
  int bR = out[bi], bG = out[bi+1], bB = out[bi+2];

  // (c) source sanity: the two probe pixels DIFFER in the source (else the merge is vacuous).
  int saR = in[ai], sbR = in[bi];
  bool sourceDiffers = std::abs(saR - sbR) > 30;

  // (a) block uniformity: same tile -> equal output (allow tiny rounding slack).
  bool blockUniform = (std::abs(aR - bR) <= 2) && (std::abs(aG - bG) <= 2) && (std::abs(aB - bB) <= 2);

  // (b) color multiplier: green & blue zeroed by Color=(1,0,0,1); red retained (>0).
  bool greenZeroed = (aG <= 2) && (bG <= 2);
  bool blueZeroed  = (aB <= 2) && (bB <= 2);
  bool redKept     = (aR > 50);

  bool pass = sourceDiffers && blockUniform && greenZeroed && blueZeroed && redKept;
  printf("[selftest-pixelate] srcDiff(%d vs %d)=%d  outA(R=%d,G=%d,B=%d) outB(R=%d,G=%d,B=%d) "
         "-> uniform=%d gZero=%d bZero=%d rKept=%d -> %s\n",
         saR, sbR, sourceDiffers ? 1 : 0, aR, aG, aB, bR, bG, bB,
         blockUniform ? 1 : 0, greenZeroed ? 1 : 0, blueZeroed ? 1 : 0, redKept ? 1 : 0,
         pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
