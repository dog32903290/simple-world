// FirstValidTexture MultiInput texture-selector op (lane multi-tex-input, image/use) — a SECOND
// consumer of the proven variable-N MultiInputSlot<Texture2D> gather seam (proven by PickTexture,
// commit 0fd14a4). LEAF-ONLY: no cook-core edits; the cook spine already feeds inputTextures[0..N).
//
// TiXL authority:
//   external/tixl Operators/Lib/image/use/FirstValidTexture.cs — a pure C# op (NO shader):
//       var connections = Input.GetCollectedTypedInputs();          // the N wired textures
//       if (connections != null && connections.Count > 0)
//           for (index 0..Count)  { v = connections[index].GetValue();
//                                   if (v != null) { Output.Value = v; break; } }
//       // if nothing valid: Output keeps its prior value (logs "No valid texture found").
//   external/tixl .../FirstValidTexture.t3 — no children, no params beyond the MultiInput `Input`.
//
// So FirstValidTexture forwards the FIRST NON-NULL of its N gathered MultiInput textures (in wire
// order), unchanged. It differs from PickTexture only in the SELECTION rule (first-non-null vs
// Index-mod-N): same variable-N gather, same 1:1 copy realization, same Resolution fork.
//
// SEAM (already wired, READ-ONLY): cookTexNode's MultiInput Texture2D branch
// (point_graph_tex_cook.cpp:218-231 + point_graph_resident_tex_cook.cpp same) expands this op's single
// multiInput `Input` port over ALL its wires into CONSECUTIVE inputTextures[0..N) (capped at
// kMaxTexInputs=4). A given gathered slot may itself be null (cookTexNode returned null for that wire),
// so the leaf scans for the first NON-NULL slot — matching TiXL's `if (v != null)` skip.
//
// FORK (named): TiXL FirstValidTexture is a pure reference passthrough (Output = the chosen Texture2D,
// no resample). The engine's tex-cook always allocates a fresh ensureTex OUTPUT for an op to fill, so we
// realize the passthrough as a 1:1 fullscreen sample-copy of the selected input into c.output (reusing
// PickTexture's picktexture_vs/_fs passthrough shaders). With output size == input size (the golden)
// this is bit-exact; nearest + ClampToEdge keeps it faithful. SAME fork class as PickTexture.
//
// FORK (named): TiXL FirstValidTexture has NO Resolution input; the engine sizes image-filter output off
// a Resolution pin, so a Resolution enum (default WindowFollow) is added — same fork class as every
// image filter that carries the engine's Resolution port.
//
// FORK (named, "all null" fallback): TiXL leaves Output at its PRIOR value when nothing valid is found
// (its Slot retains state across frames; first frame = default null). The engine always hands a FRESH
// output to fill (no prior-value retention), so "all inputs null / no wires" is realized as a CLEAR to
// black — identical to PickTexture's Count==0 early-return realization. (The TiXL log line is a
// debug-only side effect, not visual output, so it is not ported.)
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

// Clear `out` to black (no wired inputs / all gathered slots null -> nothing valid; mirrors TiXL's
// "no valid texture" leaving the output unset, realized as black in our fresh-output engine).
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

// injectBug hook (golden only): when set, NULL-OUT the first gathered slot before scanning, as if the
// first input's wire were dead. The first-non-null scan then selects a DIFFERENT (later) texture -> the
// pinned color misses -> RED. This exercises that the leaf truly honors per-slot validity AND that the
// 2nd+ wires reached inputTextures[] (only meaningful if the gather threaded them).
bool g_firstValidNullFirst = false;

// FirstValidTexture: forward the FIRST NON-NULL of the N gathered MultiInput textures into c.output via a
// 1:1 fullscreen copy. N = inputTextureCount (the variable-N gather result).
void cookFirstValidTexture(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  int n = c.inputTextureCount;
  if (n <= 0) { clearTexture(c.queue, c.output); return; }  // no wires -> nothing valid (TiXL: skip)

  // TiXL: scan connections in order, take the first whose GetValue() != null.
  const MTL::Texture* selected = nullptr;
  for (int i = 0; i < n; ++i) {
    if (g_firstValidNullFirst && i == 0) continue;  // golden injectBug: pretend slot 0's wire is dead
    if (c.inputTextures[i]) { selected = c.inputTextures[i]; break; }
  }
  if (!selected) { clearTexture(c.queue, c.output); return; }  // all null -> nothing valid -> black

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

int runFirstValidTextureSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from FirstValidTexture.cs: Input = MultiInputSlot<Texture2D> (the
// variable-N gather). Plus the engine's Resolution port (named fork — TiXL FirstValidTexture has none).
static const ImageFilterOp _reg_firstvalidtexture{
    {"FirstValidTexture", "FirstValidTexture",
     {{"Input", "Input", "Texture2D", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true},
      {"out", "out", "Texture2D", false},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "FirstValidTexture", cookFirstValidTexture, "firstvalidtexture", runFirstValidTextureSelfTest};

// --- FirstValidTexture FLAT GOLDEN (closed-form, d=0 saturated solid; drives the LEAF directly) -------
// THREE slots bound as inputTextures[0..2]; input[0] is NULL (a dead/unwired first slot), so the
// first-non-null scan must SKIP it and select input[1] (green). The output is input[1]'s exact solid.
// This is the load-bearing tooth: a leaf that returned input[0] (didn't honor null), or one that never
// saw input[1] (gather dropped it) / skipped too far, pins the WRONG color. injectBug ALSO nulls slot 1
// -> first-non-null falls through to input[2] (blue) -> the green pin misses -> RED.
constexpr uint32_t kGW = 32, kGH = 32;
constexpr uint8_t k1r = 20,  k1g = 200, k1b = 20;   // input[1]  <- first NON-NULL -> selected
constexpr uint8_t k2r = 30,  k2g = 60,  k2b = 240;  // input[2]  (injectBug fall-through)
constexpr uint8_t kExpR = k1r, kExpG = k1g, kExpB = k1b, kExpA = 255;

static void fillSolid(MTL::Texture* t, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

static bool fvtCookCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool injectBug,
                          uint8_t out[4]) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* i1 = dev->newTexture(td);
  MTL::Texture* i2 = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  fillSolid(i1, kGW, kGH, k1r, k1g, k1b);
  fillSolid(i2, kGW, kGH, k2r, k2g, k2b);

  std::map<std::string, float> params;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = nullptr;                   // dead first slot -> first-non-null must SKIP it
  c.inputTextures[1] = injectBug ? nullptr : i1;  // injectBug: also dead -> falls through to input[2]
  c.inputTextures[2] = i2;
  c.inputTextureCount = 3;
  c.inputTexture = nullptr;
  cookFirstValidTexture(c);

  std::vector<uint8_t> px((size_t)kGW * kGH * 4, 0);
  dst->getBytes(px.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  const uint32_t cx = kGW / 2, cy = kGH / 2;
  size_t i = ((size_t)cy * kGW + cx) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];

  i1->release(); i2->release(); dst->release();
  return true;
}

int runFirstValidTextureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-firstvalidtexture] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  fvtCookCenter(dev, q, lib, injectBug, got);

  // Non-degenerate: the SELECTED (input[1], green) solid differs from input[2] (blue, the injectBug
  // fall-through), so a leaf that skips too far / drops input[1] is genuinely distinguishable.
  bool nonDegenerate = (kExpR != k2r) || (kExpG != k2g) || (kExpB != k2b);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = nonDegenerate && match;
  printf("[selftest-firstvalidtexture] want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) "
         "d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d injectBug=%d -> %s\n",
         kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2], got[3], dR, dG, dB, dA,
         kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
