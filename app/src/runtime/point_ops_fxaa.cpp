// Fxaa image-filter texture op — NVIDIA FXAA 3.11 anti-aliasing (single fullscreen pass).
// TiXL authority: Operators/Lib/image/use/Fxaa.cs (Image/Preset/KeepAlpha inputs) +
// Fxaa.t3 (defaults: Preset=0, KeepAlpha=false) + Assets/shaders/img/use/FXAA.hlsl
// (FxaaPixelShader, single input texture t0, NO time / NO second texture / NO feedback).
//
// Single-pass port: cookFxaa reads c.inputTexture (upstream RenderTarget's Texture2D), runs one
// fullscreen pass of fxaa_vs/fxaa_fs, writes c.output. Binds two constant buffers:
//   b0 = FxaaParams      (rcpFrame, KeepAlpha, + the chosen preset's unrolled constants)
//   b1 = FxaaResolution  (TargetWidth/TargetHeight — host fills from c.output->width/height)
//
// FORK (named): TiXL bakes the FXAA preset (0..5) at COMPILE time via `#define FXAA_PRESET`.
// We have one precompiled metallib, so the HOST expands the chosen preset into the b0 cbuffer
// and the shader reads the constants at runtime. The FXAA arithmetic is unchanged. See
// fxaa_params.h / fxaa.metal headers. This is the same "host provides Resolution cbuffer" fork
// already used by chromab/blur/detectedges.
//
// Self-contained leaf: cookFxaa + registerFxaaOp() (static ImageFilterOp) + runFxaaSelfTest.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/fxaa_params.h"  // FxaaParams, FxaaResolution, FXAA_Params/Resolution
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"   // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"  // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward-decl (selftest used by the registrar below; defined at file end). NOT in point_ops.h:
// that header is at its linecount ratchet cap. The router declares it in selftests_decls.h.
int runFxaaSelfTest(bool injectBug);

namespace {

// Expand the FXAA preset int (0..5) into the b0 constant set. Mirrors FXAA.hlsl's
// `#if (FXAA_PRESET == N)` blocks VERBATIM (lines 110-180). Every preset sets FXAA_SUBPIX=1
// and FXAA_SEARCH_THRESHOLD=1/4; only preset 0 sets FXAA_SUBPIX_FASTER=1.
void applyFxaaPreset(FxaaParams& p, int preset) {
  preset = std::min(5, std::max(0, preset));
  p.searchThreshold = 1.0f / 4.0f;  // all presets
  p.subpixTrim      = 1.0f / 4.0f;  // FXAA_SUBPIX_TRIM (all presets)
  p.subpixFaster    = 0;
  switch (preset) {
    case 0:
      p.edgeThreshold = 1.0f / 4.0f;  p.edgeThresholdMin = 1.0f / 12.0f;
      p.searchSteps = 2;  p.searchAccel = 4;
      p.subpixFaster = 1; p.subpixCap = 2.0f / 3.0f;
      break;
    case 1:
      p.edgeThreshold = 1.0f / 8.0f;  p.edgeThresholdMin = 1.0f / 16.0f;
      p.searchSteps = 4;  p.searchAccel = 3;  p.subpixCap = 3.0f / 4.0f;
      break;
    case 2:
      p.edgeThreshold = 1.0f / 8.0f;  p.edgeThresholdMin = 1.0f / 24.0f;
      p.searchSteps = 8;  p.searchAccel = 2;  p.subpixCap = 3.0f / 4.0f;
      break;
    case 3:
      p.edgeThreshold = 1.0f / 8.0f;  p.edgeThresholdMin = 1.0f / 24.0f;
      p.searchSteps = 16; p.searchAccel = 1;  p.subpixCap = 3.0f / 4.0f;
      break;
    case 4:
      p.edgeThreshold = 1.0f / 8.0f;  p.edgeThresholdMin = 1.0f / 24.0f;
      p.searchSteps = 24; p.searchAccel = 1;  p.subpixCap = 3.0f / 4.0f;
      break;
    case 5:
    default:
      p.edgeThreshold = 1.0f / 8.0f;  p.edgeThresholdMin = 1.0f / 24.0f;
      p.searchSteps = 32; p.searchAccel = 1;  p.subpixCap = 3.0f / 4.0f;
      break;
  }
}

// FXAA texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to transparent black (TiXL RenderTarget ClearColor 0).
void cookFxaa(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  if (!c.inputTexture) {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(c.output);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    return;
  }

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "fxaa_vs", "fxaa_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  // HLSL anisotropicSampler. Fork (named, as chromab): fixed linear+clamp. FXAA's
  // negative/positive edge search reads neighbours via integer offsets and fractional UVs;
  // clamp matches the edge-pixel behaviour. (TiXL host wraps via its anisotropic state.)
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params: Fxaa.t3 defaults (Preset=0, KeepAlpha=false).
  FxaaParams p{};
  p.rcpFrameX = 1.0f / (float)c.output->width();
  p.rcpFrameY = 1.0f / (float)c.output->height();
  p.KeepAlpha = (cookParam(c, "KeepAlpha", 0.0f) != 0.0f) ? 1.0f : 0.0f;
  applyFxaaPreset(p, (int)std::lround(cookParam(c, "Preset", 0.0f)));

  FxaaResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(c.inputTexture), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(FxaaParams),     FXAA_Params);
  enc->setFragmentBytes(&res, sizeof(FxaaResolution), FXAA_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. Mirrors Fxaa.cs inputs exactly: Image / Preset / KeepAlpha (no Resolution
// inputs in TiXL — FXAA targets the upstream texture's own size; host fills b1 from output).
static const ImageFilterOp _reg_fxaa{
    {"Fxaa", "Fxaa",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Preset (InputSlot<int>, Fxaa.t3 default 0). 6 NVIDIA FXAA quality presets.
      {"Preset", "Preset", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum,
       {"0 (fastest)", "1", "2", "3", "4", "5 (best)"}, true},
      // KeepAlpha (InputSlot<bool>, Fxaa.t3 default false). off -> alpha=1; on -> sample source alpha.
      {"KeepAlpha", "KeepAlpha", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"No", "Yes"}, true}},
     nullptr},
    "Fxaa", cookFxaa, "fxaa", runFxaaSelfTest};

// --- Fxaa MATH golden (d=0 saturated-plateau discipline) ---------------------------------------
// FXAA's edge-AA term vanishes where there is no luma gradient. The early-exit predicate is
//   range < max(edgeThresholdMin, rangeMax * edgeThreshold)
// where range = rangeMax - rangeMin over the 3x3 luma neighbourhood. On a FLAT-CONSTANT input
// range == 0, so the branch fires at EVERY interior pixel and the shader returns rgbM = the
// center sample UNCHANGED. Therefore interior pixels of a flat image MUST equal the input
// BYTE-IDENTICAL — this is the parity pin.
//
//   GREEN (parity): flat solid (120,200,60,255) -> pinned interior pixel == input exactly, and
//                   KeepAlpha=No forces alpha=255. A second quarter-frame pixel is checked too.
//   RED  (injectBug): feed a SHARP 1px checkerboard instead of flat. Now range != 0 at the pin,
//                   the early-exit does NOT fire, FXAA runs its full blend path and the center
//                   pixel is averaged toward neighbours -> it NO LONGER equals (120,200,60).
//                   The parity assertion FAILS. This proves the early-exit/parity logic is REAL
//                   (a shader that always blurred, or always passed through, would not flip).
// The pinned pixel is a deep-interior pixel (W/2,H/2), never an fwidth/smoothstep edge.
int runFxaaSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-fxaa] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // GREEN: FLAT solid color (d=0 saturated plateau) -> FXAA early-exits everywhere -> output ==
  // input byte-identical. RED (injectBug): SHARP 1px checkerboard -> luma gradient at the pin ->
  // early-exit does NOT fire -> FXAA blends the center toward neighbours -> pin != input.
  const uint8_t kR = 120, kG = 200, kB = 60, kA = 255;
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      if (injectBug && ((x ^ y) & 1)) {
        // checkerboard "off" cells: black -> maximal luma gradient against the (120,200,60) cells.
        in[i] = 0; in[i + 1] = 0; in[i + 2] = 0; in[i + 3] = kA;
      } else {
        in[i] = kR; in[i + 1] = kG; in[i + 2] = kB; in[i + 3] = kA;
      }
    }
  const uint32_t pinX = W / 2, pinY = H / 2;  // deep interior pixel ("on" cell: (x^y)&1==0)
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Cook FXAA on the flat image, Preset=5 (heaviest), KeepAlpha=No.
  {
    std::map<std::string, float> pm;
    pm["Preset"] = 5.0f;
    pm["KeepAlpha"] = 0.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &pm;
    cookFxaa(c);
  }

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  size_t pi = ((size_t)pinY * W + pinX) * 4;
  int oR = out[pi], oG = out[pi + 1], oB = out[pi + 2], oA = out[pi + 3];

  // Parity pin: on a FLAT image, FXAA early-exits everywhere -> interior pixel == input exactly.
  // KeepAlpha=No -> alpha forced to 1.0 == 255.
  bool parity = (oR == kR) && (oG == kG) && (oB == kB) && (oA == 255);

  // Also verify a non-pinned interior pixel always equals the input (flat plateau, never touched
  // by the bug corruption which only hit the center pixel).
  const uint32_t qX = W / 4, qY = H / 4;
  size_t qi = ((size_t)qY * W + qX) * 4;
  bool quarterFlat = (out[qi] == kR) && (out[qi + 1] == kG) && (out[qi + 2] == kB);

  bool pass = parity && quarterFlat;
  printf("[selftest-fxaa] pin(%u,%u)=(%d,%d,%d,%d) expect(%d,%d,%d,255) parity=%d quarterFlat=%d -> %s\n",
         pinX, pinY, oR, oG, oB, oA, kR, kG, kB, parity ? 1 : 0, quarterFlat ? 1 : 0,
         pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
