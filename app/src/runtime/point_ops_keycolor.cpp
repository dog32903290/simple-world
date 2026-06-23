// KeyColor image-filter texture op (Lane B Phase-C fan-out) — HSB-distance colour keyer.
// TiXL authority: external/tixl Operators/Lib/image/color/KeyColor.cs (ports) + KeyColor.t3
// (defaults + routing) + Assets/shaders/img/fx/ChromaKey.hlsl (the SHARED kernel — KeyColor and
// ChromaKey are two ops over the SAME shader). KeyColor is the colour-domain sibling of ChromaKey:
// identical per-pixel maths (center + 4 (±Choke) neighbours → rgb2hsb → weighted HSB distance to
// the Key colour → min (choke) → composite per Return mode), exposed in image/color with KeyColor's
// own default tuning and enum naming.
//
// BACKWARD-TRACE (Cut58 discipline, KeyColor.t3 Connections, comments stripped):
//   setup child 67e730f9 (_ImageFxShaderSetupStatic), source "Lib:shaders/img/fx/ChromaKey.hlsl".
//   Its ParamConstants float buffer (slot 4ef6f204) is filled in connection order:
//     Key.xyzw  (Vector4Components 3b090021 of the Key input)   -> cbuffer float4 KeyColor
//     Background.xyzw (Vector4Components c0354bce of Background) -> cbuffer float4 Background
//     Exposure (6a4efded) / WeightHue (52942004) / WeightSaturation (d044302c) /
//     WeightBrightness (85826060) / Amplify (91e604d7)          -> the five floats
//     Mode = IntToFloat(Return) (node 99fd4d48, Return enum -> float) -> cbuffer float Mode
//     ChokeRadius = Choke (749654ec, DIRECT, no math)           -> cbuffer float ChokeRadius
//   This is an EXACT 1:1 map onto ChromaKey.hlsl b0 (KeyColor/Background/Exposure/WeightHue/
//   WeightSaturation/WeightBrightness/Amplify/Mode/ChokeRadius) — only the int->float on Return is
//   a routing node, and it is a pure cast (Return 0..3 -> Mode 0..3), so the 1:1 holds.
//
// So KeyColor REUSES chromakey_params.h (ChromaKeyParams / CHROMAKEY_Params) and chromakey.metal
// (chromakey_vs / chromakey_fs) verbatim — no new shader, no shared-file edit. Distinct cook type
// "KeyColor" with KeyColor.t3 defaults: WeightHue 10 / WeightSaturation 5 / WeightBrightness 10 /
// Exposure 1 / Amplify 0 / Return(Mode) 0 / Choke 0 / Key (1,1,1,1) / Background (0,0,0,0).
//
// FORKS (named, inherited from the shared ChromaKey port): b1 TimeConstants unused (not bound);
// Image dims read in-shader (GetDimensions) -> no Resolution cbuffer; fixed linear+clamp sampler.
//
// Self-contained leaf: cookKeyColor + ImageFilterOp registrar + runKeyColorSelfTest.
// Shares the PSO+scratch cache seam (tex_op_cache.h) with Blur/ChromaKey/Displace/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/chromakey_params.h"  // ChromaKeyParams, CHROMAKEY_Params (SHARED with ChromaKey)
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"       // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"      // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward decl: the golden runs the shared ChromaKey kernel via KeyColor's cook (defined below).
int runKeyColorSelfTest(bool injectBug);

namespace {

// KeyColor texture op: single pass over the SHARED ChromaKey kernel. Reads c.inputTexture, writes
// c.output. No upstream texture wired: clear output to black (matches ChromaKey / image-filter rule).
void cookKeyColor(TexCookCtx& c) {
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

  // SHARED kernel entry points — reuse the ChromaKey PSO (same .metal functions).
  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "chromakey_vs", "chromakey_fs", fmt);
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see chromakey.metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL KeyColor.t3 defaults (verbatim). Key = white (1,1,1,1); Background transparent black;
  // WeightHue 10 / WeightSaturation 5 / WeightBrightness 10; Exposure 1; Amplify 0; Mode 0; Choke 0.
  // (Return enum maps 1:1 to Mode via the .t3 IntToFloat node — pure cast; see header comment.)
  ChromaKeyParams p{};
  p.KeyR = cookParam(c, "KeyColor.r", 1.0f);
  p.KeyG = cookParam(c, "KeyColor.g", 1.0f);
  p.KeyB = cookParam(c, "KeyColor.b", 1.0f);
  p.KeyA = cookParam(c, "KeyColor.a", 1.0f);
  p.BgR = cookParam(c, "Background.r", 0.0f);
  p.BgG = cookParam(c, "Background.g", 0.0f);
  p.BgB = cookParam(c, "Background.b", 0.0f);
  p.BgA = cookParam(c, "Background.a", 0.0f);  // KeyColor.t3 Background default alpha = 0
  p.Exposure         = cookParam(c, "Exposure", 1.0f);
  p.WeightHue        = cookParam(c, "WeightHue", 10.0f);        // KeyColor.t3 default 10
  p.WeightSaturation = cookParam(c, "WeightSaturation", 5.0f);  // KeyColor.t3 default 5
  p.WeightBrightness = cookParam(c, "WeightBrightness", 10.0f); // KeyColor.t3 default 10
  p.Amplify          = cookParam(c, "Amplify", 0.0f);
  p.Mode             = cookParam(c, "Return", 0.0f);  // TiXL "Return" enum -> shader Mode
  p.ChokeRadius      = cookParam(c, "Choke", 0.0f);   // TiXL "Choke" -> shader ChokeRadius

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

// Self-registration. Ports mirror KeyColor.cs (Texture2d/Key/Exposure/WeightHue/WeightSaturation/
// WeightBrightness/Amplify/Choke/Return/Background) with KeyColor.t3 defaults. Cook + cbuffer maps
// onto the SHARED ChromaKey.hlsl b0 (backward-traced 1:1; Return->Mode, Choke->ChokeRadius).
static const ImageFilterOp _reg_keycolor{
    {"KeyColor", "KeyColor",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Key (Vec4): the colour to key against; KeyColor.t3 default = white (1,1,1,1).
      {"KeyColor.r", "Key", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"KeyColor.g", "KeyColor.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"KeyColor.b", "KeyColor.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"KeyColor.a", "KeyColor.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Exposure (float, distance gain before Amplify); KeyColor.t3 default 1.
      {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 10.0f},
      // HSB channel weights; KeyColor.t3 defaults WeightHue 10 / WeightSaturation 5 / WeightBrightness 10.
      {"WeightHue", "WeightHue", "Float", true, 10.0f, 0.0f, 20.0f},
      {"WeightSaturation", "WeightSaturation", "Float", true, 5.0f, 0.0f, 20.0f},
      {"WeightBrightness", "WeightBrightness", "Float", true, 10.0f, 0.0f, 20.0f},
      // Amplify (float, subtracted from distance to tighten the key); default 0.
      {"Amplify", "Amplify", "Float", true, 0.0f, 0.0f, 4.0f},
      // Choke (float, neighbour offset px for the choke min); KeyColor.t3 default 0.
      {"Choke", "Choke", "Float", true, 0.0f, 0.0f, 10.0f},
      // Return (enum -> shader Mode): RemoveKeyed / FillKeyed / KeyedWhiteOnBackground / ReturnKeyed.
      // Maps onto ChromaKey.hlsl Mode branches (Mode<0.5 alpha / <1.5 composite / <2.5 mask / dual).
      {"Return", "Return", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
       {"RemoveKeyed", "FillKeyed", "KeyedWhiteOnBackground", "ReturnKeyed"}, true},
      // Background (Vec4): composite/replacement colour; KeyColor.t3 default transparent black.
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "KeyColor", cookKeyColor, "keycolor", runKeyColorSelfTest};

// --- KeyColor MATH golden ---------------------------------------------------------------------
// KeyColor over the SHARED ChromaKey kernel; default Return=0 (RemoveKeyed) returns
//   float4(c.rgb, saturate(distance * c.a))   (ChromaKey.hlsl Mode<0.5 branch).
// The keying signal is the HSB-distance SEPARATION between the keyed colour and a far colour.
//
// Closed-form derivation (ChromaKey.hlsl GetColorDistance with KeyColor.t3 weights):
//   rgb2hsb(green=(0,1,0)) -> hue 1/3, sat 1, bright 1/2   == the Key, so distance ≈ 0.
//   rgb2hsb(red=(1,0,0))   -> hue 0,   sat 1, bright 1/2.
//   HueDistance2(0, 1/3) = min(1/3, 2/3) = 1/3. sat/bright identical -> their deltas 0.
//   weights = (smoothstep(0,1, sat*10)*WeightHue, WeightSaturation, WeightBrightness)
//           = (1*10, 5, 10)  (sat=1 so smoothstep saturates to 1).
//   length((1/3, 0, 0) * (10,5,10)) = 10/3 ≈ 3.333. *Exposure(1) - Amplify(0) = 3.333.
//   distance = saturate(3.333) = 1  -> red kept with FULL alpha (saturate(1 * 1) = 1 = 255).
//   For the GREEN region the hue delta is 0 -> distance ≈ 0 -> alpha ≈ 0 (keyed out).
// So with KeyColor's heavier weights the separation is MUCH stronger than ChromaKey's unit-weight
// case: green alpha ~0, red alpha ~255 (full). Choke default = 0 -> the 4 neighbour samples coincide
// with center (no cross-border bleed), so we can probe near the seam too; we still probe region
// centres for safety.
// Assert: green-region alpha LOW (<40) AND red-region alpha HIGH (>200).
//   injectBug Amplify=10: distance = saturate(3.333 - 10) = 0 EVERYWHERE -> red alpha collapses to 0
//   -> the "red high" assertion FAILS (teeth) — a real key-collapse the heavier weights can't hide.
int runKeyColorSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-keycolor] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: left half green (the key), right half red (a far colour to keep). Alpha 255 throughout.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      if (x < W / 2) { in[i] = 0;   in[i + 1] = 255; in[i + 2] = 0; }    // green = key
      else           { in[i] = 255; in[i + 1] = 0;   in[i + 2] = 0; }    // red = keep
      in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["KeyColor.r"] = 0.0f; params["KeyColor.g"] = 1.0f;
  params["KeyColor.b"] = 0.0f; params["KeyColor.a"] = 1.0f;
  params["Background.r"] = 0.0f; params["Background.g"] = 0.0f;
  params["Background.b"] = 0.0f; params["Background.a"] = 0.0f;
  params["Exposure"]         = 1.0f;
  params["WeightHue"]        = 10.0f;  // KeyColor.t3 defaults (heavier than ChromaKey's unit weights)
  params["WeightSaturation"] = 5.0f;
  params["WeightBrightness"] = 10.0f;
  params["Amplify"]          = injectBug ? 10.0f : 0.0f;  // bug: distance -> 0 everywhere
  params["Return"]           = 0.0f;  // RemoveKeyed (alpha branch)
  params["Choke"]            = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookKeyColor(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  const uint32_t gy = H / 2, gx = W / 4;   // green region center
  const uint32_t rx = W * 3 / 4;           // red region center
  size_t gi = ((size_t)gy * W + gx) * 4;
  size_t ri = ((size_t)gy * W + rx) * 4;
  int greenA = out[gi + 3];
  int redA   = out[ri + 3];

  bool greenKeyed = greenA < 40;   // key colour -> distance ~0 -> alpha ~0
  bool redKept    = redA > 200;    // far colour -> distance saturates to 1 -> alpha ~255
  bool pass = greenKeyed && redKept;
  printf("[selftest-keycolor] greenA=%d redA=%d -> greenKeyed=%d redKept=%d -> %s\n",
         greenA, redA, greenKeyed ? 1 : 0, redKept ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
