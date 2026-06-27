// TransformWithImage — a texture-into-points seam consumer (PointCookCtx::inputTextures[0]). Faithful
// port of external/tixl .../point/modify/TransformWithImage.{cs,t3} +
// .../Assets/shaders/points/modify/TranslateWithImage.hlsl (the op GUID is TransformWithImage; the shader
// file is TranslateWithImage). A count-preserving MODIFIER: each point samples an Image, derives a
// per-point strength = Strength·(ApplyGainAndBias(gray+scatter) + StrengthOffset)·(StrengthFactor channel),
// then applies a host-composed TRS TransformMatrix lerp-blended by that strength. See
// transformwithimage_params.h / .metal.
//
// .t3 DEFAULT AUDIT (external/tixl .../point/modify/TransformWithImage.t3):
//   Strength=1.0 | StrengthFactor=0 (None) | StrengthOffset=0 | Scatter=0 | GainAndBias=(0.5,0.5)
//   (identity) | Translate=0 | Scale=(1,1,1) | ScaleUniform=1.0 | Rotate=0 | Center=0 | Stretch=(1,1) |
//   ImageScale=1.0 | TextureRotate=0 | TextureMode=Clamp | TranslationSpace=1 (Object) | Channel=0 |
//   ScaleFx1=1.0 | ScaleFx2=1.0 | Image=null.
//   At ALL defaults the TransformMatrix is IDENTITY (Translate 0, Scale 1, Rotate 0) -> the op is a no-op
//   even at strength=0.5 (lerp toward the same position). So the golden drives a non-trivial Translate.
//   fork-channel-scalefx-dead: Channel/ScaleFx1/ScaleFx2 are read by the .cs but unused in the kernel.
//   TranslationSpace enum (.cs Spaces): Point=0, Object=1.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"
#include "runtime/graph.h"
#include "runtime/point_graph.h"
#include "runtime/tixl_point.h"
#include "runtime/transformwithimage_params.h"  // TransformImgParams, TFIMG_* bindings

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookTransformWithImage(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  const MTL::Texture* tex = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;
  if (!tex) {
    std::memcpy(c.output->contents(), const_cast<MTL::Buffer*>(srcBag)->contents(),
                (size_t)c.count * sizeof(SwPoint));
    return;
  }

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("transformwithimage", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  TransformImgParams P{};
  P.Count = c.count;
  float center[3] = {0, 0, 0};
  cookVecN(c, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  // transformSampleSpace (uv): SScale3 = (Stretch.x·Aspect·ImageScale, Stretch.y·ImageScale, ImageScale).
  float stretch[2] = {1.0f, 1.0f};
  cookVecN(c, "Stretch", stretch, 2, stretch);
  const float imageScale = cookParam(c, "ImageScale", 1.0f);
  const float texW = (float)tex->width(), texH = (float)tex->height();
  const float aspect = (texH != 0.0f) ? (texW / texH) : 1.0f;
  P.SScaleX = stretch[0] * aspect * imageScale;
  P.SScaleY = stretch[1] * imageScale;
  P.SScaleZ = imageScale;
  float srot[3] = {0, 0, 0};
  cookVecN(c, "TextureRotate", srot, 3, srot);
  P.SRotX = srot[0]; P.SRotY = srot[1]; P.SRotZ = srot[2];

  // TransformMatrix (the move): TScale3 = Scale·ScaleUniform, TR from Rotate, Translate.
  float scale[3] = {1.0f, 1.0f, 1.0f};
  cookVecN(c, "Scale", scale, 3, scale);
  const float scaleUniform = cookParam(c, "ScaleUniform", 1.0f);
  P.TScaleX = scale[0] * scaleUniform;
  P.TScaleY = scale[1] * scaleUniform;
  P.TScaleZ = scale[2] * scaleUniform;
  float trot[3] = {0, 0, 0};
  cookVecN(c, "Rotate", trot, 3, trot);
  P.TRotX = trot[0]; P.TRotY = trot[1]; P.TRotZ = trot[2];
  float trans[3] = {0, 0, 0};
  cookVecN(c, "Translate", trans, 3, trans);
  P.TransX = trans[0]; P.TransY = trans[1]; P.TransZ = trans[2];

  P.Strength = cookParam(c, "Strength", 1.0f);
  P.Scatter = cookParam(c, "Scatter", 0.0f);
  float gain[2] = {0.5f, 0.5f};
  cookVecN(c, "GainAndBias", gain, 2, gain);
  P.GainX = gain[0]; P.GainY = gain[1];
  P.StrengthOffset = cookParam(c, "StrengthOffset", 0.0f);
  P.StrengthFactor = std::round(cookParam(c, "StrengthFactor", 0.0f));      // 0 None,1 F1,2 F2
  P.TranslationSpace = std::round(cookParam(c, "TranslationSpace", 1.0f));  // .t3 default 1 (Object)

  // Sampler: Linear (TiXL default) + wrap from TextureMode (.t3 default Clamp).
  const int texMode = (int)std::lround(cookParam(c, "TextureMode", 1.0f));
  MTL::SamplerAddressMode addr;
  switch (texMode) {
    case 1:  addr = MTL::SamplerAddressModeClampToEdge;        break;
    case 2:  addr = MTL::SamplerAddressModeMirrorRepeat;       break;
    case 3:  addr = MTL::SamplerAddressModeClampToBorderColor; break;
    default: addr = MTL::SamplerAddressModeRepeat;             break;
  }
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(addr);
  sd->setTAddressMode(addr);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, TFIMG_SourcePoints);
  enc->setBuffer(c.output, 0, TFIMG_ResultPoints);
  enc->setBytes(&P, sizeof(P), TFIMG_Params);
  enc->setTexture(const_cast<MTL::Texture*>(tex), TFIMG_InputTexture);
  enc->setSamplerState(samp, TFIMG_TexSampler);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  samp->release();
  pso->release();
}

}  // namespace

void registerTransformWithImageOp() {
  registerPointOp("TransformWithImage", cookTransformWithImage);
}

// ============================================================================================
// Golden — a UNIFORM image (gray=0.5) + a pure-Translate TransformMatrix makes the move ANALYTIC.
//
//   Image uniform (128,128,128,255) -> gray = 0.5. Scatter=0 -> f = ApplyGainAndBias(0.5, (0.5,0.5)) = 0.5
//   (identity at g=b=0.5: GetSchlickBias(0.5,0.5)=0.5, GetBias(0.5,0.5)=0.5). StrengthFactor=0 -> fx=1.
//   strength = Strength(1)·(0.5 + StrengthOffset(0))·1 = 0.5.
//   TransformMatrix = pure Translate (1,0,0), Scale (1,1,1), ScaleUniform 1, Rotate 0. TranslationSpace=1
//   (Object): movedPos = qRotate(pos·1, identity) + (1,0,0) = (p.Position - Center) + (1,0,0). Center=0 ->
//   movedPos = p.Position + (1,0,0). newPos = lerp(p.Position, movedPos, 0.5) = p.Position + (0.5,0,0).
//   A point at (0,0,0) -> newPos = (0.5, 0, 0). FIXED, analytic.
//   injectBug drops the texture -> gray=0 -> f = ApplyGainAndBias(0) = 0 (value<1e-5 -> 0) -> strength=0 ->
//   newPos = lerp(p.Position, *, 0) = p.Position = (0,0,0) -> RED.
// ============================================================================================

namespace {

constexpr uint32_t kTexW = 8, kTexH = 8;

MTL::Texture* makeUniformTexT(MTL::Device* dev, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kTexW, kTexH, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  std::vector<uint8_t> px((size_t)kTexW * kTexH * 4);
  for (size_t i = 0; i < (size_t)kTexW * kTexH; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, kTexW, kTexH), 0, px.data(), kTexW * 4);
  return t;
}

bool tfimgLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture, SwPoint& out) {
  SwPoint in{};
  in.Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
  in.Rotation = SW_FLOAT4{0, 0, 0, 1};
  in.Scale = SW_PACKED3{1, 1, 1};

  MTL::Buffer* srcBag = dev->newBuffer(&in, sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Texture* tex = makeUniformTexT(dev, 128, 128, 128, 255);  // gray ~0.5

  std::map<std::string, float> params;
  params["Center.x"] = 0; params["Center.y"] = 0; params["Center.z"] = 0;
  params["Stretch.x"] = 1.0f; params["Stretch.y"] = 1.0f; params["ImageScale"] = 1.0f;
  params["TextureRotate.x"] = 0; params["TextureRotate.y"] = 0; params["TextureRotate.z"] = 0;
  params["TextureMode"] = 1.0f;  // Clamp
  params["Scale.x"] = 1.0f; params["Scale.y"] = 1.0f; params["Scale.z"] = 1.0f; params["ScaleUniform"] = 1.0f;
  params["Rotate.x"] = 0; params["Rotate.y"] = 0; params["Rotate.z"] = 0;
  params["Translate.x"] = 1.0f; params["Translate.y"] = 0.0f; params["Translate.z"] = 0.0f;
  params["Strength"] = 1.0f; params["Scatter"] = 0.0f;
  params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;
  params["StrengthOffset"] = 0.0f; params["StrengthFactor"] = 0.0f;
  params["TranslationSpace"] = 1.0f;  // Object

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {1};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = 1;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (wireTexture) { c.inputTextures[0] = tex; c.inputTextureCount = 1; }
  else { c.inputTextureCount = 0; }
  cookTransformWithImage(c);

  std::memcpy(&out, outBag->contents(), sizeof(SwPoint));
  srcBag->release(); outBag->release(); tex->release();
  return true;
}

}  // namespace

int runTransformWithImageSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-transformwithimage] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  SwPoint out{};
  tfimgLeg(dev, q, lib, /*wireTexture=*/!injectBug, out);
  // gray=0.5 -> f=0.5 -> strength=0.5 ; Translate=(1,0,0), Object -> newPos = (0,0,0) + (0.5,0,0).
  const float wantX = 0.5f;
  float errX = std::fabs(out.Position.x - wantX);
  float errYZ = std::fabs(out.Position.y) + std::fabs(out.Position.z);
  bool pass = (errX < 3e-3f) && (errYZ < 3e-3f);
  std::printf("[selftest-transformwithimage] newPos=(%.5f,%.5f,%.5f) want=(0.5,0,0) errX=%.5f errYZ=%.5f "
              "injectBug=%d -> %s\n",
              out.Position.x, out.Position.y, out.Position.z, errX, errYZ, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
