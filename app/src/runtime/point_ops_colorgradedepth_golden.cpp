// ColorGradeDepth MATH golden (400-line ratchet split from point_ops_colorgradedepth.cpp — the cook
// fn + registrar live there; cookColorGradeDepth + the injectBug accessors are non-anonymous-namespace
// `sw::` symbols so this sibling TU can drive them directly, mirroring the split-file convention
// point_ops_loadimage.cpp / point_ops_loadimage_golden.cpp already uses).
//
// Two closed-form teeth, both pinned at the EXACT CENTER pixel of an ODD-sized (33x33) render target
// — pixel index 16's rasterized texCoord = (16+0.5)/33 = 0.5 EXACTLY (an even-sized target's "middle"
// pixel center is off by half a texel, e.g. 16.5/32=0.515625, which would NOT zero the vignette term
// below) — with VignetteRadius=1/VignetteFeather=1/VignetteCenter=(0,0) (the TiXL defaults): at that
// exact pixel `v` (the vignette term) collapses to smoothstep(0,1,0)=0 REGARDLESS of VignetteColor
// (hand-derived: uv-0.5-center=0 -> v=0 -> v/=(radius*1/2)=0 -> v-=0.5 -> v=-0.5 ->
// smoothstep(0,1,(-0.5-0.5)/(1*1*2)+0.5) = smoothstep(0,1,0) = 0). This isolates the two OTHER terms
// (grading, gradient-tint) for a tractable closed-form pin, mirroring RgbTV's "pin the analytically-
// simple pixel" methodology (point_ops_rgbtv.cpp).
//
// CASE A (grading path, ColorGradeWithDepth.hlsl :65-74): Gain=(0.6,0.6,0.6,0.5) (OFF the TiXL-
// identity value 0.5 — for a Vec4 with rgb==0.5, liftScaled/gammaScaled/gainScaled collapse to 0.5
// regardless of alpha, i.e. TiXL's "neutral grade" identity; moving rgb to 0.6 leaves the ALGEBRA
// live: gainScaled = 0.6*2*0.5 + (0.5-0.5) = 0.6). Gamma=Lift=TiXL defaults (0.5,0.5,0.5,x) ->
// gammaScaled=liftScaled=0.5 exactly -> pow exponent 1/(0.5*2)=1 (linear, no pow distortion) and the
// lift term (liftScaled*2-1)=0 (vanishes). Gradient port UNWIRED -> op's own neutral-gray default ->
// (gradientColor.rgb-0.5)=0 -> contributes nothing. PreSaturate=1 (no desaturation). Uniform solid
// input color c0=(0.4,0.4,0.4,1.0):
//   c.rgb = pow((0.4 + 0*(1-0.4)) * 0.6*2, 1) = 0.4 * 1.2 = 0.48   (exact; no depth/gradient term)
//   -> RGBA8Unorm byte = round(0.48*255) = 122, alpha byte 255.
// injectBug (dropGrading): forces GainX/Y/Z back to 0.5 (the identity value) in the REAL cook path ->
// gainScaled=0.5 -> gainScaled*2=1 -> c.rgb=0.4 unchanged -> byte 102 (0.4*255=102), missing the
// 122 pin -> RED.
//
// CASE B (depth/gradient path, :43-49,:68-69): Gain/Gamma/Lift back to TiXL defaults (identity grade,
// gainScaled=0.5 baseline). A UNIFORM 2-stop gradient (both stops = the SAME off-default color
// (0.6,0.6,0.6,1.0) — ALL CHANNELS ABOVE 0.5 on purpose, see pow-domain note below; sidestepping LUT-
// interpolation precision the same way RgbTV pins a UNIFORM noise texture) is wired to the Gradient
// port: gradientColor is (0.6,0.6,0.6,1.0) at EVERY normalizedZ, so the depth/NearClip/FarClip/
// GradientDepthRange chain only has to land the sample INSIDE [0,1] (it does, for any finite depth) —
// the tooth targets the "Gradient input correctly flows to a bound LUT and perturbs gainScaled" seam,
// not the DepthToSceneZ arithmetic itself (accepted scope: the depth->normalizedZ chain is exercised
// as plumbing, not pinned digit-for-digit).
//   gainScaled = 0.5 (identity) + (0.6-0.5)*(1.0*2+1) = 0.5 + 0.1*3 = 0.8   (all 3 channels, uniform)
//   c.rgb (input 0.4,0.4,0.4): pow(0.4*0.8*2, 1) = pow(0.64,1) = 0.64
//     (POW-DOMAIN NOTE: Metal's pow(x,y) is UNDEFINED for x<0 regardless of y — even at the
//     mathematically-trivial y=1 exponent this shader always reduces to here (gammaScaled=0.5 ->
//     1/(0.5*2)=1). The tint is deliberately chosen ABOVE 0.5 on every channel so gainScaled stays
//     POSITIVE and the pow argument (0.64) never goes negative — an EARLIER draft used a tint with a
//     channel below 0.5, which drove that channel's gainScaled negative and the pow argument negative
//     -> undefined on real hardware. Caught before landing; keep any future edit to this tint on the
//     same side of 0.5 for every channel.)
//   -> RGBA8Unorm byte = round(0.64*255) = 163, alpha byte 255.
// So Case B's exact-pixel pin is (163,163,163,255) — clearly distinct from Case A's 122 AND from the
// untouched-input 102, so a miss can't be mistaken for "some other path fired instead."
// injectBug (dropGradient): rebinds a FLAT neutral LUT (0.5,0.5,0.5,0.5) regardless of the real wired
// gradient -> the (gradientColor.rgb-0.5) term is 0 -> gainScaled stays 0.5 (identity) -> c.rgb=0.4
// unchanged -> byte (102,102,102,255), missing the (163,163,163,255) pin -> RED.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/point_graph.h"    // TexCookCtx
#include "runtime/sw_gradient.h"    // SwGradient
#include "runtime/tex_op_cache.h"   // clearTexOpCache

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Declared (non-anon) in point_ops_colorgradedepth.cpp.
void cookColorGradeDepth(TexCookCtx& c);
bool& colorGradeDepthDropGrading();
bool& colorGradeDepthDropGradient();

namespace {

constexpr uint32_t kCGDW = 33, kCGDH = 33;  // ODD size: pixel 16's texCoord = 16.5/33 = 0.5 exactly
constexpr uint8_t kCGDGray = 102;  // 0.4 * 255 (input solid color)

void cookGrayInput(MTL::Texture* tex) {
  std::vector<uint8_t> in((size_t)kCGDW * kCGDH * 4, 0);
  for (size_t i = 0; i < (size_t)kCGDW * kCGDH; ++i) {
    in[i * 4 + 0] = kCGDGray; in[i * 4 + 1] = kCGDGray; in[i * 4 + 2] = kCGDGray; in[i * 4 + 3] = 255;
  }
  tex->replaceRegion(MTL::Region::Make2D(0, 0, kCGDW, kCGDH), 0, in.data(), kCGDW * 4);
}

}  // namespace

int runColorGradeDepthSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-colorgradedepth] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kCGDW, kCGDH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* image = dev->newTexture(td);
  MTL::Texture* depth = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);
  cookGrayInput(image);
  cookGrayInput(depth);  // depth value irrelevant (Case A gradient neutral; Case B uses UNIFORM gradient)

  auto centerPixel = [&](MTL::Texture* t, uint8_t out[4]) {
    std::vector<uint8_t> px((size_t)kCGDW * kCGDH * 4, 0);
    t->getBytes(px.data(), kCGDW * 4, MTL::Region::Make2D(0, 0, kCGDW, kCGDH), 0);
    size_t i = (((size_t)kCGDH / 2) * kCGDW + kCGDW / 2) * 4;
    for (int k = 0; k < 4; ++k) out[k] = px[i + k];
  };

  // ===== CASE A: grading path (Gain off-identity, Gradient unwired/neutral) =====
  colorGradeDepthDropGrading() = injectBug;
  colorGradeDepthDropGradient() = false;
  {
    std::map<std::string, float> params;
    params["Gain.x"] = 0.6f; params["Gain.y"] = 0.6f; params["Gain.z"] = 0.6f; params["Gain.w"] = 0.5f;
    // Gamma/Lift/PreSaturate/Vignette left at TiXL defaults (fillParams' baked-in defaults).
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.output = dst; c.params = &params;
    c.inputTextures[0] = image; c.inputTextures[1] = depth; c.inputTextureCount = 2;
    c.inputGradients = nullptr;  // unwired -> op's own neutral default
    cookColorGradeDepth(c);
  }
  colorGradeDepthDropGrading() = false;
  uint8_t gotA[4];
  centerPixel(dst, gotA);
  const uint8_t wantA[4] = {122, 122, 122, 255};  // 0.4*1.2=0.48 -> round(0.48*255)=122
  bool matchA = true;
  for (int k = 0; k < 4; ++k) if (std::abs((int)gotA[k] - (int)wantA[k]) > 2) matchA = false;

  // ===== CASE B: depth/gradient path (identity grade, uniform off-default gradient wired) =====
  colorGradeDepthDropGrading() = false;
  colorGradeDepthDropGradient() = injectBug;
  std::vector<SwGradient> grads(1);
  grads[0].interpolation = kGradientLinear;
  const simd::float4 kTint = simd::make_float4(0.6f, 0.6f, 0.6f, 1.0f);  // all channels > 0.5 (pow-domain)
  grads[0].steps.push_back({0.0f, kTint});
  grads[0].steps.push_back({1.0f, kTint});  // uniform: same color at both stops
  {
    std::map<std::string, float> params;
    // Gain/Gamma/Lift left at TiXL defaults (identity grade) via fillParams. NearClip/FarClip/
    // GradientDepthRange chosen so normalizedZ lands mid-range (~0.538, see header derivation) rather
    // than at an endpoint — moot for THIS pin's numeric value (the wired gradient is uniform, so any
    // normalizedZ in [0,1] samples the same color), kept mid-range anyway so the depth chain is
    // genuinely exercised as plumbing, not degenerate-at-a-saturation-plateau.
    params["CamNearFarClip.x"] = 1.0f;
    params["CamNearFarClip.y"] = 2.0f;
    params["GradientDepthRange.x"] = 1.5f;
    params["GradientDepthRange.y"] = 2.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 2; c.output = dst; c.params = &params;
    c.inputTextures[0] = image; c.inputTextures[1] = depth; c.inputTextureCount = 2;
    c.inputGradients = &grads;
    cookColorGradeDepth(c);
  }
  colorGradeDepthDropGradient() = false;
  uint8_t gotB[4];
  centerPixel(dst, gotB);
  const uint8_t wantB[4] = {163, 163, 163, 255};  // gainScaled=0.8 -> 0.4*0.8*2=0.64 -> round(0.64*255)=163
  bool matchB = true;
  for (int k = 0; k < 4; ++k) if (std::abs((int)gotB[k] - (int)wantB[k]) > 2) matchB = false;

  bool pass = matchA && matchB;
  printf("[selftest-colorgradedepth] caseA(grading) got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) match=%d | "
         "caseB(depth/gradient) got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) match=%d | injectBug=%d -> %s\n",
         gotA[0], gotA[1], gotA[2], gotA[3], wantA[0], wantA[1], wantA[2], wantA[3], matchA ? 1 : 0,
         gotB[0], gotB[1], gotB[2], gotB[3], wantB[0], wantB[1], wantB[2], wantB[3], matchB ? 1 : 0,
         injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  image->release(); depth->release(); dst->release();
  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
