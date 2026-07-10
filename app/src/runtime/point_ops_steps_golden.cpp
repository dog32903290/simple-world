// Steps MATH golden (400-line ratchet split from point_ops_steps.cpp — the cook fn + registrar live
// there; cookSteps + the injectBug accessors are non-anonymous-namespace `sw::` symbols so this
// sibling TU can drive them directly, mirroring the split-file convention point_ops_colorgradedepth
// .cpp / _golden.cpp already uses).
//
// Two closed-form teeth on a SOLID uniform-gray input (sidesteps texture-sampling precision the
// same way ColorGradeDepth/RgbTV pin a uniform source), both with UseSuperSampling=Repeat=false (an
// "accepted scope" note, mirroring ColorGradeDepth's depth-chain plumbing note: the Repeat=true
// wraparound/edge-blend sub-branch (Steps.hlsl :55-64) only has a NONZERO effect within ~0.005 of
// gray=0 or gray=1 — steps.metal's `edge` term saturates to 0 for any mid-range gray, so a mid-range
// probe cannot pin that branch's arithmetic without extra precision risk; exercised as plumbing via
// the UseSuperSampling=false / Repeat=false path here, not pinned digit-for-digit).
//
// CASE A (highlight-blend path, Steps.hlsl :74-80): StepCount=4, Bias=0.5 (Bias2(x,0.5)=x, the
// IDENTITY case — 1/0.5-2=0 kills the (1-x) term — chosen to keep the index/main/remainder chain
// hand-tractable; the DISTINGUISHING behavior under test here is the highlight blend + ramp/edge LUT
// compositing, not Bias2's curve shape, which CASE B exercises instead), Offset=0, uniform gray
// input g=0.4, Ramp=UNIFORM(0.5,0.5,0.5,1.0), Edge=UNIFORM(0,0,0,0) (fully transparent -> inert):
//   gray=0.4 -> biased=0.4 -> c=0.4 -> modulo=saturate(0.4)=0.4 -> index=int(0.4*4)=1 ->
//   main=1/3=0.33333 -> remainder=(0.4-0.33333)*4+0.33333=0.6.
//   HighlightIndex=1 -> modi(1,4)=1 -> index(1)==1 -> isHighlight=TRUE.
//   Highlight=(0.2,0.3,0.4,0.6) (off-default, alpha>0 so the blend is genuinely exercised):
//     mainRampColor = lerp((0.5,0.5,0.5,1.0), (0.2,0.3,0.4,0.6), 0.6) = (0.32,0.38,0.44,0.76).
//   edgeRampColor = (0,0,0,0) (uniform, any t) -> a=clamp(0.76+0-0,0,1)=0.76,
//     rgb=(1-0)*mainRampColor.rgb=(0.32,0.38,0.44).
//   -> RGBA8Unorm bytes = (round(0.32*255),round(0.38*255),round(0.44*255),round(0.76*255))
//                       = (82,97,112,194).
// injectBug (dropHighlight): forces Highlight.a=0 in the REAL cook path -> mainRampColor collapses
//   to rampColor=(0.5,0.5,0.5,1.0) regardless of isHighlight -> bytes (128,128,128,255) -> miss.
//
// CASE B (Bias2 curve + edge-composite path, Steps.hlsl :35-38,:92-93): StepCount=5, Bias=0.25
// (Bias2(x,0.25)=x/(3-2x), genuinely off-identity), Offset=2, uniform gray input g=0.6,
// Ramp=UNIFORM(0.2,0.6,0.8,1.0), Edge=UNIFORM(0.9,0.1,0.5,0.5) (alpha=0.5, nonzero -> the
// alpha-composite is genuinely exercised, unlike Case A's inert edge):
//   gray=0.6 -> biased=0.6/(3-1.2)=0.33333 -> c=0.33333+2/5=0.73333 -> modulo=saturate(0.73333)
//     =0.73333 -> index=int(0.73333*5)=3 -> main=3/4=0.75 -> remainder=(0.73333-0.75)*5+0.75
//     =0.66667.
//   HighlightIndex=0 -> modi(0,5)=0 != index(3) -> isHighlight=FALSE -> mainRampColor=rampColor
//     =(0.2,0.6,0.8,1.0) (uniform Ramp, any t).
//   edgeRampColor=(0.9,0.1,0.5,0.5) (uniform, any t):
//     a=clamp(1.0+0.5-1.0*0.5,0,1)=clamp(1.0,0,1)=1.0.
//     rgb=(1-0.5)*(0.2,0.6,0.8) + 0.5*(0.9,0.1,0.5) = (0.1,0.3,0.4)+(0.45,0.05,0.25)=(0.55,0.35,0.65).
//   -> RGBA8Unorm bytes = (140,89,166,255).
// injectBug (dropEdge): rebinds a FULLY-TRANSPARENT flat LUT for Edge regardless of the real wired
//   gradient -> edgeRampColor.a=0 -> rgb=mainRampColor.rgb=(0.2,0.6,0.8) -> bytes (51,153,204,255)
//   -> miss.
//
// The two cases' expected bytes (82,97,112,194) vs (140,89,166,255) — and their bug-state bytes
// (128,128,128,255) vs (51,153,204,255) — are all four mutually distinct, so a miss on either probe
// cannot be mistaken for the other's expected value.
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

// Declared (non-anon) in point_ops_steps.cpp.
void cookSteps(TexCookCtx& c);
bool& stepsDropHighlight();
bool& stepsDropEdge();

namespace {

constexpr uint32_t kW = 16, kH = 16;  // uniform input -> any pixel is representative, no center-pin needed

void cookUniformGray(MTL::Texture* tex, uint8_t gray) {
  std::vector<uint8_t> in((size_t)kW * kH * 4, 0);
  for (size_t i = 0; i < (size_t)kW * kH; ++i) {
    in[i * 4 + 0] = gray; in[i * 4 + 1] = gray; in[i * 4 + 2] = gray; in[i * 4 + 3] = 255;
  }
  tex->replaceRegion(MTL::Region::Make2D(0, 0, kW, kH), 0, in.data(), kW * 4);
}

SwGradient uniformGradient(simd::float4 col) {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, col});
  g.steps.push_back({1.0f, col});
  return g;
}

}  // namespace

int runStepsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-steps] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kW, kH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* imgA = dev->newTexture(td);
  MTL::Texture* imgB = dev->newTexture(td);
  MTL::Texture* dstA = dev->newTexture(td);
  MTL::Texture* dstB = dev->newTexture(td);
  cookUniformGray(imgA, 102);  // 0.4*255
  cookUniformGray(imgB, 153);  // 0.6*255

  auto anyPixel = [&](MTL::Texture* t, uint8_t out[4]) {
    std::vector<uint8_t> px((size_t)kW * kH * 4, 0);
    t->getBytes(px.data(), kW * 4, MTL::Region::Make2D(0, 0, kW, kH), 0);
    for (int k = 0; k < 4; ++k) out[k] = px[k];  // pixel (0,0) — uniform result, any pixel matches
  };

  // ===== CASE A: highlight-blend path =====
  stepsDropHighlight() = injectBug;
  stepsDropEdge() = false;
  std::vector<SwGradient> gradsA = {
      uniformGradient(simd::make_float4(0.5f, 0.5f, 0.5f, 1.0f)),  // Ramp
      uniformGradient(simd::make_float4(0.0f, 0.0f, 0.0f, 0.0f)),  // Edge (inert)
  };
  {
    std::map<std::string, float> params;
    params["Count"] = 4.0f; params["Bias"] = 0.5f; params["Offset"] = 0.0f;
    params["HighlightIndex"] = 1.0f;
    params["Highlight.x"] = 0.2f; params["Highlight.y"] = 0.3f;
    params["Highlight.z"] = 0.4f; params["Highlight.w"] = 0.6f;
    params["Repeat"] = 0.0f; params["UseSuperSampling"] = 0.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.output = dstA; c.params = &params;
    c.inputTextures[0] = imgA; c.inputTextureCount = 1; c.inputTexture = imgA;
    c.inputGradients = &gradsA;
    cookSteps(c);
  }
  stepsDropHighlight() = false;
  uint8_t gotA[4];
  anyPixel(dstA, gotA);
  const uint8_t wantA[4] = {82, 97, 112, 194};
  bool matchA = true;
  for (int k = 0; k < 4; ++k) if (std::abs((int)gotA[k] - (int)wantA[k]) > 2) matchA = false;

  // ===== CASE B: Bias2 curve + edge-composite path =====
  stepsDropHighlight() = false;
  stepsDropEdge() = injectBug;
  std::vector<SwGradient> gradsB = {
      uniformGradient(simd::make_float4(0.2f, 0.6f, 0.8f, 1.0f)),  // Ramp
      uniformGradient(simd::make_float4(0.9f, 0.1f, 0.5f, 0.5f)),  // Edge
  };
  {
    std::map<std::string, float> params;
    params["Count"] = 5.0f; params["Bias"] = 0.25f; params["Offset"] = 2.0f;
    params["HighlightIndex"] = 0.0f;
    params["Highlight.x"] = 1.0f; params["Highlight.y"] = 1.0f;
    params["Highlight.z"] = 1.0f; params["Highlight.w"] = 0.0f;
    params["Repeat"] = 0.0f; params["UseSuperSampling"] = 0.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 2; c.output = dstB; c.params = &params;
    c.inputTextures[0] = imgB; c.inputTextureCount = 1; c.inputTexture = imgB;
    c.inputGradients = &gradsB;
    cookSteps(c);
  }
  stepsDropEdge() = false;
  uint8_t gotB[4];
  anyPixel(dstB, gotB);
  const uint8_t wantB[4] = {140, 89, 166, 255};
  bool matchB = true;
  for (int k = 0; k < 4; ++k) if (std::abs((int)gotB[k] - (int)wantB[k]) > 2) matchB = false;

  bool pass = matchA && matchB;
  printf("[selftest-steps] caseA(highlight) got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) match=%d | "
         "caseB(bias2/edge) got=(%d,%d,%d,%d) want=(%d,%d,%d,%d) match=%d | injectBug=%d -> %s\n",
         gotA[0], gotA[1], gotA[2], gotA[3], wantA[0], wantA[1], wantA[2], wantA[3], matchA ? 1 : 0,
         gotB[0], gotB[1], gotB[2], gotB[3], wantB[0], wantB[1], wantB[2], wantB[3], matchB ? 1 : 0,
         injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  imgA->release(); imgB->release(); dstA->release(); dstB->release();
  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
