// StarGlowStreaks image-filter texture op (image/fx/stylize) — directional glow-streak stylize.
// TiXL authority: Operators/Lib/image/fx/stylize/StarGlowStreaks.cs ([Input] order
//   Image(Texture2D)->Color(Vector4)->Range(float)->Brightness(float)->Threshold(float)->
//   BlendMode(int enum RgbBlendModes)->OriginalColor(Vector4)->Quality(int)->GlareModes(int enum
//   Methods{Diagonals,Cross,Star,Horizontal,Vertical})) + StarGlowStreaks.t3 (defaults:
//   Color=(1,1,1,1), Range=0.1, Brightness=3.0, Threshold=0.5, BlendMode=8 (linearDodge),
//   OriginalColor=(1,1,1,1), Quality=0, GlareModes=0 (Diagonals); _ImageFxShaderSetupStatic
//   Wrap=MirrorOnce, OutputFormat=R16G16B16A16_Float, Filter default MinMagMipLinear) +
//   Assets/shaders/img/fx/StarGlowStreaks.hlsl (single-pass kernel — ported line-for-line in
//   starglowstreaks.metal, blend-functions.hlsl BlendColors inlined there).
//
// Single-pass port: cookStarGlowStreaks reads c.inputTexture (the upstream RenderTarget's Texture2D
// via the I1 gather direct-through), runs one fullscreen pass of starglowstreaks_vs/_fs, writes
// c.output. Color/OriginalColor (Vector4) are decomposed into .x/.y/.z/.w Float ports (Widget::Vec,
// arity 4), mirroring the Vector2 decomposition in TransformImage. BlendMode/GlareModes (int enums)
// and Quality (int) are cooked as floats (the _ForceKind int->float pattern; the kernel dispatches
// by (int)BlendMode / GlareModes float compares / level(Quality)).
//
// FORKS (named — full HLSL->MSL rationale in starglowstreaks.metal header):
//  [fork-range-const]   .hlsl local `const float range=0.3` is DEAD (loop bound is cbuffer Range);
//    ported the LIVE path (bound=Range, step=0.002 literal). No knob invented.
//  [fork-glaremodes-branch] The tautological GlareModes routing is ported VERBATIM (not simplified)
//    so active-direction sets match: 0=Diag1+Diag2, 1=H+V, 2=Diag1+Diag2+H+V, 3=H, 4=V.
//  [fork-diag1-scalar-uv]   `uv + i` (HLSL scalar->vec promotion) = uv + float2(i,i). Verbatim.
//  [fork-vertical-i2]       Vertical sample stride is i*2 (double). Verbatim.
//  [fork-samplelevel-quality] SampleLevel(.,.,Quality) -> sample(.,., level(Quality)). Final
//    orgColor uses Sample (mip0). Quality is the explicit LOD per .hlsl. Verbatim.
//  [fork-sampler-wrap]      Sampler = MirrorOnce + linear, matching StarGlowStreaks.t3
//    (_ImageFxShaderSetupStatic Wrap=MirrorOnce + Filter default MinMagMipLinear). Set in the cook.
//  [fork-OutputFormat]      .t3 OutputFormat=R16G16B16A16_Float — TexCookCtx has no per-op output-
//    format seam; the op writes c.output's EXISTING pixel format. Honest no-op (not a NodeSpec port:
//    OutputFormat is on the internal _ImageFxShaderSetupStatic node, NOT a StarGlowStreaks.cs
//    [Input], so it is NOT listed — listing it would invent a knob TiXL does not expose on this op).
//
// Self-contained leaf: cookStarGlowStreaks + _reg_starglowstreaks registrar + runStarGlowStreaksSelfTest.
// Shares the PSO+scratch cache seam (tex_op_cache.h) with Tint/Blur/Displace/ConvertColors/TransformImage.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/starglowstreaks_params.h"     // StarGlowStreaksParams, SGS_Params
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"               // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward decl (defined at the bottom of this leaf) — declared HERE (not in shared point_ops.h) so
// this op stays a zero-shared-file-edit self-registered leaf. The registrar references it before
// its definition; the selftest dispatcher resolves it via the imageFilterSelfTests() sink.
int runStarGlowStreaksSelfTest(bool injectBug);

namespace {

// StarGlowStreaks texture op: single pass. Reads c.inputTexture (upstream tex op's output), writes
// c.output. No upstream texture wired: clear output to black — mirrors cookTint/cookTransformImage.
void cookStarGlowStreaks(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "starglowstreaks_vs", "starglowstreaks_fs", fmt);
  if (!rps) return;

  // [fork-sampler-wrap] MirrorOnce + linear, matching StarGlowStreaks.t3 (Wrap=MirrorOnce,
  // Filter default MinMagMipLinear). MipFilter linear so SampleLevel(Quality) selects a real LOD.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMipFilter(MTL::SamplerMipFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeMirrorClampToEdge);  // MirrorOnce
  sd->setTAddressMode(MTL::SamplerAddressModeMirrorClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL StarGlowStreaks.cs / .t3 defaults.
  StarGlowStreaksParams p{};
  p.ColorR = cookParam(c, "Color.x", 1.0f);
  p.ColorG = cookParam(c, "Color.y", 1.0f);
  p.ColorB = cookParam(c, "Color.z", 1.0f);
  p.ColorA = cookParam(c, "Color.w", 1.0f);
  p.Range = cookParam(c, "Range", 0.1f);
  p.Brightness = cookParam(c, "Brightness", 3.0f);
  p.Threshold = cookParam(c, "Threshold", 0.5f);
  p.BlendMode = cookParam(c, "BlendMode", 8.0f);  // 8 = linearDodge (t3 default)
  p.OrigR = cookParam(c, "OriginalColor.x", 1.0f);
  p.OrigG = cookParam(c, "OriginalColor.y", 1.0f);
  p.OrigB = cookParam(c, "OriginalColor.z", 1.0f);
  p.OrigA = cookParam(c, "OriginalColor.w", 1.0f);
  p.Quality = cookParam(c, "Quality", 0.0f);
  p.GlareModes = cookParam(c, "GlareModes", 0.0f);  // 0 = Diagonals (t3 default)
  p._pad0 = 0.0f;
  p._pad1 = 0.0f;

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
  enc->setFragmentBytes(&p, sizeof(StarGlowStreaksParams), SGS_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration (replaces register line + node_registry spec + kTable row).
// NodeSpec ports mirror StarGlowStreaks.cs [Input] order VERBATIM:
//   Image -> Color(.x/.y/.z/.w) -> Range -> Brightness -> Threshold -> BlendMode(enum) ->
//   OriginalColor(.x/.y/.z/.w) -> Quality(int) -> GlareModes(enum).
// Vector4 inputs are split into four Float Widget::Vec ports (head vecArity=4). The BlendMode enum
// labels mirror SharedEnums.RgbBlendModes (the blend-functions.hlsl switch cases 0..9); GlareModes
// labels mirror the .cs private enum Methods. NO knob invented (OutputFormat lives on the internal
// shader-setup node, not a .cs [Input], so it is NOT a port — see [fork-OutputFormat] header note).
static const ImageFilterOp _reg_starglowstreaks{
    {"StarGlowStreaks", "StarGlowStreaks",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Color (Vector4, TiXL t3 default (1,1,1,1)).
      {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Range (float, TiXL t3 default 0.1).
      {"Range", "Range", "Float", true, 0.1f, 0.0f, 0.5f, Widget::Slider},
      // Brightness (float, TiXL t3 default 3.0).
      {"Brightness", "Brightness", "Float", true, 3.0f, 0.0f, 20.0f, Widget::Slider},
      // Threshold (float color-key 0..1, TiXL t3 default 0.5).
      {"Threshold", "Threshold", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Slider},
      // BlendMode (int enum RgbBlendModes, TiXL t3 default 8 = LinearDodge). Labels = blend cases 0..9.
      {"BlendMode", "BlendMode", "Float", true, 8.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference", "UseImageA_RGB", "UseImageB_RGB",
        "ColorDodge", "LinearDodge", "MultiplyA"}, true},
      // OriginalColor (Vector4, TiXL t3 default (1,1,1,1)).
      {"OriginalColor.x", "OriginalColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"OriginalColor.y", "OriginalColor.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"OriginalColor.z", "OriginalColor.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"OriginalColor.w", "OriginalColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Quality (int = explicit mip LOD, TiXL t3 default 0).
      {"Quality", "Quality", "Float", true, 0.0f, 0.0f, 8.0f, Widget::Slider},
      // GlareModes (int enum Methods, TiXL t3 default 0 = Diagonals).
      {"GlareModes", "GlareModes", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Diagonals", "Cross", "Star", "Horizontal", "Vertical"}, true},
      // Output-size seam (shared Resolution enum, mirrors Tint/ConvertColors/TransformImage).
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "StarGlowStreaks", cookStarGlowStreaks, "starglowstreaks", runStarGlowStreaksSelfTest};

// --- StarGlowStreaks MATH golden (headless, real GPU assertion) --------------------------------
// TWO hand-computed legs derived from StarGlowStreaks.hlsl for KNOWN inputs.
//
// Test A (exact pixel, pins the THRESHOLD gate + BlendMode=8 path):
//   Source = UNIFORM dark grey C=(0.2,0.2,0.2,1). Sum r+g+b = 0.6 < Threshold*3 (=1.5 at
//   Threshold=0.5) -> the color-key gate FAILS for EVERY sample -> streaksColor stays (0,0,0,1).
//   With a uniform source the result is independent of loop iteration count (no accumulation), so
//   it is EXACT. orgColor = C * OriginalColor(1,1,1,1) = C. BlendColors(orgColor, (0,0,0,1)*Color,8)
//   linearDodge: rgb = orgColor.rgb + 0 = (0.2,0.2,0.2). -> RGBA8 (51,51,51,255) EXACT.
//   injectBug RAISES Threshold above 1.0 won't help — instead injectBug LOWERS the cooked Threshold
//   to 0.0 so 0.6 > 0.0 -> the gate now PASSES, streaks accumulate, rgb grows well above 0.2 ->
//   pixel no longer (51,51,51) -> Test A FAILS rc=1.
//
// Test B (direction, pins the GlareModes Horizontal fork — the fork-prone streak sampling):
//   Source = BLACK except a bright VERTICAL band at columns [30,34). GlareModes=3 (Horizontal):
//   samples uv + float2(i,0) for i in [-Range,Range) -> reaches +-Range(=0.1=6.4px) horizontally.
//   - Probe col 26 (u=0.406): horizontal reach [0.306,0.506) OVERLAPS band [0.469,0.531) -> the
//     band is sampled, gate passes (band is bright) -> pixel becomes NON-BLACK.
//   - Probe col 4  (u=0.063): horizontal reach [-0.038,0.163) NEVER reaches the band -> stays BLACK.
//   This pins the HORIZONTAL direction: vertical sampling (which stays in-column) could not reach a
//   vertical band from col 26. injectBug cooks GlareModes=4 (Vertical) instead of 3: now col 26
//   samples uv+float2(0,i*2) (stays in column 26, all black) -> col 26 stays BLACK -> Test B FAILS.
namespace {

bool isBlackish(const uint8_t* px) { return px[0] < 16 && px[1] < 16 && px[2] < 16; }

}  // namespace

int runStarGlowStreaksSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-starglowstreaks] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  auto at = [&](const std::vector<uint8_t>& buf, uint32_t x, uint32_t y) {
    return &buf[((size_t)y * W + x) * 4];
  };

  bool aPass = false, bPass = false;

  // ---- Test A: uniform dark grey -> threshold gate fails -> exact passthrough (51,51,51) ----
  {
    MTL::Texture* src = dev->newTexture(td);
    std::vector<uint8_t> in((size_t)W * H * 4, 0);
    for (size_t i = 0; i < (size_t)W * H; ++i) {
      in[i * 4 + 0] = 51; in[i * 4 + 1] = 51; in[i * 4 + 2] = 51; in[i * 4 + 3] = 255;  // 51/255≈0.2
    }
    src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

    std::map<std::string, float> params;
    // t3 defaults; injectBug lowers Threshold to 0.0 so the dark source's gate (0.6>thr) flips.
    params["Color.x"] = 1.0f; params["Color.y"] = 1.0f; params["Color.z"] = 1.0f; params["Color.w"] = 1.0f;
    params["Range"] = 0.1f; params["Brightness"] = 3.0f;
    params["Threshold"] = injectBug ? 0.0f : 0.5f;
    params["BlendMode"] = 8.0f;
    params["OriginalColor.x"] = 1.0f; params["OriginalColor.y"] = 1.0f;
    params["OriginalColor.z"] = 1.0f; params["OriginalColor.w"] = 1.0f;
    params["Quality"] = 0.0f; params["GlareModes"] = 0.0f;  // Diagonals
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
    cookStarGlowStreaks(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    const uint8_t* px = at(out, 32, 32);
    // EXACT expectation (uniform source, gate fails -> streaks=0 -> linearDodge adds 0): (51,51,51,255).
    aPass = std::abs((int)px[0] - 51) <= 2 && std::abs((int)px[1] - 51) <= 2 &&
            std::abs((int)px[2] - 51) <= 2 && px[3] >= 250;
    printf("[selftest-starglowstreaks] TestA(thresh-gate) px(32,32)=(%d,%d,%d,%d) expect=(51,51,51,255) -> %s\n",
           px[0], px[1], px[2], px[3], aPass ? "PASS" : "FAIL");
    src->release();
  }

  // ---- Test B: black + bright vertical band, GlareModes=Horizontal -> only horizontally-reachable
  //              probe lights up (col 26 non-black, col 4 stays black). ----
  {
    MTL::Texture* src = dev->newTexture(td);
    std::vector<uint8_t> in((size_t)W * H * 4, 0);
    for (uint32_t y = 0; y < H; ++y) {
      for (uint32_t x = 0; x < W; ++x) {
        size_t i = ((size_t)y * W + x) * 4;
        bool band = (x >= 30 && x < 34);
        uint8_t v = band ? 255 : 0;  // bright white band on black
        in[i + 0] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
      }
    }
    src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

    std::map<std::string, float> params;
    params["Color.x"] = 1.0f; params["Color.y"] = 1.0f; params["Color.z"] = 1.0f; params["Color.w"] = 1.0f;
    // Brightness raised to 30 (10x t3 default): the streak that reaches col 26 is GENUINELY faint
    // (only ~22 of 100 loop steps land the horizontal sample inside the band, each scaled by
    // falloff*steps*Brightness), so at the t3 default Brightness=3 the lit pixel is only ~(7,7,7) —
    // real but below a robust non-black bar. Brightness=30 lifts it to ~(70,70,70), comfortably
    // above black, WITHOUT changing which direction can reach the band (the discriminating signal).
    params["Range"] = 0.1f; params["Brightness"] = 30.0f; params["Threshold"] = 0.5f;
    params["BlendMode"] = 8.0f;
    params["OriginalColor.x"] = 1.0f; params["OriginalColor.y"] = 1.0f;
    params["OriginalColor.z"] = 1.0f; params["OriginalColor.w"] = 1.0f;
    params["Quality"] = 0.0f;
    // GlareModes=3 (Horizontal) normally; injectBug=4 (Vertical) -> col 26 can't reach the band.
    params["GlareModes"] = injectBug ? 4.0f : 3.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
    cookStarGlowStreaks(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    const uint8_t* near = at(out, 26, 32);  // horizontally reaches band -> non-black (Horizontal mode)
    const uint8_t* far  = at(out, 4, 32);   // out of horizontal reach -> stays black
    bool nearLit   = !isBlackish(near);
    bool farDark   = isBlackish(far);
    bPass = nearLit && farDark;
    printf("[selftest-starglowstreaks] TestB(horiz-dir) near(26,32)=(%d,%d,%d) far(4,32)=(%d,%d,%d) "
           "nearLit=%d farDark=%d -> %s\n",
           near[0], near[1], near[2], far[0], far[1], far[2],
           nearLit ? 1 : 0, farDark ? 1 : 0, bPass ? "PASS" : "FAIL");
    src->release();
  }

  bool pass = aPass && bPass;
  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
