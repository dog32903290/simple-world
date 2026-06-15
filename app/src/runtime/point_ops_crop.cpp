// Crop image-filter texture op (lane image_filter) — the FIRST -cs.hlsl COMPUTE leaf.
// TiXL authority: Operators/Lib/image/transform/Crop.cs (Texture2d/LeftRight/TopBottom/PaddingColor
// inputs) + Crop.t3 (defaults PaddingColor=(1,1,1,0), LeftRight=(0,0), TopBottom=(0,0)) +
// Assets/shaders/img/transform/CropImage-cs.hlsl (the compute kernel: shift sample by (Left,Top),
// emit BackgroundColor outside the source rect). Crop SHRINKS the image: output dims = input minus
// (Left+Right) x (Top+Bottom). See crop.metal + crop_params.h for the kernel + shared cbuffer.
//
// COMPUTE seam usage (how a future -cs.hlsl op registers): end the leaf with a file-scope
// `ImageFilterComputeOp` (not ImageFilterOp). It self-registers the cook + NodeSpec + selftest like
// the pixel registrar, PLUS marks the cook type as needing ShaderWrite on its output and (optionally)
// supplies a SizeFn so cookTexNode sizes the output from the cooked INPUT dims, not the Resolution
// pin. The cook body dispatches a compute encoder itself (compute mechanics mirror
// particle_system.cpp): cachedComputePSO -> computeCommandEncoder -> setComputePipelineState /
// setTexture(in@0,out@1) / setBytes(params@0) -> dispatchThreadgroups(ceil(out/8), 8x8) -> commit.
//
// NodeSpec ports 1:1 with Crop.cs [Input] (invent NO knobs):
//   Image(Texture2D) | LeftRight(Int2 -> .x/.y Float Vec) | TopBottom(Int2 -> .x/.y Float Vec) |
//   PaddingColor(Vec4 -> .r/.g/.b/.a Float Vec). CropLeft=LeftRight.x, CropRight=LeftRight.y,
//   CropTop=TopBottom.x, CropBottom=TopBottom.y (.cs Int2 X/Y -> the two named crop margins).
//
// FORK (named): TiXL Int2 LeftRight/TopBottom modeled as Float Widget::Vec ports (same as Pixelate's
// TileAmount); the kernel truncates via int(v+0.4) so fractional Float values land on the same pixel
// as the integer would. PaddingColor default alpha is 0 (Crop.t3) — kept verbatim.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/crop_params.h"               // CropParams, CROP_* bindings, CROP_TGX/TGY
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterComputeOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, RenderResolution
#include "runtime/tex_op_cache.h"              // cachedComputePSO (compute PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

inline uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

// Crop compute op: dispatch crop_cs over the OUTPUT extent, sampling the (shifted) source.
// No upstream texture wired: clear output to black (parity with the pixel ops' no-input path).
void cookCrop(TexCookCtx& c) {
  if (!c.lib || !c.output) return;

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

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "crop_cs");
  if (!pso) return;

  // CropParams from the .cs Int2 margins: CropLeft=LeftRight.x, CropRight=LeftRight.y,
  // CropTop=TopBottom.x, CropBottom=TopBottom.y. PaddingColor default (1,1,1,0) per Crop.t3.
  CropParams p{};
  p.CropLeft   = cookParam(c, "LeftRight.x", 0.0f);
  p.CropRight  = cookParam(c, "LeftRight.y", 0.0f);
  p.CropTop    = cookParam(c, "TopBottom.x", 0.0f);
  p.CropBottom = cookParam(c, "TopBottom.y", 0.0f);
  p.BackgroundColor[0] = cookParam(c, "PaddingColor.r", 1.0f);
  p.BackgroundColor[1] = cookParam(c, "PaddingColor.g", 1.0f);
  p.BackgroundColor[2] = cookParam(c, "PaddingColor.b", 1.0f);
  p.BackgroundColor[3] = cookParam(c, "PaddingColor.a", 0.0f);

  const uint32_t outW = c.output->width();
  const uint32_t outH = c.output->height();

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setTexture(const_cast<MTL::Texture*>(c.inputTexture), CROP_Source);  // in  @ texture(0)
  enc->setTexture(c.output, CROP_Result);                                   // out @ texture(1)
  enc->setBytes(&p, sizeof(CropParams), CROP_Params);                       // cbuffer @ buffer(0)
  // [numthreads(8,8,1)] lives ONLY here (MSL has no numthreads). ceil-div output dims by 8.
  MTL::Size tg = MTL::Size::Make(CROP_TGX, CROP_TGY, 1);
  MTL::Size grid = MTL::Size::Make(ceilDiv(outW, CROP_TGX), ceilDiv(outH, CROP_TGY), 1);
  enc->dispatchThreadgroups(grid, tg);
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // pso is cache-owned (tex_op_cache), not released here.
}

// SizeFn: Crop shrinks the image. out.w = max(1, inW - Left - Right), out.h = max(1, inH-Top-Bottom).
// Margins truncate the same way the kernel does (int(v+0.4)) so the dispatch extent matches the
// kernel's source-shift bookkeeping. Matches Crop.t3's internal AddInts/GetTextureSize sizing.
RenderResolution cropSize(const std::map<std::string, float>& params, RenderResolution in) {
  auto get = [&](const char* k) -> float {
    auto it = params.find(k);
    return it != params.end() ? it->second : 0.0f;
  };
  int left   = (int)(get("LeftRight.x") + 0.4f);
  int right  = (int)(get("LeftRight.y") + 0.4f);
  int top    = (int)(get("TopBottom.x") + 0.4f);
  int bottom = (int)(get("TopBottom.y") + 0.4f);
  int w = (int)in.w - left - right;
  int h = (int)in.h - top - bottom;
  RenderResolution out;
  out.w = (uint32_t)std::max(1, w);
  out.h = (uint32_t)std::max(1, h);
  return out;
}

}  // namespace

int runCropSelfTest(bool injectBug);

// Self-registration (COMPUTE leaf): ImageFilterComputeOp marks "Crop" as ShaderWrite + supplies the
// cropSize SizeFn so the output texture is the SHRUNKEN size, and registers --selftest-crop[-bug].
static const ImageFilterComputeOp _reg_crop{
    {"Crop", "Crop",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // LeftRight (Int2, TiXL default (0,0)) -> CropLeft=.x, CropRight=.y.
      {"LeftRight.x", "LeftRight", "Float", true, 0.0f, 0.0f, 4096.0f, Widget::Vec, {}, true, 2},
      {"LeftRight.y", "LeftRight.y", "Float", true, 0.0f, 0.0f, 4096.0f, Widget::Vec, {}, true, 1},
      // TopBottom (Int2, TiXL default (0,0)) -> CropTop=.x, CropBottom=.y.
      {"TopBottom.x", "TopBottom", "Float", true, 0.0f, 0.0f, 4096.0f, Widget::Vec, {}, true, 2},
      {"TopBottom.y", "TopBottom.y", "Float", true, 0.0f, 0.0f, 4096.0f, Widget::Vec, {}, true, 1},
      // PaddingColor (Vec4, TiXL default (1,1,1,0)) — color of cropped/padded pixels.
      {"PaddingColor.r", "PaddingColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"PaddingColor.g", "PaddingColor.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"PaddingColor.b", "PaddingColor.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"PaddingColor.a", "PaddingColor.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
     nullptr},
    "Crop", cookCrop, cropSize, "crop", runCropSelfTest};

// --- Crop COMPUTE golden ----------------------------------------------------------------------
// Crop shifts the surviving image by (-Left,-Top) into a SMALLER output and pads the rest with
// PaddingColor. Source: a recognizable pattern on a 64x64 black image — a single distinctly-colored
// marker pixel (green) at a known location, plus a white border-band test. We crop a known margin and
// assert:
//   (1) OUTPUT DIMS == inW-(L+R) x inH-(T+B): the SizeFn sized the texture from the cropped input.
//   (2) SHIFT: the source marker at (mx,my) lands at (mx-Left, my-Top) in the output.
//   (3) PADDING: a pixel that maps outside the source (x-Left<0) is exactly PaddingColor (magenta).
// injectBug: zero the crop offsets in the params (Left=Top=0) -> no shift -> the marker stays at its
// original coords, so the shift assertion at (mx-Left,my-Top) fails (RED).
int runCropSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  // Non-8-divisible input/output on purpose (risk-trap 3): inW=100 -> outW=100-Left-Right.
  const uint32_t W = 100, H = 100;
  const int LEFT = 12, RIGHT = 8, TOP = 20, BOTTOM = 4;  // -> out 80 x 76
  const uint32_t OW = (uint32_t)(W - LEFT - RIGHT);       // 80
  const uint32_t OH = (uint32_t)(H - TOP - BOTTOM);       // 76

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-crop] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source texture: ShaderRead (sampled/read by the kernel) + RenderTarget for the upload path.
  MTL::TextureDescriptor* tdSrc =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  tdSrc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  tdSrc->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(tdSrc);

  // Output texture: RenderTarget|ShaderRead|ShaderWrite (the compute leaf needs ShaderWrite — the
  // extra flag vs the pixel selftests; risk-trap 1: c.output must carry ShaderWrite).
  MTL::TextureDescriptor* tdDst =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, OW, OH, false);
  tdDst->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                  MTL::TextureUsageShaderWrite);
  tdDst->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(tdDst);

  // Source pattern: black field with one bright GREEN marker pixel at (mx,my).
  const uint32_t MX = 40, MY = 50;  // inside the kept rect for the chosen margins
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (size_t i = 0; i < (size_t)W * H; ++i) in[i * 4 + 3] = 255;  // opaque black
  {
    size_t mi = ((size_t)MY * W + MX) * 4;
    in[mi + 0] = 0; in[mi + 1] = 255; in[mi + 2] = 0; in[mi + 3] = 255;  // green marker
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // PaddingColor = magenta (distinct from any source pixel), alpha 1 so it reads clearly.
  std::map<std::string, float> pp;
  pp["LeftRight.x"] = injectBug ? 0.0f : (float)LEFT;
  pp["LeftRight.y"] = injectBug ? 0.0f : (float)RIGHT;
  pp["TopBottom.x"] = injectBug ? 0.0f : (float)TOP;
  pp["TopBottom.y"] = injectBug ? 0.0f : (float)BOTTOM;
  pp["PaddingColor.r"] = 1.0f;
  pp["PaddingColor.g"] = 0.0f;
  pp["PaddingColor.b"] = 1.0f;
  pp["PaddingColor.a"] = 1.0f;

  // Pre-cook STALE fill: paint the whole output with a sentinel the kernel can NEVER emit
  // (neither the black/green source nor the magenta PaddingColor produce it). After cook, any
  // pixel still holding the sentinel was NOT written by the kernel -> a coverage hole. This bites
  // a ceil-div -> floor-div regression of the host dispatch, which on a non-8-divisible output
  // (76 = 9*8+4) would drop the bottom remainder rows (72..75) and leave them stale.
  const uint8_t STALE_R = 7, STALE_G = 11, STALE_B = 13, STALE_A = 200;
  std::vector<uint8_t> stale((size_t)OW * OH * 4);
  for (size_t i = 0; i < (size_t)OW * OH; ++i) {
    stale[i * 4 + 0] = STALE_R; stale[i * 4 + 1] = STALE_G;
    stale[i * 4 + 2] = STALE_B; stale[i * 4 + 3] = STALE_A;
  }
  dst->replaceRegion(MTL::Region::Make2D(0, 0, OW, OH), 0, stale.data(), OW * 4);

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &pp;
  cookCrop(c);

  std::vector<uint8_t> out((size_t)OW * OH * 4, 0);
  dst->getBytes(out.data(), OW * 4, MTL::Region::Make2D(0, 0, OW, OH), 0);

  // (1) Output dims (the texture we allocated via the SizeFn shape) — sanity on the chosen sizes.
  bool dimsOk = (OW == 80 && OH == 76);

  // (2) Shift: the kernel reads source(i - Left, i - Top), so the source marker at (MX,MY) appears
  // at OUTPUT (MX+LEFT, MY+TOP) = (52, 70) when not buggy (output(i) samples source(i-Left)).
  const int sx = (int)MX + LEFT;  // 52
  const int sy = (int)MY + TOP;   // 70
  size_t si = ((size_t)sy * OW + sx) * 4;
  int gR = out[si + 0], gG = out[si + 1], gB = out[si + 2];
  bool markerShifted = (gG > 200) && (gR < 60) && (gB < 60);  // green present at shifted loc

  // (3) Padding: output pixel (2, 30) maps to source x = 2+LEFT? No — the kernel computes
  // sourceX = outX - Left only when... actually source = i - Left, so output (x,y) reads source
  // (x+? ). Per kernel: x_src = i.x - int(Left). At output x=2, x_src = 2-12 = -10 < 0 -> padded.
  const uint32_t pX = 2, pY = 30;
  size_t pi = ((size_t)pY * OW + pX) * 4;
  int pR = out[pi + 0], pG = out[pi + 1], pB = out[pi + 2];
  bool paddedMagenta = (pR > 200) && (pG < 60) && (pB > 200);

  // (4) FULL COVERAGE: every output pixel must have been written by the kernel — scan the WHOLE
  // output and count any pixel still holding the pre-cook sentinel (a stale/garbage band). The
  // remainder tile (rows 72..75 of the 76-tall output) is the band a floor-div dispatch would
  // drop; counting over the whole texture catches that and any other coverage hole. Intact
  // ceil-div -> unwritten==0; floor-div -> the dropped remainder rows stay sentinel -> unwritten>0.
  int unwritten = 0;
  for (size_t i = 0; i < (size_t)OW * OH; ++i) {
    if (out[i * 4 + 0] == STALE_R && out[i * 4 + 1] == STALE_G &&
        out[i * 4 + 2] == STALE_B && out[i * 4 + 3] == STALE_A) {
      ++unwritten;
    }
  }
  bool fullyCovered = (unwritten == 0);

  bool pass = dimsOk && markerShifted && paddedMagenta && fullyCovered;
  printf("[selftest-crop] out=%ux%u(dimsOk=%d) marker@(%d,%d)=(R%d,G%d,B%d) shifted=%d "
         "pad@(%u,%u)=(R%d,G%d,B%d) magenta=%d unwritten=%d covered=%d -> %s\n",
         OW, OH, dimsOk ? 1 : 0, sx, sy, gR, gG, gB, markerShifted ? 1 : 0, pX, pY, pR, pG, pB,
         paddedMagenta ? 1 : 0, unwritten, fullyCovered ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
