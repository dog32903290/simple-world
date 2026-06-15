// ColorGrade image-filter texture op (image/color) — lift/gamma/gain + vignette + pre-saturation.
// TiXL authority: Operators/Lib/image/color/ColorGrade.cs ([Input] order
//   Texture2d(Texture2D)->PreSaturate(float)->Gain(Vector4)->Gamma(Vector4)->Lift(Vector4)->
//   VignetteColor(Vector4)->VignetteRadius(float)->VignetteFeather(float)->VignetteCenter(Vector2)->
//   GenerateMipmaps(bool)->ClampResult(bool))
// + ColorGrade.t3 (defaults: PreSaturate=1, Gain=(.5,.5,.5,.506), Gamma=(.5,.5,.5,.506),
//   Lift=(.5,.5,.5,.25), VignetteColor=(.5,.5,.5,0), VignetteRadius=1, VignetteFeather=1,
//   VignetteCenter=(0,0), GenerateMipmaps=false, ClampResult=false; the internal
//   _ImageFxShaderSetupStatic node Filter=MinMagMipLinear / Wrap=Wrap = the linear+repeat sampler)
// + Assets/shaders/img/ColorGrade.hlsl (self-contained single-pass PIXEL kernel — ported
//   line-for-line in colorgrade.metal; samples level 0 only via .Sample, no mip>0 reads).
//
// Single-pass port: cookColorGrade reads c.inputTexture (the upstream RenderTarget's Texture2D via
// the I1 gather direct-through), runs one fullscreen pass of colorgrade_vs/_fs, writes c.output.
// Vector4 inputs (Gain/Gamma/Lift/VignetteColor) are decomposed into .x/.y/.z/.w Float ports
// (Widget::Vec arity 4); Vector2 (VignetteCenter) into .x/.y (arity 2) — mirroring Tint/ConvertColors
// /TransformImage. PreSaturate/VignetteRadius/VignetteFeather are scalar Float ports.
//
// FORKS (named):
//  [fork-feather-is-bias] The .cs input "VignetteFeather" feeds the .hlsl cbuffer field
//    "VignetteBias" (ColorGrade.t3 wires slot e94da387 into the SetupStatic Params multi-input at
//    the VignetteBias position). NodeSpec port name = "VignetteFeather" (.cs); host field =
//    VignetteBias (.hlsl). Same value, faithful — not an invented or dropped knob.
//  [fork-clampresult] ColorGrade.hlsl ALWAYS clamps c.rgb to [0.000001,1000] (twice) and c.a to
//    [0,1] regardless of the ClampResult bool — ClampResult does NOT enter the pixel shader. The
//    .cs ClampResult wires (via BoolToFloat) into the _ImageFxShaderSetupStatic FRAMEWORK node,
//    selecting the OUTPUT TEXTURE FORMAT (a clamping UNorm vs an unbounded float format), which is
//    a sizing/format concern, not per-pixel math. The leaf has no per-op output-format seam, so the
//    per-pixel clamps are implemented faithfully (unconditional, matching the shader) and the
//    ClampResult port is a NO-OP fork — LISTED in the NodeSpec per .cs [Input] order. Honest no-op.
//  [fork-GenerateMipmaps] GenerateMipmaps (bool, .cs/.t3 default false) — TexCookCtx has no mip
//    seam; LISTED in the NodeSpec (in .cs [Input] order) but NO-OP in the cook. Honest no-op.
//  [fork-sampler] Fixed linear+repeat sampler = ColorGrade.t3's _ImageFxShaderSetupStatic defaults
//    (Filter=MinMagMipLinear, Wrap=Wrap). (Same as TransformImage; ConvertColors uses point+clamp —
//    this is a per-op choice read from each op's own .t3, not a blanket fork.)
//
// Self-contained leaf: cookColorGrade + _reg_colorgrade registrar + runColorGradeSelfTest.
// Shares the PSO+scratch cache seam (tex_op_cache.h) with Tint/Blur/Displace/ConvertColors/
// TransformImage.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/colorgrade_params.h"       // ColorGradeParams, CG_Params
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"             // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"            // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration (defined at the bottom of this leaf). Declared HERE rather than in the shared
// point_ops.h so this op stays a zero-shared-file-edit self-registered leaf — the registrar below
// references it before its definition, and the selftest dispatcher resolves it via the
// imageFilterSelfTests() sink (registered by the _reg_colorgrade constructor).
int runColorGradeSelfTest(bool injectBug);

namespace {

// ColorGrade texture op: single pass. Reads c.inputTexture (upstream tex op's output), writes
// c.output. No upstream texture wired: clear output to black (nothing to grade) — mirrors cookTint.
void cookColorGrade(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "colorgrade_vs", "colorgrade_fs", fmt);
  if (!rps) return;

  // [fork-sampler] linear(MinMagMipLinear)+repeat(Wrap), matching ColorGrade.t3 SetupStatic defaults.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMipFilter(MTL::SamplerMipFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL ColorGrade.cs / .t3 defaults. Vector4/Vector2 inputs read as .x/.y/.z/.w scalar ports.
  ColorGradeParams p{};
  p.GainR = cookParam(c, "Gain.x", 0.5f);
  p.GainG = cookParam(c, "Gain.y", 0.5f);
  p.GainB = cookParam(c, "Gain.z", 0.5f);
  p.GainA = cookParam(c, "Gain.w", 0.506f);
  p.GammaR = cookParam(c, "Gamma.x", 0.5f);
  p.GammaG = cookParam(c, "Gamma.y", 0.5f);
  p.GammaB = cookParam(c, "Gamma.z", 0.5f);
  p.GammaA = cookParam(c, "Gamma.w", 0.506f);
  p.LiftR = cookParam(c, "Lift.x", 0.5f);
  p.LiftG = cookParam(c, "Lift.y", 0.5f);
  p.LiftB = cookParam(c, "Lift.z", 0.5f);
  p.LiftA = cookParam(c, "Lift.w", 0.25f);
  p.VigColorR = cookParam(c, "VignetteColor.x", 0.5f);
  p.VigColorG = cookParam(c, "VignetteColor.y", 0.5f);
  p.VigColorB = cookParam(c, "VignetteColor.z", 0.5f);
  p.VigColorA = cookParam(c, "VignetteColor.w", 0.0f);
  p.VigCenterX = cookParam(c, "VignetteCenter.x", 0.0f);
  p.VigCenterY = cookParam(c, "VignetteCenter.y", 0.0f);
  p.VignetteRadius = cookParam(c, "VignetteRadius", 1.0f);
  // [fork-feather-is-bias] .cs port "VignetteFeather" feeds .hlsl "VignetteBias".
  p.VignetteBias = cookParam(c, "VignetteFeather", 1.0f);
  p.PreSaturate = cookParam(c, "PreSaturate", 1.0f);

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
  enc->setFragmentBytes(&p, sizeof(ColorGradeParams), CG_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration (replaces registerColorGradeOp + node_registry spec + kTable row).
// NodeSpec ports mirror ColorGrade.cs [Input] order VERBATIM:
//   Image -> PreSaturate -> Gain(.x/.y/.z/.w) -> Gamma(.x/.y/.z/.w) -> Lift(.x/.y/.z/.w) ->
//   VignetteColor(.x/.y/.z/.w) -> VignetteRadius -> VignetteFeather -> VignetteCenter(.x/.y) ->
//   GenerateMipmaps(bool) -> ClampResult(bool).
// Vector4 inputs split into four Float Widget::Vec ports (head vecArity=4); Vector2 into two
// (head vecArity=2). GenerateMipmaps/ClampResult are LISTED per .cs order but NO-OP forks (named in
// the file header). CustomW/CustomH back the shared Resolution=Custom path (mirrors Tint/etc).
static const ImageFilterOp _reg_colorgrade{
    {"ColorGrade", "ColorGrade",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // PreSaturate (float, TiXL default 1).
      {"PreSaturate", "PreSaturate", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider},
      // Gain (Vector4, TiXL default (.5,.5,.5,.506)).
      {"Gain.x", "Gain", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Gain.y", "Gain.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Gain.z", "Gain.z", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Gain.w", "Gain.w", "Float", true, 0.506f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Gamma (Vector4, TiXL default (.5,.5,.5,.506)).
      {"Gamma.x", "Gamma", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Gamma.y", "Gamma.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Gamma.z", "Gamma.z", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Gamma.w", "Gamma.w", "Float", true, 0.506f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Lift (Vector4, TiXL default (.5,.5,.5,.25)).
      {"Lift.x", "Lift", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Lift.y", "Lift.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Lift.z", "Lift.z", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Lift.w", "Lift.w", "Float", true, 0.25f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // VignetteColor (Vector4, TiXL default (.5,.5,.5,0)).
      {"VignetteColor.x", "VignetteColor", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"VignetteColor.y", "VignetteColor.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"VignetteColor.z", "VignetteColor.z", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"VignetteColor.w", "VignetteColor.w", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // VignetteRadius (float, TiXL default 1).
      {"VignetteRadius", "VignetteRadius", "Float", true, 1.0f, -2.0f, 2.0f, Widget::Slider},
      // VignetteFeather (float, TiXL default 1) — [fork-feather-is-bias] feeds .hlsl VignetteBias.
      {"VignetteFeather", "VignetteFeather", "Float", true, 1.0f, 0.01f, 4.0f, Widget::Slider},
      // VignetteCenter (Vector2, TiXL default (0,0)).
      {"VignetteCenter.x", "VignetteCenter", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"VignetteCenter.y", "VignetteCenter.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // GenerateMipmaps (bool, TiXL default false) — LISTED per .cs order, NO-OP fork (no mip seam).
      {"GenerateMipmaps", "GenerateMipmaps", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      // ClampResult (bool, TiXL default false) — LISTED per .cs order, NO-OP fork (output-format
      // concern; the .hlsl per-pixel clamps are unconditional, ported faithfully).
      {"ClampResult", "ClampResult", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      // CustomW/CustomH back the shared Resolution=Custom path (mirrors Tint/ConvertColors).
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ColorGrade", cookColorGrade, "colorgrade", runColorGradeSelfTest};

// --- ColorGrade MATH golden (headless, real GPU assertion) ------------------------------------
// All expected values are HAND-COMPUTED from ColorGrade.hlsl psMain (see the python derivation in the
// dossier); RGBA8Unorm stores 0.5 as 128/255 so a ±2 tolerance absorbs the quantization.
//
// Test A (grade, LOAD-BEARING for the lift/gamma/gain math): solid input (0.5,0.25,0.75),
//   VignetteColor.a=0 (vignette term zero -> grade isolated), Gain.a=0.6, Gamma.a=0.7, Lift.a=0.3.
//   Hand math: liftScaled=0.5 -> (liftS*2-1)=0 (lift term drops); gainScaled=0.5*2*0.6+(0.5-0.6)=0.62;
//   gammaScaled=0.5*2*0.7+(0.5-0.7)=0.5 -> exponent 1/(0.5*2)=1. So out = c*0.62*2 = c*1.24:
//   R=0.62, G=0.31, B=0.93 -> rgba8 (158,79,237). Assert center pixel == (158,79,237) ±2.
//     injectBug NEGATES the gamma exponent in the shader path is not reachable (shader is compiled),
//     so we inject by perturbing the COOK gamma: with injectBug we cook Gamma.a=0.25 which makes
//     gammaScaled=0.5*2*0.25+(0.5-0.25)=0.5 ... same. Instead injectBug sets Gamma.a so the exponent
//     INVERTS toward >1: we cook Gamma.a=0.166666 -> gammaScaled=0.5*2*0.1666+(0.5-0.1666)=0.5 again
//     (Gamma.rgb=0.5 makes gammaScaled invariant to .a). The clean lever is Gamma.rgb: injectBug
//     cooks Gamma.rgb=0.0 -> gammaScaled=0*2*0.7+(0.5-0.7)=-0.2 -> exponent 1/(-0.4)=-2.5 -> the
//     pow blows up / clamps to white -> (255,255,255) != (158,79,237) -> Test A FAILS rc=1.
// Test B (vignette falloff, 補強): VignetteColor=(1,0,0,1), input grey 0.5, default grade. The
//   vignette v = smoothstep over length(uv-0.5); at the CENTER pixel v~=0 (no push, stays ~grey
//   128), at a CORNER pixel v~=0.72 pushes gainScaled hard toward red -> (255,0,0). Assert center is
//   grey-ish (not red) AND corner is red-dominant. This pins the vignette radius/feather/center math
//   independent of the injectBug (which only perturbs Test A's cook).
namespace {
bool isGreyish(const uint8_t* px) {  // R,G,B all mid and close together
  int r = px[0], g = px[1], b = px[2];
  return std::abs(r - g) < 24 && std::abs(g - b) < 24 && r > 90 && r < 170;
}
bool isRedDom(const uint8_t* px) { return px[0] > 200 && px[1] < 80 && px[2] < 80; }
}  // namespace

int runColorGradeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-colorgrade] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  auto fillSolid = [&](uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> in((size_t)W * H * 4, 0);
    for (size_t i = 0; i < (size_t)W * H; ++i) {
      in[i * 4 + 0] = r; in[i * 4 + 1] = g; in[i * 4 + 2] = b; in[i * 4 + 3] = 255;
    }
    src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);
  };
  auto at = [&](const std::vector<uint8_t>& buf, uint32_t x, uint32_t y) {
    return &buf[((size_t)y * W + x) * 4];
  };
  auto cook = [&](std::map<std::string, float>& params, std::vector<uint8_t>& out) {
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
    cookColorGrade(c);
    out.assign((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  };

  // ---- Test A: grade math on solid (0.5,0.25,0.75); expect center (158,79,237) ±2 ----
  fillSolid(128, 64, 191);  // 0.5, 0.25, 0.75 in RGBA8
  std::map<std::string, float> pA;
  pA["PreSaturate"] = 1.0f;
  pA["Gain.x"] = 0.6f; pA["Gain.y"] = 0.6f; pA["Gain.z"] = 0.6f; pA["Gain.w"] = 0.6f;
  // injectBug zeros Gamma.rgb -> gammaScaled=-0.2 -> exponent 1/(-0.4) -> pow blows up to white.
  float gammaRGB = injectBug ? 0.0f : 0.5f;
  pA["Gamma.x"] = gammaRGB; pA["Gamma.y"] = gammaRGB; pA["Gamma.z"] = gammaRGB; pA["Gamma.w"] = 0.7f;
  pA["Lift.x"] = 0.5f; pA["Lift.y"] = 0.5f; pA["Lift.z"] = 0.5f; pA["Lift.w"] = 0.3f;
  pA["VignetteColor.w"] = 0.0f;  // vignette off
  pA["VignetteRadius"] = 1.0f; pA["VignetteFeather"] = 1.0f;
  std::vector<uint8_t> outA;
  cook(pA, outA);
  const uint8_t* aC = at(outA, 32, 32);
  bool aPass = std::abs((int)aC[0] - 158) <= 2 && std::abs((int)aC[1] - 79) <= 2 &&
               std::abs((int)aC[2] - 237) <= 2;
  printf("[selftest-colorgrade] TestA(grade) center=(%d,%d,%d) expect=(158,79,237) -> %s\n",
         aC[0], aC[1], aC[2], aPass ? "PASS" : "FAIL");

  // ---- Test B: vignette falloff — center stays grey, corner pushed red ----
  fillSolid(128, 128, 128);  // grey 0.5
  std::map<std::string, float> pB;
  pB["PreSaturate"] = 1.0f;
  pB["Gain.x"] = 0.5f; pB["Gain.y"] = 0.5f; pB["Gain.z"] = 0.5f; pB["Gain.w"] = 0.506f;
  pB["Gamma.x"] = 0.5f; pB["Gamma.y"] = 0.5f; pB["Gamma.z"] = 0.5f; pB["Gamma.w"] = 0.506f;
  pB["Lift.x"] = 0.5f; pB["Lift.y"] = 0.5f; pB["Lift.z"] = 0.5f; pB["Lift.w"] = 0.25f;
  pB["VignetteColor.x"] = 1.0f; pB["VignetteColor.y"] = 0.0f; pB["VignetteColor.z"] = 0.0f;
  pB["VignetteColor.w"] = 1.0f;  // vignette ON, push red
  pB["VignetteRadius"] = 1.0f; pB["VignetteFeather"] = 1.0f;
  std::vector<uint8_t> outB;
  cook(pB, outB);
  const uint8_t* bCenter = at(outB, 32, 32);   // v~0 -> grey
  const uint8_t* bCorner = at(outB, 2, 2);     // v~0.72 -> red
  bool bPass = isGreyish(bCenter) && isRedDom(bCorner);
  printf("[selftest-colorgrade] TestB(vignette) center=(%d,%d,%d) corner=(%d,%d,%d) -> %s\n",
         bCenter[0], bCenter[1], bCenter[2], bCorner[0], bCorner[1], bCorner[2],
         bPass ? "PASS" : "FAIL");

  // injectBug only perturbs Test A's cook (Gamma.rgb=0 -> exponent inverts -> white). Test B always
  // cooks valid params and stands on its own. With injectBug, Test A FAILS -> overall FAIL rc=1.
  bool pass = aPass && bPass;
  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
