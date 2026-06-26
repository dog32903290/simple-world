// UseFallbackTexture two-input texture-selector op (lane multi-tex-input, image/use). LEAF-ONLY: no
// cook-core edits; the cook spine already feeds each FIXED Texture2D port into a consecutive
// inputTextures[] slot (the else branch of cookTexNode's Texture2D scan — same as Combine3Images'
// ImageA/B/C).
//
// TiXL authority:
//   external/tixl Operators/Lib/image/use/UseFallbackTexture.cs — a pure C# op (NO shader):
//       _fallback = (cached) Fallback.GetValue(context);
//       var tex = TextureA.GetValue(context) ?? _fallback;
//       Output.Value = tex;
//   external/tixl .../UseFallbackTexture.t3 — no children; two InputSlot<Texture2D> (TextureA, Fallback).
//
// So UseFallbackTexture forwards TextureA if non-null, else Fallback. NOT a MultiInput: two FIXED
// single-cardinality texture ports — TextureA = inputTextures[0], Fallback = inputTextures[1]. An
// unwired port still occupies its slot as nullptr (the FIXED else branch), so inputTextureCount == 2.
// Null semantics are EXACTLY TiXL's `TextureA ?? Fallback` (C# null-coalesce): TextureA wins iff
// non-null; otherwise Fallback (which may itself be null -> nothing -> black).
//
// FORK (named): TiXL UseFallbackTexture is a pure reference passthrough (Output = the chosen Texture2D,
// no resample). The engine's tex-cook always allocates a fresh ensureTex OUTPUT to fill, so the
// passthrough is realized as a 1:1 fullscreen sample-copy of the selected input into c.output (reusing
// PickTexture's picktexture_vs/_fs passthrough shaders). Output size == input size (the golden) -> the
// copy is bit-exact; nearest + ClampToEdge keeps it faithful. SAME fork class as PickTexture.
//
// FORK (named): TiXL UseFallbackTexture has NO Resolution input; the engine sizes image-filter output off
// a Resolution pin, so a Resolution enum (default WindowFollow) is added — same fork class as every image
// filter that carries the engine's Resolution port.
//
// FORK (named, "both null" fallback): TiXL's `_fallback` is a stateful cache that can retain a prior
// frame's texture; first frame / both-unwired -> null -> Output null. The engine hands a FRESH output
// each cook (no prior-value retention), so "TextureA null AND Fallback null" is realized as a CLEAR to
// black — identical to PickTexture's no-input realization.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Clear `out` to black (both inputs null -> nothing to forward; mirrors TiXL's null Output, realized as
// black in our fresh-output engine).
void clearTexture(MTL::CommandQueue* q, MTL::Texture* out) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(out);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// injectBug hook (golden only): when set, NULL-OUT TextureA (inputTextures[0]) before the select, as if
// its wire were dead. The `?? Fallback` then forwards Fallback (inputTextures[1]) instead -> the pinned
// (TextureA) color misses -> RED. This exercises the EXACT null-coalesce: a leaf that ignored TextureA's
// validity (always TextureA, or always Fallback) would not flip here.
bool g_useFallbackNullTextureA = false;

// UseFallbackTexture: tex = TextureA (inputTextures[0]) ?? Fallback (inputTextures[1]); 1:1 copy into out.
void cookUseFallbackTexture(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* texA = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* fallback = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  if (g_useFallbackNullTextureA) texA = nullptr;  // golden injectBug: pretend TextureA's wire is dead

  // TiXL: var tex = TextureA.GetValue(context) ?? _fallback;
  const MTL::Texture* selected = texA ? texA : fallback;
  if (!selected) { clearTexture(c.queue, c.output); return; }  // both null -> nothing -> black

  // Reuse PickTexture's generic passthrough copy shaders (picktexture_vs/_fs are pure samplers).
  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "picktexture_vs", "picktexture_fs", fmt);
  if (!rps) return;

  // Nearest + ClampToEdge: a faithful 1:1 copy (output size == input size in production passthrough).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(selected), 0);  // texture(0) = the selected input
  enc->setFragmentSamplerState(samp, 0);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

int runUseFallbackTextureSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from UseFallbackTexture.cs: TWO FIXED single-cardinality Texture2D ports
// — TextureA (slot 0), Fallback (slot 1), multiInput==false (the default). Plus the engine's Resolution
// port (named fork — TiXL UseFallbackTexture has none). Port DECLARATION order = slot order: TextureA
// must be first so it lands in inputTextures[0], Fallback in inputTextures[1].
static const ImageFilterOp _reg_usefallbacktexture{
    {"UseFallbackTexture", "UseFallbackTexture",
     {{"TextureA", "TextureA", "Texture2D", true},
      {"Fallback", "Fallback", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "UseFallbackTexture", cookUseFallbackTexture, "usefallbacktexture", runUseFallbackTextureSelfTest};

// --- UseFallbackTexture FLAT GOLDEN (closed-form, d=0 saturated solid; drives the LEAF directly) ------
// TextureA (green) + Fallback (blue) both bound. With TextureA non-null, `TextureA ?? Fallback` selects
// TextureA -> output is green. The load-bearing tooth: TextureA winning over a present Fallback proves
// the null-coalesce order AND that Fallback (slot 1) didn't overwrite the choice. injectBug nulls
// TextureA -> the select falls through to Fallback (blue) -> the green pin misses -> RED.
constexpr uint32_t kGW = 32, kGH = 32;
constexpr uint8_t kAr = 20, kAg = 200, kAb = 20;   // TextureA (green) <- selected when non-null
constexpr uint8_t kFr = 30, kFg = 60,  kFb = 240;  // Fallback (blue)  <- injectBug fall-through
constexpr uint8_t kExpR = kAr, kExpG = kAg, kExpB = kAb, kExpA = 255;

static void fillSolid(MTL::Texture* t, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

static bool uftCookCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool injectBug,
                          uint8_t out[4]) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* texA = dev->newTexture(td);
  MTL::Texture* fb = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  fillSolid(texA, kGW, kGH, kAr, kAg, kAb);
  fillSolid(fb, kGW, kGH, kFr, kFg, kFb);

  std::map<std::string, float> params;
  g_useFallbackNullTextureA = injectBug;  // injectBug: drop TextureA -> select Fallback

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = texA;  // TextureA -> slot 0
  c.inputTextures[1] = fb;    // Fallback -> slot 1
  c.inputTextureCount = 2;
  c.inputTexture = texA;
  cookUseFallbackTexture(c);
  g_useFallbackNullTextureA = false;

  std::vector<uint8_t> px((size_t)kGW * kGH * 4, 0);
  dst->getBytes(px.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  const uint32_t cx = kGW / 2, cy = kGH / 2;
  size_t i = ((size_t)cy * kGW + cx) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];

  texA->release(); fb->release(); dst->release();
  return true;
}

int runUseFallbackTextureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-usefallbacktexture] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  uftCookCenter(dev, q, lib, injectBug, got);

  // Non-degenerate: TextureA (green) differs from Fallback (blue, the injectBug fall-through), so a leaf
  // that ignored the null-coalesce order is genuinely distinguishable.
  bool nonDegenerate = (kExpR != kFr) || (kExpG != kFg) || (kExpB != kFb);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = nonDegenerate && match;
  printf("[selftest-usefallbacktexture] want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) "
         "d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d injectBug=%d -> %s\n",
         kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2], got[3], dR, dG, dB, dA,
         kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
