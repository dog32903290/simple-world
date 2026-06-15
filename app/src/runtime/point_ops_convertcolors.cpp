// ConvertColors image-filter texture op (image/color) — RGB<->OkLab / RGB<->LCh converter.
// TiXL authority: Operators/Lib/image/color/ConvertColors.cs ([Input] order
// Texture2d(Texture2D)->Mode(int enum Modes)->GenerateMipmaps(bool)->OutputFormat(Format)) +
// ConvertColors.t3 (defaults: Mode=0, GenerateMipmaps=false, OutputFormat=R32G32B32A32_Float,
// _ImageFxShaderSetupStatic Filter=MinMagMipPoint) + Assets/shaders/img/adjust/
// img-fx-ConvertColors.hlsl (the single-pass kernel: SampleLevel then Mode<0.5/<1.5/<2.5/<3.5
// branches calling color-functions.hlsl's RgbToOkLab/OklabToRgb/RgbToLCh/LChToRgb).
//
// Single-pass port: cookConvertColors reads c.inputTexture (the upstream RenderTarget's Texture2D
// via the I1 gather direct-through), runs one fullscreen pass of convertcolors_vs/convertcolors_fs,
// writes c.output. Mode (int enum) is cooked as one float param into ConvertColorsParams.Mode and
// dispatched by float thresholds in the kernel (the _ForceKind int->float pattern).
//
// FORKS (named):
//  [fork-GenerateMipmaps] ConvertColors.cs:16-17 InputSlot<bool> GenerateMipmaps — TexCookCtx has
//    no mipmap seam, so this is a NO-OP (no mips generated). The port is still LISTED in the NodeSpec
//    (in .cs [Input] order) but unread by the cook. Honest no-op, not an invented knob.
//  [fork-OutputFormat] ConvertColors.cs:19-20 InputSlot<Format> OutputFormat (t3 default
//    R32G32B32A32_Float) — TexCookCtx has no output-format seam; the op writes c.output's EXISTING
//    pixel format. Listed in NodeSpec, no-op in cook. Honest no-op.
//  [fork-sampler] Fixed point(nearest)+clamp sampler — matches ConvertColors.t3's
//    _ImageFxShaderSetupStatic Filter=MinMagMipPoint. (Tint/AdjustColors use linear; this op is
//    point per its .t3 — a per-op choice, not a blanket clamp-fork.)
//  [fork-matrix-mul] color-functions.hlsl HLSL row-major float3x3 mul(M,v)/mul(v,M) ported to MSL
//    column-major — see convertcolors.metal header (the SAME invB is M*v in RgbToOkLab and v*M in
//    RgbToLCh; ported per-function, NOT a single mul direction).
//
// Self-contained leaf: cookConvertColors + registerConvertColorsOp() + runConvertColorsSelfTest.
// Shares the PSO+scratch cache seam (tex_op_cache.h) with Tint/Blur/Displace.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/convertcolors_params.h"  // ConvertColorsParams, CC_Params
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"           // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"          // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// ConvertColors texture op: single pass. Reads c.inputTexture (upstream tex op's output), writes
// c.output. No upstream texture wired: clear output to black (nothing to convert) — mirrors cookTint.
void cookConvertColors(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "convertcolors_vs", "convertcolors_fs", fmt);
  if (!rps) return;

  // fork-sampler: point(nearest)+clamp, matching ConvertColors.t3 Filter=MinMagMipPoint.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL ConvertColors.cs / .t3: Mode default 0 (RgbToOKLab). GenerateMipmaps/OutputFormat are
  // listed ports but NO-OP in this cook (forks named in the file header).
  ConvertColorsParams p{};
  p.Mode = cookParam(c, "Mode", 0.0f);

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
  enc->setFragmentBytes(&p, sizeof(ConvertColorsParams), CC_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration (replaces registerConvertColorsOp + node_registry spec + kTable row).
// NodeSpec literal moved verbatim from the old node_registry_image_filter.cpp::imageFilterSpecs().
static const ImageFilterOp _reg_convertcolors{
    // ConvertColors (TiXL Lib.image.color.ConvertColors): RGB<->OkLab / RGB<->LCh color-space
    // converter. Ports mirror ConvertColors.cs [Input] order verbatim:
    // Texture2d→Mode→GenerateMipmaps→OutputFormat. FORKS (named):
    //  - GenerateMipmaps (bool, .cs:16-17) LISTED but NO-OP (TexCookCtx has no mip seam).
    //  - OutputFormat (Format, .cs:19-20, t3 default R32G32B32A32_Float) LISTED but NO-OP
    //    (op writes c.output's existing format; no format seam). Enum shows the t3 default first.
    //  - Fixed point(nearest)+clamp sampler = ConvertColors.t3 Filter=MinMagMipPoint (verbatim).
    {"ConvertColors", "ConvertColors",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Mode (int enum Modes, TiXL t3 default 0 = RgbToOKLab).
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
       {"RgbToOKLab", "OKLabToRgb", "RgbToLCh", "LChToRgb"}, true},
      // GenerateMipmaps (bool, TiXL t3 default false) — LISTED per .cs [Input] order, NO-OP fork.
      {"GenerateMipmaps", "GenerateMipmaps", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      // OutputFormat (Format, TiXL t3 default R32G32B32A32_Float) — LISTED, NO-OP fork.
      {"OutputFormat", "OutputFormat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"R32G32B32A32_Float", "(output format follows pipeline)"}, true},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ConvertColors", cookConvertColors, "convertcolors", runConvertColorsSelfTest};

// --- ConvertColors MATH golden (headless, real GPU assertion) ---------------------------------
// Test A (forward hand-computed, LOAD-BEARING — guards the matrix mul DIRECTION per the silent-
// color-corruption fork): fill a 64x64 source with a solid non-grey color RGBA(200,100,50,255),
// run ConvertColors Mode=2 (RgbToLCh), read back the center pixel and assert it equals the LCh
// hand-computed on CPU with the SAME color-functions.hlsl formula (c·invB -> sign*pow(|.|,1/3) ->
// ·invA [row-vector both] -> polar L/C/H). The expected RGBA8 center pixel is (199,24,168) ±2.
//   A round-trip-only test would MASK a both-directions-wrong matrix (the inverse error cancels);
//   Test A pins one absolute forward value so a flipped mul direction (e.g. RgbToLCh using
//   mul(invB,col) instead of mul(col,invB)) lands on a different LCh and FAILS.
// Test B (round-trip補強): Mode=0 (RgbToOKLab) then Mode=1 (OKLabToRgb) over two passes should
// return the original color ±4/255.
// injectBug flips RgbToLCh's matrix multiply direction in the CPU expectation (mul(invB,col)
// instead of mul(col,invB)) so the GPU (correct) center pixel no longer matches the (now wrong)
// CPU expectation -> Test A FAILS rc=1. (We inject on the EXPECTED side because the shader is
// compiled; this still proves the assertion bites a direction error.)
namespace {

using Mat3 = float[3][3];
// color-functions.hlsl row-major matrices (rows as written in the HLSL initializer).
const Mat3 kInvB = {{0.4121656120f, 0.2118591070f, 0.0883097947f},
                    {0.5362752080f, 0.6807189584f, 0.2818474174f},
                    {0.0514575653f, 0.1074065790f, 0.6302613616f}};
const Mat3 kInvA = {{0.2104542553f, 1.9779984951f, 0.0259040371f},
                    {0.7936177850f, -2.4285922050f, 0.7827717662f},
                    {-0.0040720468f, 0.4505937099f, -0.8086757660f}};

// HLSL mul(M, v): result_r = dot(row_r, v).
void mulMv(const Mat3 M, const float v[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = M[r][0] * v[0] + M[r][1] * v[1] + M[r][2] * v[2];
}
// HLSL mul(v, M): result_c = sum_r v[r]*M[r][c].
void mulvM(const float v[3], const Mat3 M, float out[3]) {
  for (int cc = 0; cc < 3; ++cc) out[cc] = v[0] * M[0][cc] + v[1] * M[1][cc] + v[2] * M[2][cc];
}
void sgnPow13(const float v[3], float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = std::copysign(std::pow(std::fabs(v[i]), 1.0f / 3.0f), v[i]);
}

// CPU mirror of color-functions.hlsl RgbToLCh. injectBug flips the FIRST mul to mul(invB,col)
// (M*v) instead of mul(col,invB) (v*M), simulating a ported-the-wrong-direction matrix.
void cpuRgbToLCh(const float rgb[3], bool injectBug, float lch[3]) {
  float col[3];
  if (injectBug) mulMv(kInvB, rgb, col);  // BUG: wrong direction
  else mulvM(rgb, kInvB, col);            // correct: mul(col, invB)
  float sp[3];
  sgnPow13(col, sp);
  float col2[3];
  mulvM(sp, kInvA, col2);  // mul(sign*pow, invA)
  lch[0] = col2[0];
  lch[1] = std::sqrt(col2[1] * col2[1] + col2[2] * col2[2]);
  lch[2] = std::atan2(col2[2], col2[1]) / (2.0f * 3.141592f) + 0.5f;
}

}  // namespace

int runConvertColorsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-convertcolors] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // ---- Test A: forward RgbToLCh on solid RGBA(200,100,50,255) ----
  {
    std::vector<uint8_t> in((size_t)W * H * 4, 0);
    for (size_t i = 0; i < (size_t)W * H; ++i) {
      in[i * 4 + 0] = 200; in[i * 4 + 1] = 100; in[i * 4 + 2] = 50; in[i * 4 + 3] = 255;
    }
    src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

    std::map<std::string, float> params;
    params["Mode"] = 2.0f;  // RgbToLCh
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
    cookConvertColors(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    size_t i = ((size_t)(H / 2) * W + (W / 2)) * 4;
    int gR = out[i], gG = out[i + 1], gB = out[i + 2];

    // CPU expectation (injectBug flips the matrix mul direction -> expectation diverges from GPU).
    float rgb[3] = {200.0f / 255.0f, 100.0f / 255.0f, 50.0f / 255.0f};
    float lch[3];
    cpuRgbToLCh(rgb, injectBug, lch);
    auto to8 = [](float v) { return (int)std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f); };
    int eR = to8(lch[0]), eG = to8(lch[1]), eB = to8(lch[2]);

    bool aPass = std::abs(gR - eR) <= 2 && std::abs(gG - eG) <= 2 && std::abs(gB - eB) <= 2;
    printf("[selftest-convertcolors] TestA(RgbToLCh) gpu=(%d,%d,%d) expect=(%d,%d,%d) -> %s\n",
           gR, gG, gB, eR, eG, eB, aPass ? "PASS" : "FAIL");

    // ---- Test B: round-trip Mode=0 then Mode=1 should return original ±4 ----
    std::map<std::string, float> p0; p0["Mode"] = 0.0f;  // RgbToOKLab
    // OkLab a/b are SMALL and NEGATIVE — an RGBA8Unorm intermediate would clamp them to 0 and
    // destroy the round-trip. TiXL avoids this with t3 OutputFormat=R32G32B32A32_Float; we mirror
    // that here with a float intermediate (the [fork-OutputFormat] no-op only affects the
    // production cook, which writes the node's own texture; this golden owns its mid texture).
    MTL::TextureDescriptor* tdf =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA32Float, W, H, false);
    tdf->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    tdf->setStorageMode(MTL::StorageModeShared);
    MTL::Texture* mid = dev->newTexture(tdf);
    TexCookCtx c0; c0.dev = dev; c0.lib = lib; c0.queue = q;
    c0.nodeId = 1; c0.inputTexture = src; c0.output = mid; c0.params = &p0;
    cookConvertColors(c0);
    std::map<std::string, float> p1; p1["Mode"] = 1.0f;  // OKLabToRgb
    TexCookCtx c1; c1.dev = dev; c1.lib = lib; c1.queue = q;
    c1.nodeId = 1; c1.inputTexture = mid; c1.output = dst; c1.params = &p1;
    cookConvertColors(c1);
    std::vector<uint8_t> rt((size_t)W * H * 4, 0);
    dst->getBytes(rt.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    size_t j = ((size_t)(H / 2) * W + (W / 2)) * 4;
    bool bPass = std::abs((int)rt[j] - 200) <= 4 && std::abs((int)rt[j + 1] - 100) <= 4 &&
                 std::abs((int)rt[j + 2] - 50) <= 4;
    printf("[selftest-convertcolors] TestB(roundtrip) back=(%d,%d,%d) orig=(200,100,50) -> %s\n",
           rt[j], rt[j + 1], rt[j + 2], bPass ? "PASS" : "FAIL");
    mid->release();

    bool pass = aPass && bPass;
    src->release(); dst->release();
    lib->release(); q->release(); dev->release(); pool->release();
    return pass ? 0 : 1;
  }
}

}  // namespace sw
