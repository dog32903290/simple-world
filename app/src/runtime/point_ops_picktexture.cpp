// PickTexture MultiInput texture-selector op (lane multi-tex-input, image/use) — the FIRST genuinely
// variable-N MultiInputSlot<Texture2D> consumer, the proving op for the variable-N texture gather seam.
// TiXL authority:
//   external/tixl Operators/Lib/image/use/PickTexture.cs — a pure C# op (NO shader):
//       var connections = Input.GetCollectedTypedInputs();      // the N wired textures
//       if (connections == null || connections.Count == 0) return;
//       var index = Index.GetValue(context).Mod(connections.Count);
//       Selected.Value = connections[index].GetValue(context);  // forward the (Index mod N)-th input
//   external/tixl .../PickTexture.t3 — no children, no params beyond Index + the MultiInput `Input`.
//
// SEAM PROVEN: PickTexture's `Input` is a MultiInputSlot<Texture2D> (variable-N). cookTexNode (flat
// point_graph_tex_cook.cpp + resident point_graph_resident_tex_cook.cpp) now expands a multiInput
// Texture2D port over ALL its wires into CONSECUTIVE TexCookCtx::inputTextures[] slots (mirroring the
// FloatList/Gradient MultiInput gather), capped at kMaxTexInputs=4. This leaf reads inputTextures[0..N)
// and forwards the (Index mod N)-th — so the golden's result depends on N AND on the specific selected
// input being gathered, which is the load-bearing tooth (a gather that dropped inputs 2/3 selects the
// wrong texture → RED).
//
// FORK (named): TiXL PickTexture is a pure reference passthrough (Selected = the chosen Texture2D, no
// resample). The engine's tex-cook always allocates a fresh ensureTex OUTPUT for an op to fill, so we
// realize the passthrough as a 1:1 fullscreen sample-copy of the selected input into c.output. With
// output size == input size (the golden) this is bit-exact; nearest + ClampToEdge keeps it faithful.
//
// FORK (named): TiXL PickTexture has NO Resolution input; the engine sizes image-filter output off a
// Resolution pin, so a Resolution enum (default WindowFollow) is added — same fork class as every image
// filter that carries the engine's Resolution port. Mod semantics: TiXL `int.Mod(count)` is a true
// (non-negative) modulo; matched with a floored-positive modulo here.
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

// Clear `out` to black (no wired inputs -> nothing selected; mirrors TiXL's early-return on Count==0).
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

// injectBug hook (golden only): when set, the LAST gathered input is dropped from the count (as if its
// wire were missing). With Index pointing at that last input, `Index mod (N-1)` now selects a DIFFERENT
// (earlier) texture -> the pinned color misses -> RED. This directly exercises the variable-N gather:
// the result diverges iff the final MultiInput wire was really threaded into inputTextures[].
bool g_pickDropLastInput = false;

// PickTexture: forward the (Index mod N)-th of the N gathered MultiInput textures into c.output via a
// 1:1 fullscreen copy. N = inputTextureCount (the variable-N gather result). Index from the host param.
void cookPickTexture(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  int n = c.inputTextureCount;
  if (g_pickDropLastInput && n > 0) --n;  // golden injectBug: pretend the last wire is gone
  if (n <= 0) { clearTexture(c.queue, c.output); return; }  // no inputs -> nothing (TiXL: early return)

  // TiXL: index = Index.Mod(connections.Count). int.Mod is a non-negative (floored) modulo.
  int rawIndex = (int)std::lround(cookParam(c, "Index", 0.0f));
  int idx = ((rawIndex % n) + n) % n;
  const MTL::Texture* selected = c.inputTextures[idx];
  if (!selected) { clearTexture(c.queue, c.output); return; }  // gathered slot null -> nothing to copy

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "picktexture_vs", "picktexture_fs", fmt);
  if (!rps) return;

  // Nearest + ClampToEdge: a faithful 1:1 copy (output size == input size in production passthrough) —
  // no smoothing, no wrap. (TiXL passes the texture by reference; this is the engine-required copy.)
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

int runPickTextureSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from PickTexture.cs: Input = MultiInputSlot<Texture2D> (the variable-N
// gather), Index = int. Plus the engine's Resolution port (named fork — TiXL PickTexture has none).
static const ImageFilterOp _reg_picktexture{
    {"PickTexture", "PickTexture",
     {{"Input", "Input", "Texture2D", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Texture2D", false},
      {"Index", "Index", "Float", true, 0.0f, 0.0f, 1000.0f, Widget::Slider},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "PickTexture", cookPickTexture, "picktexture", runPickTextureSelfTest};

// --- PickTexture FLAT GOLDEN (closed-form, d=0 saturated solid; drives the LEAF directly) -----------
// THREE flat solids with DISTINCT colors bound as inputTextures[0..2]; inputTextureCount=3. Index=2 ->
// the op selects inputTextures[2] -> the output is that exact solid. The selected (3rd) input's color is
// the load-bearing tooth: if the variable-N gather dropped the 3rd input (count<3), Index mod count
// would select a DIFFERENT solid -> the pin misses. injectBug (g_pickDropLastInput) drops the last input
// -> Index(2) mod 2 = 0 -> selects input[0] instead -> RED.
constexpr uint32_t kGW = 32, kGH = 32;
// Distinct solid colors (each non-degenerate; the 3rd is the one Index=2 selects).
constexpr uint8_t k0r = 200, k0g = 20,  k0b = 20;   // input[0]
constexpr uint8_t k1r = 20,  k1g = 200, k1b = 20;   // input[1]
constexpr uint8_t k2r = 30,  k2g = 60,  k2b = 240;  // input[2]  <- selected by Index=2
constexpr uint8_t kExpR = k2r, kExpG = k2g, kExpB = k2b, kExpA = 255;

static void fillSolid(MTL::Texture* t, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

static bool pickCookCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool dropLast,
                           uint8_t out[4]) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* i0 = dev->newTexture(td);
  MTL::Texture* i1 = dev->newTexture(td);
  MTL::Texture* i2 = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  fillSolid(i0, kGW, kGH, k0r, k0g, k0b);
  fillSolid(i1, kGW, kGH, k1r, k1g, k1b);
  fillSolid(i2, kGW, kGH, k2r, k2g, k2b);

  std::map<std::string, float> params;
  params["Index"] = 2.0f;  // select the THIRD gathered input
  g_pickDropLastInput = dropLast;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = i0; c.inputTextures[1] = i1; c.inputTextures[2] = i2;
  c.inputTextureCount = 3;
  c.inputTexture = i0;
  cookPickTexture(c);
  g_pickDropLastInput = false;

  std::vector<uint8_t> px((size_t)kGW * kGH * 4, 0);
  dst->getBytes(px.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  const uint32_t cx = kGW / 2, cy = kGH / 2;
  size_t i = ((size_t)cy * kGW + cx) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];

  i0->release(); i1->release(); i2->release(); dst->release();
  return true;
}

int runPickTextureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-picktexture] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  pickCookCenter(dev, q, lib, /*dropLast=*/injectBug, got);

  // Non-degenerate: the SELECTED (3rd) solid differs from input[0] (what injectBug falls back to), so a
  // dropped 3rd input is genuinely distinguishable.
  bool nonDegenerate = (kExpR != k0r) || (kExpG != k0g) || (kExpB != k0b);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = nonDegenerate && match;
  printf("[selftest-picktexture] want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) "
         "d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d injectBug=%d -> %s\n",
         kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2], got[3], dR, dG, dB, dA,
         kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
