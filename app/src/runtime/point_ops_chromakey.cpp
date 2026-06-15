// ChromaKey image-filter texture op (lane image_filter) — HSB-distance chroma keyer.
// TiXL authority: external/tixl Operators/Lib/Assets/shaders/img/fx/ChromaKey.hlsl (NO .cs — port
// authority = the .hlsl cbuffer b0). Per-pixel: convert center + 4 (±ChokeRadius) neighbours to
// HSB, measure weighted distance to KeyColor, take the min over the neighbourhood (choke), then
// composite per Mode.
//
// Single-pass port: cookChromaKey reads c.inputTexture, runs one fullscreen pass of
// chromakey_vs/chromakey_fs, writes c.output. Binds b0 = ChromaKeyParams. No upstream texture
// wired: clear output to black.
//
// FORK (named): b1 TimeConstants unused -> not bound; Image dims read in-shader (no Resolution
// cbuffer/port). See chromakey.metal / chromakey_params.h.
//
// Self-contained leaf: cookChromaKey + registerChromaKeyOp() + runChromaKeySelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace/DetectEdges/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/chromakey_params.h"  // ChromaKeyParams, CHROMAKEY_Params
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"       // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"      // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookChromaKey(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "chromakey_vs", "chromakey_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see .metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL ChromaKey.hlsl ParamConstants (b0). NO .t3/.cs defaults file -> defaults match the .hlsl's
  // neutral expectations (KeyColor green, unit channel weights, Exposure 1, Amplify 0, Mode 0).
  ChromaKeyParams p{};
  p.KeyR = cookParam(c, "KeyColor.r", 0.0f);
  p.KeyG = cookParam(c, "KeyColor.g", 1.0f);  // default key = green screen
  p.KeyB = cookParam(c, "KeyColor.b", 0.0f);
  p.KeyA = cookParam(c, "KeyColor.a", 1.0f);
  p.BgR = cookParam(c, "Background.r", 0.0f);
  p.BgG = cookParam(c, "Background.g", 0.0f);
  p.BgB = cookParam(c, "Background.b", 0.0f);
  p.BgA = cookParam(c, "Background.a", 1.0f);
  p.Exposure         = cookParam(c, "Exposure", 1.0f);
  p.WeightHue        = cookParam(c, "WeightHue", 1.0f);
  p.WeightSaturation = cookParam(c, "WeightSaturation", 1.0f);
  p.WeightBrightness = cookParam(c, "WeightBrightness", 1.0f);
  p.Amplify          = cookParam(c, "Amplify", 0.0f);
  p.Mode             = cookParam(c, "Mode", 0.0f);
  p.ChokeRadius      = cookParam(c, "ChokeRadius", 1.0f);

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
  enc->setFragmentBytes(&p, sizeof(ChromaKeyParams), CHROMAKEY_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. NodeSpec literal moved verbatim from node_registry_image_filter.cpp.
static const ImageFilterOp _reg_chromakey{
    // ChromaKey (TiXL Lib image/fx ChromaKey — NO .cs, ports = ChromaKey.hlsl cbuffer b0 verbatim):
    // HSB-distance chroma keyer. Single Texture2D in → Texture2D out (point_ops_chromakey.cpp).
    // Kernel: ChromaKey.hlsl — center + 4 (±ChokeRadius) neighbours → rgb2hsb → weighted distance to
    // KeyColor → min (choke) → composite per Mode. Ports mirror cbuffer b0: KeyColor(Vec4)/
    // Background(Vec4)/Exposure/WeightHue/WeightSaturation/WeightBrightness/Amplify/Mode/ChokeRadius.
    // FORKS (named): b1 TimeConstants unused (not bound); dims read in-shader; fixed clamp sampler.
    {"ChromaKey", "ChromaKey",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // KeyColor (Vec4, colour to key out; default green screen).
      {"KeyColor.r", "KeyColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"KeyColor.g", "KeyColor.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"KeyColor.b", "KeyColor.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"KeyColor.a", "KeyColor.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, composite/replacement colour).
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Exposure (float, distance gain before Amplify).
      {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 10.0f},
      // WeightHue / WeightSaturation / WeightBrightness (float, HSB channel weights).
      {"WeightHue", "WeightHue", "Float", true, 1.0f, 0.0f, 4.0f},
      {"WeightSaturation", "WeightSaturation", "Float", true, 1.0f, 0.0f, 4.0f},
      {"WeightBrightness", "WeightBrightness", "Float", true, 1.0f, 0.0f, 4.0f},
      // Amplify (float, subtracted from distance to tighten the key).
      {"Amplify", "Amplify", "Float", true, 0.0f, 0.0f, 4.0f},
      // Mode (float selector: 0 alpha / 1 composite / 2 mask / 3 dual; .hlsl thresholds).
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
       {"Alpha", "Composite", "Mask", "Dual"}, true},
      // ChokeRadius (float, neighbour offset px for the choke min).
      {"ChokeRadius", "ChokeRadius", "Float", true, 1.0f, 0.0f, 10.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ChromaKey", cookChromaKey, "chromakey", runChromaKeySelfTest};

// --- ChromaKey MATH golden --------------------------------------------------------------------
// Source: left half pure GREEN (0,255,0) = the key colour, right half pure RED (255,0,0) = a
// colour far from the key in HSB. Run ChromaKey with KeyColor=(0,1,0), Mode=0 (alpha keying),
// Exposure=1, Amplify=0, unit weights. Mode<0.5 returns (c.rgb, saturate(distance*c.a)):
//   - GREEN region: HSB ≈ KeyColor -> distance ≈ 0 -> output alpha ≈ 0 (keyed out).
//   - RED region:   HSB far from key -> distance ≈ 1 -> output alpha ≈ 1 (kept).
// HSB distance: red hue 0.0 vs green-key hue ~0.333 -> HueDistance2 ~0.333, *Exposure(1) -> the red
// region's distance (= kept alpha) is ~0.33 (≈85/255), not 1.0 — that is the faithful ChromaKey
// magnitude (sat/brightness identical, only hue differs). The key signal is the SEPARATION:
// green keys to ~0, red keeps a clearly higher alpha.
// Assert: green-region alpha LOW (<40) AND red alpha clearly ABOVE green (redA > greenA + 40).
// injectBug Amplify=10: distance = saturate(... - 10) = 0 EVERYWHERE -> red alpha collapses to 0
// -> red no longer rises above green -> the separation assertion FAILS (teeth) — a real key-collapse.
int runChromaKeySelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-chromakey] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: left half green (key), right half red (keep). Alpha = 255 throughout.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      if (x < W / 2) { in[i] = 0;   in[i + 1] = 255; in[i + 2] = 0; }    // green
      else           { in[i] = 255; in[i + 1] = 0;   in[i + 2] = 0; }    // red
      in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["KeyColor.r"] = 0.0f; params["KeyColor.g"] = 1.0f;
  params["KeyColor.b"] = 0.0f; params["KeyColor.a"] = 1.0f;
  params["Background.r"] = 0.0f; params["Background.g"] = 0.0f;
  params["Background.b"] = 0.0f; params["Background.a"] = 1.0f;
  params["Exposure"]         = 1.0f;
  params["WeightHue"]        = 1.0f;
  params["WeightSaturation"] = 1.0f;
  params["WeightBrightness"] = 1.0f;
  params["Amplify"]          = injectBug ? 10.0f : 0.0f;  // bug: distance -> 0 everywhere
  params["Mode"]             = 0.0f;  // alpha keying
  params["ChokeRadius"]      = 1.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookChromaKey(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Probe deep inside each region (away from the seam, so the 4-neighbour choke min doesn't pull
  // a region's distance toward its neighbour across the boundary).
  const uint32_t gy = H / 2, gx = W / 4;        // green region center
  const uint32_t rx = W * 3 / 4;                // red region center
  size_t gi = ((size_t)gy * W + gx) * 4;
  size_t ri = ((size_t)gy * W + rx) * 4;
  int greenA = out[gi + 3];
  int redA   = out[ri + 3];

  bool greenKeyed = greenA < 40;        // key colour -> distance ~0 -> alpha ~0
  bool redKept    = redA > greenA + 40; // far colour -> alpha clearly above key (injectBug: fails)
  bool pass = greenKeyed && redKept;
  printf("[selftest-chromakey] greenA=%d redA=%d -> greenKeyed=%d redKept=%d -> %s\n",
         greenA, redA, greenKeyed ? 1 : 0, redKept ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
