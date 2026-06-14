// ChannelMixer image-filter texture op (lane image_filter).
// TiXL authority: external/tixl/Operators/Lib/image/color/ChannelMixer.cs (ports) +
// ChannelMixer.t3 (defaults) + Assets/shaders/img/MixChannels.hlsl (kernel).
//
// Single-pass port: cookChannelMixer reads c.inputTexture (upstream Texture2D via the
// gather direct-through), runs one fullscreen pass of channelmixer_vs/channelmixer_fs,
// writes c.output. No upstream texture: clear output to black.
//
// Vec4 inputs (MultiplyR/G/B/A/Add) are decomposed via four cookParam calls each
// (.r/.g/.b/.a components stored as separate float params, matching NodeSpec Vec ports).
//
// Self-contained leaf: cookChannelMixer + registerChannelMixerOp() + runChannelMixerSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace/Tint/AdjustColors.
//
// Forks (named):
// 1. DX11 PS pipeline -> Metal fullscreen triangle VS+FS (same as Tint/AdjustColors fork class).
// 2. GenerateMipmaps (bool input): TiXL post-generates mips after the filter. We skip mip
//    generation — the param is read but does not dispatch a mip-gen pass (follow-up item).
// 3. Fixed linear+clamp sampler — TiXL's Wrap/TextureFiltering host knobs not exposed.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/channelmixer_params.h"  // ChannelMixerParams, CHANNELMIXER_Params
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_graph.h"    // TexCookCtx, cookParam, registerTexOp
#include "runtime/resident_eval_graph.h"
#include "runtime/tex_op_cache.h"   // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// ChannelMixer texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookChannelMixer(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "channelmixer_vs", "channelmixer_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL ChannelMixer.cs defaults from ChannelMixer.t3:
  // MultiplyR default (1,0,0,0), MultiplyG (0,1,0,0), MultiplyB (0,0,1,0), MultiplyA (0,0,0,1)
  // Add default (0,0,0,0), ClampResult default true.
  ChannelMixerParams p{};
  // MultiplyR (Vec4) — default (1,0,0,0)
  p.MultiplyRr = cookParam(c, "MultiplyR.r", 1.0f);
  p.MultiplyRg = cookParam(c, "MultiplyR.g", 0.0f);
  p.MultiplyRb = cookParam(c, "MultiplyR.b", 0.0f);
  p.MultiplyRa = cookParam(c, "MultiplyR.a", 0.0f);
  // MultiplyG (Vec4) — default (0,1,0,0)
  p.MultiplyGr = cookParam(c, "MultiplyG.r", 0.0f);
  p.MultiplyGg = cookParam(c, "MultiplyG.g", 1.0f);
  p.MultiplyGb = cookParam(c, "MultiplyG.b", 0.0f);
  p.MultiplyGa = cookParam(c, "MultiplyG.a", 0.0f);
  // MultiplyB (Vec4) — default (0,0,1,0)
  p.MultiplyBr = cookParam(c, "MultiplyB.r", 0.0f);
  p.MultiplyBg = cookParam(c, "MultiplyB.g", 0.0f);
  p.MultiplyBb = cookParam(c, "MultiplyB.b", 1.0f);
  p.MultiplyBa = cookParam(c, "MultiplyB.a", 0.0f);
  // MultiplyA (Vec4) — default (0,0,0,1)
  p.MultiplyAr = cookParam(c, "MultiplyA.r", 0.0f);
  p.MultiplyAg = cookParam(c, "MultiplyA.g", 0.0f);
  p.MultiplyAb = cookParam(c, "MultiplyA.b", 0.0f);
  p.MultiplyAa = cookParam(c, "MultiplyA.a", 1.0f);
  // Add (Vec4) — default (0,0,0,0)
  p.AddR = cookParam(c, "Add.r", 0.0f);
  p.AddG = cookParam(c, "Add.g", 0.0f);
  p.AddB = cookParam(c, "Add.b", 0.0f);
  p.AddA = cookParam(c, "Add.a", 0.0f);
  // ClampResult (bool) — default true
  p.ClampResult = cookParam(c, "ClampResult", 1.0f);  // TiXL default true
  // GenerateMipmaps (bool) — read but not dispatched (fork #2)
  (void)cookParam(c, "GenerateMipmaps", 0.0f);

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
  enc->setFragmentBytes(&p, sizeof(ChannelMixerParams), CHANNELMIXER_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerChannelMixerOp() { registerTexOp("ChannelMixer", cookChannelMixer); }

// --- ChannelMixer MATH golden ---------------------------------------------------------------
// Fill a 64x64 source texture with solid red (R=200, G=50, B=50, A=255).
// Apply ChannelMixer with:
//   MultiplyR = (0,1,0,0)  -- swap: output R = input G
//   MultiplyG = (1,0,0,0)  -- swap: output G = input R
//   MultiplyB = (0,0,1,0)  -- identity: output B = input B
//   MultiplyA = (0,0,0,1)  -- identity: output A = input A
//   Add = (0,0,0,0), ClampResult = 1
// Input (200,50,50,255) -> output R=src.G=50, output G=src.R=200.
// Assert output R < 100 and output G > 100 (channels really swapped).
// injectBug: swap MultiplyR and MultiplyG back to identity -> R stays at 200, G at 50
// -> R<100 assertion FAILS (teeth).
int runChannelMixerSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-channelmixer] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source texture: solid reddish (R=200, G=50, B=50, A=255).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);
  std::vector<uint8_t> in((size_t)W * H * 4);
  for (size_t i = 0; i < (size_t)W * H; ++i) {
    in[i * 4 + 0] = 200;  // R
    in[i * 4 + 1] = 50;   // G
    in[i * 4 + 2] = 50;   // B
    in[i * 4 + 3] = 255;  // A
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  if (injectBug) {
    // Bug: identity matrix — channels NOT swapped. R stays ~200, G stays ~50.
    params["MultiplyR.r"] = 1.0f; params["MultiplyR.g"] = 0.0f;
    params["MultiplyR.b"] = 0.0f; params["MultiplyR.a"] = 0.0f;
    params["MultiplyG.r"] = 0.0f; params["MultiplyG.g"] = 1.0f;
    params["MultiplyG.b"] = 0.0f; params["MultiplyG.a"] = 0.0f;
  } else {
    // Swap R<->G: MultiplyR picks from G col, MultiplyG picks from R col.
    params["MultiplyR.r"] = 0.0f; params["MultiplyR.g"] = 1.0f;  // output R = src G
    params["MultiplyR.b"] = 0.0f; params["MultiplyR.a"] = 0.0f;
    params["MultiplyG.r"] = 1.0f; params["MultiplyG.g"] = 0.0f;  // output G = src R
    params["MultiplyG.b"] = 0.0f; params["MultiplyG.a"] = 0.0f;
  }
  // Identity for B/A channels.
  params["MultiplyB.r"] = 0.0f; params["MultiplyB.g"] = 0.0f;
  params["MultiplyB.b"] = 1.0f; params["MultiplyB.a"] = 0.0f;
  params["MultiplyA.r"] = 0.0f; params["MultiplyA.g"] = 0.0f;
  params["MultiplyA.b"] = 0.0f; params["MultiplyA.a"] = 1.0f;
  params["Add.r"] = 0.0f; params["Add.g"] = 0.0f;
  params["Add.b"] = 0.0f; params["Add.a"] = 0.0f;
  params["ClampResult"] = 1.0f;
  params["GenerateMipmaps"] = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookChannelMixer(c);

  // Readback: center pixel.
  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  const uint32_t cx = W / 2, cy = H / 2;
  size_t i = ((size_t)cy * W + cx) * 4;
  int R = out[i], G = out[i + 1];
  // After R<->G swap: output R should be src G (~50) -> <100; output G should be src R (~200) -> >100.
  bool rSwapped = (R < 100);  // injectBug (identity): R=~200 -> fails this
  bool gSwapped = (G > 100);  // injectBug (identity): G=~50  -> fails this
  bool pass = rSwapped && gSwapped;
  printf("[selftest-channelmixer] center R=%d G=%d rSwapped=%d gSwapped=%d -> %s\n",
         R, G, rSwapped ? 1 : 0, gSwapped ? 1 : 0, pass ? "PASS" : "FAIL");

  // Secondary tooth: verify Add offset. Rebuild with identity matrix + Add.r=0.5 (=128 in uint8).
  // Output R should be ~src.R(200/255) + 0.5 -> clamped to ~1.0 -> ~255. Assert R > 200.
  std::map<std::string, float> addParams;
  addParams["MultiplyR.r"] = 1.0f; addParams["MultiplyR.g"] = 0.0f;
  addParams["MultiplyR.b"] = 0.0f; addParams["MultiplyR.a"] = 0.0f;
  addParams["MultiplyG.r"] = 0.0f; addParams["MultiplyG.g"] = 1.0f;
  addParams["MultiplyG.b"] = 0.0f; addParams["MultiplyG.a"] = 0.0f;
  addParams["MultiplyB.r"] = 0.0f; addParams["MultiplyB.g"] = 0.0f;
  addParams["MultiplyB.b"] = 1.0f; addParams["MultiplyB.a"] = 0.0f;
  addParams["MultiplyA.r"] = 0.0f; addParams["MultiplyA.g"] = 0.0f;
  addParams["MultiplyA.b"] = 0.0f; addParams["MultiplyA.a"] = 1.0f;
  addParams["Add.r"] = 0.5f;  // push R up by half
  addParams["Add.g"] = 0.0f; addParams["Add.b"] = 0.0f; addParams["Add.a"] = 0.0f;
  addParams["ClampResult"] = 1.0f;
  addParams["GenerateMipmaps"] = 0.0f;

  MTL::Texture* dst2 = dev->newTexture(td);
  TexCookCtx c2;
  c2.dev = dev; c2.lib = lib; c2.queue = q;
  c2.nodeId = 2; c2.inputTexture = src; c2.output = dst2; c2.params = &addParams;
  cookChannelMixer(c2);

  std::vector<uint8_t> out2((size_t)W * H * 4, 0);
  dst2->getBytes(out2.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int R2 = out2[i];
  bool addPass = (R2 > 200);  // add pushed past 200/255; injectBug doesn't affect Add sub-test
  printf("[selftest-channelmixer] add-tooth R2=%d (need>200) -> %s\n",
         R2, addPass ? "PASS" : "FAIL");

  bool allPass = pass && addPass;

  src->release(); dst->release(); dst2->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return allPass ? 0 : 1;
}

}  // namespace sw
