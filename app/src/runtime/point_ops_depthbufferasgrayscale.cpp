// DepthBufferAsGrayScale image-filter texture op (lane image_filter).
// TiXL authority: external/tixl/Operators/Lib/image/use/DepthBufferAsGrayScale.cs (ports) +
// DepthBufferAsGrayScale.t3 (defaults + FloatsToBuffer cbuffer routing) +
// Assets/shaders/img/post-fx/depth-to-linear.hlsl (kernel).
//
// Despite the name, the .cs input is a plain InputSlot<Texture2D> Texture2d — a regular single
// Texture2D in, single Texture2D out (NO depth-attachment seam). The kernel reads the .r channel of
// that texture AS a depth value and reverse-projects it into a linear distance, then remaps through
// OutputRange and optionally saturates.
//
// Single-pass port: cookDepthBufferAsGrayScale reads c.inputTexture, runs one fullscreen pass of
// depthbufferasgrayscale_vs/_fs, writes c.output. No upstream texture wired: clear output to black.
//
// The op binds ONE constant buffer (b0 = DepthBufferAsGrayScaleParams: Near/Far/OutrangeMin/
// OutrangeMax/ClampRange/Mode). Texture dims read in-shader (the TiXL GetTextureSize node — no
// Resolution cbuffer port).
//
// Self-contained leaf: cookDepthBufferAsGrayScale + registerTexOp (via ImageFilterOp registrar) +
// runDepthBufferAsGrayScaleSelfTest, all registered from the file-scope ImageFilterOp static. The
// selftest self-registers into imageFilterSelfTests() (its name is passed to the ImageFilterOp ctor),
// so --selftest-depthbufferasgrayscale resolves with ZERO shared-file edits (CMake point_ops*.cpp +
// shaders/*.metal globs pick up this leaf and its .metal automatically).
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/DetectEdges/Tint/ChannelMixer.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/depthbufferasgrayscale_params.h"  // DepthBufferAsGrayScaleParams, *_Params
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"              // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runDepthBufferAsGrayScaleSelfTest(bool injectBug);

namespace {

// DepthBufferAsGrayScale texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookDepthBufferAsGrayScale(TexCookCtx& c) {
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

  MTL::RenderPipelineState* rps = cachedTexPSO(
      c.dev, c.lib, "depthbufferasgrayscale_vs", "depthbufferasgrayscale_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  // TiXL .t3 SamplerState = MinMagMipPoint (point, no interpolation). Clamp matches GetTextureSize
  // bounds (named fork: TiXL .t3 default addressing not surfaced; clamp keeps interior loads exact).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL DepthBufferAsGrayScale defaults from DepthBufferAsGrayScale.t3:
  // NearFarRange (0.01, 1000.0), OutputRange (0.0, 5.0), ClampOutput false, Mode 0.
  DepthBufferAsGrayScaleParams p{};
  p.Near        = cookParam(c, "NearFarRange.x", 0.01f);
  p.Far         = cookParam(c, "NearFarRange.y", 1000.0f);
  p.OutrangeMin = cookParam(c, "OutputRange.x", 0.0f);
  p.OutrangeMax = cookParam(c, "OutputRange.y", 5.0f);
  p.ClampRange  = cookParam(c, "ClampOutput", 0.0f);  // bool, default false
  p.Mode        = cookParam(c, "Mode", 0.0f);         // int (0=standard, 1=legacy DoF)

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
  enc->setFragmentBytes(&p, sizeof(DepthBufferAsGrayScaleParams), DEPTHBUFFERASGRAYSCALE_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() +
// imageFilterSelfTests() pre-main. No shared file edited.
static const ImageFilterOp _reg_depthbufferasgrayscale{
    // DepthBufferAsGrayScale (TiXL Lib.image.use.DepthBufferAsGrayScale): reverse-projects the .r
    // channel of a (depth-buffer) Texture2D into a linear distance, remaps through OutputRange,
    // optionally saturates. Single Texture2D in -> Texture2D out (point_ops_depthbufferasgrayscale).
    // Kernel: depth-to-linear.hlsl — c = -Far*Near/(depth*(Far-Near)-Far) [Mode<0.5] or
    // 2*Near/(Far+Near-depth*(Far-Near)) [legacy]; OutputRange remap; ClampOutput saturate.
    // Params mirror DepthBufferAsGrayScale.cs/.t3: Image / NearFarRange(Vec2) / OutputRange(Vec2) /
    // ClampOutput(bool) / Mode(int 0=standard,1=legacy). FORKS (named): DX11 compute (RWTexture2D
    // <float>) -> Metal fullscreen-triangle VS+FS; single-channel R output -> gray replicated to RGB;
    // texture dims read in-shader (no Resolution cbuffer port); point+clamp sampler (TiXL .t3
    // MinMagMipPoint).
    {"DepthBufferAsGrayScale", "DepthBufferAsGrayScale",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // NearFarRange (Vec2, TiXL t3 default (0.01, 1000.0)): camera near/far clip planes.
      {"NearFarRange.x", "NearFarRange", "Float", true, 0.01f, 0.0f, 100000.0f, Widget::Vec, {}, true, 2},
      {"NearFarRange.y", "NearFarRange.y", "Float", true, 1000.0f, 0.0f, 100000.0f, Widget::Vec, {}, true, 1},
      // OutputRange (Vec2, TiXL t3 default (0.0, 5.0)): remap floor/ceiling (skipped if both 0).
      {"OutputRange.x", "OutputRange", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, true, 2},
      {"OutputRange.y", "OutputRange.y", "Float", true, 5.0f, -100000.0f, 100000.0f, Widget::Vec, {}, true, 1},
      // ClampOutput (bool, TiXL t3 default false): saturate output to [0,1].
      {"ClampOutput", "ClampOutput", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      // Mode (int enum, TiXL t3 default 0): 0=standard reverse-projection, 1=legacy DoF.
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Standard", "LegacyDoF"}, true}},
     nullptr},
    "DepthBufferAsGrayScale", cookDepthBufferAsGrayScale, "depthbufferasgrayscale",
    runDepthBufferAsGrayScaleSelfTest};

// --- DepthBufferAsGrayScale MATH golden -----------------------------------------------------------
// Source: solid texture, R=0 (depth=0.0). Near=1, Far=2, Mode=0 -> the standard branch yields a
// closed-form distance independent of pixel position (uniform output), so any deep-interior pixel
// carries the derived value. We exercise BOTH the saturate path and the OutputRange-remap path:
//
//   c = (-Far*Near) / (depth*(Far-Near) - Far)
//     = (-2*1) / (0*(2-1) - 2) = -2 / -2 = 1.0
//
//   Case A (OutputRange (0,0) -> remap skipped, ClampOutput on):
//       g = saturate(1.0) = 1.0      -> byte 255 (WHITE)
//   Case B (OutputRange (0,2) -> remap active, ClampOutput on):
//       c = (1.0 - 0) / (2 - 0) = 0.5
//       g = saturate(0.5) = 0.5      -> byte 128 (MID GRAY, RGBA8Unorm round(0.5*255)=128)
//
// injectBug sets Near=2 (so Near==Far==2): the standard branch denominator becomes
// depth*(Far-Near)-Far = 0*0 - 2 = -2, and the numerator -Far*Near = -4, so c = -4/-2 = 2.0.
//   Case A (remap skipped): saturate(2.0) = 1.0 -> 255  (still WHITE -> Case-A assertion survives)
//   Case B (remap (0,2)):    c = (2.0-0)/(2-0) = 1.0 -> saturate -> 255, NOT 128.
// The mid-gray Case-B assertion (~128) then FAILS (teeth) while Case A stays green, proving the
// golden actually keys on the reverse-projection + OutputRange math, not just on "any output".
int runDepthBufferAsGrayScaleSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-depthbufferasgrayscale] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Solid source: R=0 -> depth=0.0 everywhere.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (size_t i = 0; i < in.size(); i += 4) in[i + 3] = 255;  // opaque
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  auto cookCase = [&](float outMin, float outMax) -> int {
    std::map<std::string, float> params;
    params["NearFarRange.x"] = injectBug ? 2.0f : 1.0f;      // Near (bug: Near==Far -> c=2.0)
    params["NearFarRange.y"] = 2.0f;                         // Far
    params["OutputRange.x"]  = outMin;
    params["OutputRange.y"]  = outMax;
    params["ClampOutput"]    = 1.0f;                         // saturate
    params["Mode"]           = 0.0f;                         // standard branch

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
    cookDepthBufferAsGrayScale(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    // Deep-interior pixel (centre).
    size_t pi = (((size_t)H / 2) * W + W / 2) * 4;
    return (int)out[pi];  // gray = R (== G == B)
  };

  // Case A: OutputRange (0,0) -> remap skipped, c=1.0 saturates -> 255.
  int gA = cookCase(0.0f, 0.0f);
  // Case B: OutputRange (0,2) -> c=1.0 remapped to 0.5 -> 128.
  int gB = cookCase(0.0f, 2.0f);

  bool caseAWhite   = std::abs(gA - 255) <= 2;
  bool caseBMidGray = std::abs(gB - 128) <= 2;  // injectBug Near==Far -> Case B -> 255 -> FAIL
  bool pass = caseAWhite && caseBMidGray;
  printf("[selftest-depthbufferasgrayscale] caseA(gray=%d expect~255) caseB(gray=%d expect~128) -> "
         "caseAWhite=%d caseBMidGray=%d -> %s\n",
         gA, gB, caseAWhite ? 1 : 0, caseBMidGray ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
