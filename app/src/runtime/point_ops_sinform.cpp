// SinForm image generator / pattern op (Phase C, C-3).
// TiXL authority: Operators/Lib/image/generate/pattern/SinForm.cs (ports + defaults) +
// SinForm.t3 (defaults: Fill=(1,1,1,1), Background=(0,0,0,0), LineWidth=0.04333334, Fade=1.0,
// Size=(1,1), Offset=(0,0), Rotate=0.0, Copies=0.0, OffsetCopies=(0,0.05), Image=null) +
// Assets/shaders/img/fx/SinForm.hlsl (psMain: aspect-corrected rotation, sin-wave loop with
// OffsetCopies stacking, smoothstep feather → alpha composite over optional input orgColor).
//
// SinForm is a GENERATOR: the Image input is optional (TiXL default null). When no upstream
// texture is wired (c.inputTexture == nullptr), the host binds a 1x1 transparent-black dummy
// so sinform_fs always receives a valid texture2d handle and computes the wave over black
// (TiXL behaviour with no input → wave on black, orgColor=(0,0,0,0)).
// When Image IS wired, the wave is alpha-composited over the upstream colour (orgColor != 0).
//
// Two cbuffers:
//   b0 SinFormParams (SINFORM_Params): Fill/Background/Size/Offset/OffsetCopies/Rotate/LineWidth/
//      Fade/Copies — host-filled from cookParam.
//   b1 SinFormResolution (SINFORM_Resolution): TargetWidth/TargetHeight — host-filled from output.
//
// Self-contained leaf: cookSinForm + _reg_sinform + runSinFormSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h).
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, registerTexOp
#include "runtime/sinform_params.h"            // SinFormParams/Resolution, SINFORM_* bindings
#include "runtime/tex_op_cache.h"              // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runSinFormSelfTest.
int runSinFormSelfTest(bool injectBug);

namespace {

// Helper: create a 1x1 transparent-black dummy texture for the no-input case.
// SinForm is a generator (TiXL Image=null default). We must still run the shader;
// a 1x1 dummy gives sinform_fs a valid texture2d handle → orgColor=(0,0,0,0) = transparent.
static MTL::Texture* makeDummyBlackTexture(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  // Fill with (0,0,0,0) — default zeroed buffer from newTexture satisfies this, but explicit:
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// SinForm texture op: single render pass. Optionally reads c.inputTexture (if wired), always
// writes c.output. If no upstream: the wave is drawn over black (transparent-black dummy).
void cookSinForm(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "sinform_vs", "sinform_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: TiXL .t3 Wrap=Clamp
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL SinForm.t3 defaults: Fill=(1,1,1,1), Background=(0,0,0,0), Size=(1,1), Offset=(0,0),
  // OffsetCopies=(0,0.05), Rotate=0.0, LineWidth=0.04333334, Fade=1.0, Copies=0.0.
  SinFormParams p{};
  // Fill (Vec4, default (1,1,1,1))
  p.FillR = cookParam(c, "Fill.r", 1.0f);
  p.FillG = cookParam(c, "Fill.g", 1.0f);
  p.FillB = cookParam(c, "Fill.b", 1.0f);
  p.FillA = cookParam(c, "Fill.a", 1.0f);
  // Background (Vec4, default (0,0,0,0))
  p.BgR = cookParam(c, "Background.r", 0.0f);
  p.BgG = cookParam(c, "Background.g", 0.0f);
  p.BgB = cookParam(c, "Background.b", 0.0f);
  p.BgA = cookParam(c, "Background.a", 0.0f);
  // Size (Vec2, default (1,1))
  p.SizeX = cookParam(c, "Size.x", 1.0f);
  p.SizeY = cookParam(c, "Size.y", 1.0f);
  // Offset (Vec2, default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // OffsetCopies (Vec2, default (0, 0.05))
  p.OffCopX = cookParam(c, "OffsetCopies.x", 0.0f);
  p.OffCopY = cookParam(c, "OffsetCopies.y", 0.05f);
  // Scalars
  p.Rotate    = cookParam(c, "Rotate",    0.0f);
  p.LineWidth = cookParam(c, "LineWidth", 0.04333334f);
  p.Fade      = cookParam(c, "Fade",      1.0f);
  p.Copies    = cookParam(c, "Copies",    0.0f);

  SinFormResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind input texture (or 1x1 black dummy when no upstream is wired).
  // FORK (named): TiXL .t3 Image=null default → shader sees orgColor=(0,0,0,0) → wave on black.
  // sw host provides the dummy so sinform_fs always has a valid texture2d binding.
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* inputTex = c.inputTexture;
  if (!inputTex) {
    dummyTex = makeDummyBlackTexture(c.dev);
    inputTex = dummyTex;
  }

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));  // transparent clear
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(inputTex), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(SinFormParams),     SINFORM_Params);
  enc->setFragmentBytes(&res, sizeof(SinFormResolution), SINFORM_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. NodeSpec mirrors SinForm.cs inputs.
// Vec4 inputs (Fill, Background) decomposed to .r/.g/.b/.a scalar ports.
// Vec2 inputs (Size, Offset, OffsetCopies) decomposed to .x/.y scalar ports.
// Resolution: standard Output Size enum + CustomW/H (same as VoronoiCells/Tint).
// FORKS (named):
//   1. TiXL Image input optional (default null): sw accepts null input and renders wave over black.
//   2. TiXL TextureFormat (DXGI Format enum) → not modelled (sw uses RGBA8Unorm via Resolution enum).
//   3. Fixed clamp sampler (TiXL .t3 Wrap=Clamp verbatim).
static const ImageFilterOp _reg_sinform{
    // SinForm (TiXL Lib.image.generate.pattern.SinForm): sinusoidal wave pattern generator.
    // Optional Texture2D in (Image, default null) → Texture2D out. When no Image: wave on black.
    // Kernel: SinForm.hlsl — aspect-corrected rotation, Copies-loop sin-wave, smoothstep
    // feather (LineWidth/Fade), alpha composite over input (or black). Params mirror SinForm.cs/.t3.
    {"SinForm", "SinForm",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Fill (Vec4, TiXL t3 default (1,1,1,1) — white wave)
      {"Fill.r", "Fill", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Fill.g", "Fill.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.b", "Fill.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.a", "Fill.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL t3 default (0,0,0,0) — transparent)
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // LineWidth (float, TiXL t3 default 0.04333334)
      {"LineWidth", "LineWidth", "Float", true, 0.04333334f, 0.0f, 0.5f, Widget::Slider},
      // Fade (float, TiXL t3 default 1.0; controls feather = LineWidth*Fade/2)
      {"Fade", "Fade", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Slider},
      // Size (Vec2, TiXL t3 default (1,1); x=period, y=amplitude)
      {"Size.x", "Size", "Float", true, 1.0f, 0.01f, 10.0f, Widget::Vec, {}, true, 2},
      {"Size.y", "Size.y", "Float", true, 1.0f, 0.0f, 5.0f, Widget::Vec, {}, true, 1},
      // Offset (Vec2, TiXL t3 default (0,0); x=phase shift, y=vertical offset)
      {"Offset.x", "Offset", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Rotate (float, TiXL t3 default 0.0, degrees)
      {"Rotate", "Rotate", "Float", true, 0.0f, -180.0f, 180.0f},
      // Copies (float, TiXL t3 default 0.0 → clamp to 1 copy; max 20)
      {"Copies", "Copies", "Float", true, 0.0f, 0.0f, 19.0f, Widget::Slider},
      // OffsetCopies (Vec2, TiXL t3 default (0,0.05); per-copy phase/vertical step)
      {"OffsetCopies.x", "OffsetCopies", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"OffsetCopies.y", "OffsetCopies.y", "Float", true, 0.05f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "SinForm", cookSinForm, "sinform", runSinFormSelfTest};

// --- SinForm MATH golden -----------------------------------------------------------------------
// Configuration: default params (Fill=white, Background=black, LineWidth=0.04333334, Fade=1.0,
// Size=(1,1), Offset=(0,0), Rotate=0, Copies=0→1 copy, OffsetCopies=(0,0.05)).
// No input texture (generator mode: no upstream wired).
//
// Manual derivation of the wave at UV center (0.5, 0.5) and at a known wave-peak:
//
// At UV=(0.5, 0.5): Rotate=0 so imageRotationRad = (-0-90)/180*pi = -pi/2.
//   sina = sin(-(-pi/2) - pi/2) = sin(0) = 0
//   cosa = cos(0) = 1
//   p = (0.5-0.5, 0.5-0.5) = (0, 0); p.x *= ar → (0, 0); rotate → (0, 0); p.x /= ar → (0, 0).
//   sin arg = pp.x/SizeX * PI2/2 + OffX/2*PI2 + 0 = 0/1 * pi + 0 = 0
//   sin(0) = 0 → pp.y = p.y + 0 * 0.5 + 0 + 0 = 0.
//   c = abs(0) = 0; feather = 0.04333334 * 1.0 / 2 = 0.021667.
//   smoothstep(0.021667+0.021667, 0.021667-0.021667=0, 0) = smoothstep(0.04333, 0, 0) = 1.
//   smoothstep(0, 1, 1) = 1.  cc = max(0, 1) = 1.
//   col = lerp(Background=(0,0,0,0), Fill=(1,1,1,1), 1) = (1,1,1,1).
//   orgColor = (0,0,0,0) (no input). a = 0+1-0*1 = 1. rgb = (1-1)*(0,0,0) + 1*(1,1,1) = (1,1,1).
//   Result at center UV: white (1,1,1,1).
//
// The wave passes through UV.y=0.5 (p.y=0) at UV.x=0.5 — so center pixel is ON the wave → white.
// Far from the wave (large |pp.y|): c_raw = abs(large) > LineWidth/2+feather → smoothstep → 0.
// At UV=(0.5, 0.8): p.y = 0.3, pp.y = 0.3, c = 0.3 >> 0.04333/2 → cc = 0 → col=Background=(0,0,0,0).
// orgColor=(0,0,0,0), a=0, rgb=black. Result: transparent/black.
//
// GOLDEN ASSERTIONS:
//   (A) Center pixel (W/2, H/2) is WHITE: R≥240, G≥240, B≥240, alpha≥240.
//       (The wave exactly crosses the center at default params, UV=(0.5,0.5).)
//   (B) Top-edge pixel (W/2, H/4) is BLACK/TRANSPARENT: R<20 AND alpha<20.
//       (UV.y=0.25 → p.y=-0.25, well outside the wave band of ~0.021667.)
//
// injectBug: set LineWidth=0.0 → feather=0, smoothstep(0,0,c) is undefined / returns 0 for c>0,
//   so even at the wave center (c=0, borderline) the wave band collapses. The actual GPU
//   behavior for smoothstep(0, 0, 0) is implementation-defined, but smoothstep(0, 0, c>0) = 1
//   and smoothstep(0, 0, c≤0) = 0. At center c=0: we get 1 (same). BUT we use a different
//   injectBug: set Fade=0 → feather=0, LineWidth/2+feather = LineWidth/2-feather = LineWidth/2,
//   smoothstep(x, x, anything) is undefined → in Metal it is clamped so effectively = 0 for
//   values outside the degenerate band, but we pick a stronger injectBug: flip Fill to black
//   (0,0,0,0) → center pixel is black, assertion A fails (R<240 → RED).
//
// GOLDEN ASSERTION (C) — copiesCount floor guard:
//   Copies=2.7, OffsetCopies.y=-0.1, Size.y=0.4, Rotate=0.
//   Correct TiXL: copiesCount = (int)Copies = floor(2.7) = 2. Buggy (round-half-up): = 3.
//   Manual derivation for pixel (row=90, col=64) in 128×128 — UV=(0.5, 90/128=0.703125):
//     p = (0, 0.203125), Rotate=0 → rotation is identity.
//     i=0: pp.y = p.y + sin(0)*0.2 + 0 + 0*0 = 0.203125 (far from wave → c≈0)
//     i=1: pp.y = p.y + sin(0)*0.2 + 0 + (-0.1)*1 = 0.103125 (still far → c≈0)
//     → floor copiesCount=2: cc=0 → col=Background=(0,0,0,0) → pixel BLACK (assertion C passes).
//     i=2 (only with buggy copiesCount=3):
//       pp.y = p.y + sin(0)*0.2 + 0 + (-0.1)*2 = 0.203125 - 0.2 = 0.003125
//       abs(0.003125) < LineWidth/2+feather ≈ 0.021667+0.021667 → c≈0.985 → pixel WHITE.
//     → round copiesCount=3: row=90 is WHITE → assertion C (expect BLACK) FAILS → RED.
//   injectBug for (C): set Copies=3.0 (forces copiesCount=3 under any semantics) → row=90 WHITE
//   → assertion C fails → confirms the guard detects unwanted 3-copy behavior.
int runSinFormSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-sinform] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Output texture — RGBA8Unorm, shared memory for readback.
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // ---- CASE A+B: default 1-copy wave (Fill/Center white, top-quarter black) ------------------
  // Default params (SinForm.t3): 1 copy, white fill, black/transparent background.
  std::map<std::string, float> params;
  // injectBug: flip Fill to black → center wave pixel goes black → assertion A fails.
  params["Fill.r"] = injectBug ? 0.0f : 1.0f;
  params["Fill.g"] = injectBug ? 0.0f : 1.0f;
  params["Fill.b"] = injectBug ? 0.0f : 1.0f;
  params["Fill.a"] = injectBug ? 0.0f : 1.0f;
  params["Background.r"] = 0.0f; params["Background.g"] = 0.0f;
  params["Background.b"] = 0.0f; params["Background.a"] = 0.0f;
  params["LineWidth"] = 0.04333334f;
  params["Fade"]      = 1.0f;
  params["Size.x"]    = 1.0f;  params["Size.y"]    = 1.0f;
  params["Offset.x"]  = 0.0f;  params["Offset.y"]  = 0.0f;
  params["Rotate"]    = 0.0f;
  params["Copies"]    = 0.0f;  // → 1 copy in shader
  params["OffsetCopies.x"] = 0.0f;
  params["OffsetCopies.y"] = 0.05f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // GENERATOR MODE: no upstream texture (TiXL Image=null default)
  c.output = dst;
  c.params = &params;
  cookSinForm(c);

  // Readback.
  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Assertion A: center pixel is on the wave → white.
  const uint32_t cx = W / 2, cy = H / 2;
  size_t ci = ((size_t)cy * W + cx) * 4;
  int cR = out[ci], cG = out[ci+1], cB = out[ci+2], cA = out[ci+3];
  bool centerWhite = (cR >= 200 && cG >= 200 && cB >= 200 && cA >= 200);

  // Assertion B: top-quarter pixel (W/2, H/4) is off-wave → transparent/black.
  const uint32_t tx = W / 2, ty = H / 4;
  size_t ti = ((size_t)ty * W + tx) * 4;
  int tR = out[ti], tA = out[ti+3];
  bool topBlack = (tR < 50 && tA < 50);

  printf("[selftest-sinform] AB center(%d,%d)=(%d,%d,%d,%d) centerWhite=%d | top(%d,%d)=(%d,a=%d) topBlack=%d\n",
         cx, cy, cR, cG, cB, cA, centerWhite ? 1 : 0,
         tx, ty, tR, tA, topBlack ? 1 : 0);

  // ---- CASE C: copiesCount floor guard (TiXL L77: (int)Copies + 0.5 = floor, not round) ------
  // Copies=2.7 → TiXL floor → copiesCount=2. Round-half-up → copiesCount=3 (bug).
  // With OffsetCopies.y=-0.1, pixel (row=90, col=64) is OFF the 2-copy wave pattern (BLACK),
  // but ON the 3rd copy wave (WHITE) only if copiesCount=3.
  // injectBug: set Copies=3.0 → forces copiesCount=3 regardless of floor/round → pixel WHITE
  //   → assertion C fails → confirms the guard detects the 3-copy divergence.
  // Manual derivation: see header comment "GOLDEN ASSERTION (C)" above.
  std::map<std::string, float> paramsC;
  paramsC["Fill.r"] = 1.0f; paramsC["Fill.g"] = 1.0f;
  paramsC["Fill.b"] = 1.0f; paramsC["Fill.a"] = 1.0f;
  paramsC["Background.r"] = 0.0f; paramsC["Background.g"] = 0.0f;
  paramsC["Background.b"] = 0.0f; paramsC["Background.a"] = 0.0f;
  paramsC["LineWidth"] = 0.04333334f;
  paramsC["Fade"]      = 1.0f;
  paramsC["Size.x"]    = 1.0f;   paramsC["Size.y"]    = 0.4f;  // amplitude 0.2 (Size.y/2)
  paramsC["Offset.x"]  = 0.0f;   paramsC["Offset.y"]  = 0.0f;
  paramsC["Rotate"]    = 0.0f;
  // injectBug: Copies=3.0 forces 3 copies (wave reaches row=90). Correct: Copies=2.7 → floor=2.
  paramsC["Copies"]    = injectBug ? 3.0f : 2.7f;
  paramsC["OffsetCopies.x"] = 0.0f;
  paramsC["OffsetCopies.y"] = -0.1f;  // per-copy step pulls copies downward in UV

  MTL::Texture* dstC = dev->newTexture(td);
  c.nodeId = 2; c.output = dstC; c.params = &paramsC;
  cookSinForm(c);

  std::vector<uint8_t> outC((size_t)W * H * 4, 0);
  dstC->getBytes(outC.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // row=90, col=64: with floor copiesCount=2 this pixel is BLACK (off-wave).
  // With round copiesCount=3 the 3rd copy (i=2, OffCopY=-0.1*2=-0.2) lands here → WHITE.
  const uint32_t cr = 90, cc_col = W / 2;
  size_t cCi = ((size_t)cr * W + cc_col) * 4;
  int cCR = outC[cCi], cCA = outC[cCi+3];
  // Expect: BLACK (floor=2 copies, wave does not reach row=90).
  bool copiesFloorBlack = (cCR < 30 && cCA < 30);

  printf("[selftest-sinform] C copies-floor-guard pixel(%d,%d)=(%d,a=%d) expectBlack=%d "
         "(Copies=%.1f copiesCount=%s)\n",
         cc_col, cr, cCR, cCA, copiesFloorBlack ? 1 : 0,
         paramsC["Copies"], injectBug ? "3(inject)" : "floor(2.7)=2");

  bool pass = centerWhite && topBlack && copiesFloorBlack;
  printf("[selftest-sinform] -> %s\n", pass ? "PASS" : "FAIL");

  dstC->release();
  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
