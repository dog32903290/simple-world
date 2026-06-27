// SamplePointAttributes_v1 — a texture-into-points seam consumer (SAME seam as
// SamplePointColorAttributes / AttributesFromImageChannels: PointCookCtx::inputTextures[0] +
// host-composed transformSampleSpace). Faithful port of external/tixl
// .../point/modify/SamplePointAttributes_v1.cs (.cs ports) +
// .../Assets/shaders/points/modify/SamplePointAttributes.hlsl (the kernel). A count-preserving
// MODIFIER: each point samples `inputTexture` at uv = (pos-Center).xy*(1,-1)+0.5 (via transformSampleSpace),
// then ROUTES the sampled brightness(L)/R/G/B channels — each through its Attributes routing enum +
// per-channel Factor/Offset — into Position xyz / W(FX1) / Rotation / Stretch(Scale). See
// samplepointattributes_params.h / .metal for the full .hlsl trace.
//
// .t3 DEFAULT AUDIT (external/tixl .../point/modify/SamplePointAttributes_v1.t3):
//   Scale=1.0 (NOT 2.0 like SPCA!) | Stretch=(1,1) | TextureRotate=0 | Center=0 | TextureMode=Wrap |
//   Brightness/Red/Green/Blue/Alpha enums = 0 (NotUsed) | all *Factor/*Offset = 0 | Mode=0 (Add) |
//   RotationSpace=1 (Point) | TranslationSpace=0 (Object). At ALL defaults the op is a PASSTHROUGH
//   (every channel NotUsed, every factor 0) — so the golden drives a non-default routing to exercise it.
//   fork-alpha-dead-in-kernel: the .cs Alpha/AlphaFactor/AlphaOffset inputs are commented OUT in the .hlsl
//   (lines 47-49) — the NodeSpec carries them for 1:1 .cs parity but they route nothing (faithful).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                   // calcDispatchCount
#include "runtime/graph.h"
#include "runtime/point_graph.h"                // PointCookCtx, registerPointOp
#include "runtime/samplepointattributes_params.h"  // SampleAttrParams, SAMPLEATTR_* bindings
#include "runtime/tex_op_cache.h"
#include "runtime/tixl_point.h"                 // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// SamplePointAttributes cook: sample inputTextures[0] per point -> route channels into attributes.
// Unwired Points input -> nothing; unwired Texture2D -> passthrough copy (mirror SPCA / crop seam guard).
void cookSamplePointAttributes(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  const MTL::Texture* tex = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;
  if (!tex) {
    std::memcpy(c.output->contents(), const_cast<MTL::Buffer*>(srcBag)->contents(),
                (size_t)c.count * sizeof(SwPoint));
    return;
  }

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "samplepointattributes");
  if (!pso) return;

  SampleAttrParams P{};
  P.Count = c.count;
  float center[3] = {0, 0, 0};
  cookVecN(c, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  // transformSampleSpace (host half): Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, Scale);
  // Aspect = texW/texH (.t3 Div). Scale .t3 default 1.0 (NOT 2.0 like SPCA).
  float stretch[2] = {1.0f, 1.0f};
  cookVecN(c, "Stretch", stretch, 2, stretch);
  const float scaleU = cookParam(c, "Scale", 1.0f);
  const float texW = (float)tex->width(), texH = (float)tex->height();
  const float aspect = (texH != 0.0f) ? (texW / texH) : 1.0f;
  P.ScaleX = stretch[0] * aspect * scaleU;
  P.ScaleY = stretch[1] * scaleU;
  P.ScaleZ = scaleU;
  float rot[3] = {0, 0, 0};
  cookVecN(c, "TextureRotate", rot, 3, rot);
  P.RotX = rot[0]; P.RotY = rot[1]; P.RotZ = rot[2];

  // Channel routing enums + per-channel Factor/Offset (Brightness == L in the kernel).
  P.L = cookParam(c, "Brightness", 0.0f);
  P.LFactor = cookParam(c, "BrightnessFactor", 0.0f);
  P.LOffset = cookParam(c, "BrightnessOffset", 0.0f);
  P.R = cookParam(c, "Red", 0.0f);
  P.RFactor = cookParam(c, "RedFactor", 0.0f);
  P.ROffset = cookParam(c, "RedOffset", 0.0f);
  P.G = cookParam(c, "Green", 0.0f);
  P.GFactor = cookParam(c, "GreenFactor", 0.0f);
  P.GOffset = cookParam(c, "GreenOffset", 0.0f);
  P.B = cookParam(c, "Blue", 0.0f);
  P.BFactor = cookParam(c, "BlueFactor", 0.0f);
  P.BOffset = cookParam(c, "BlueOffset", 0.0f);
  P.Mode = std::round(cookParam(c, "Mode", 0.0f));               // 0 Add, 1 Multiply
  P.TranslationSpace = std::round(cookParam(c, "TranslationSpace", 0.0f));  // 0 Object, 1 Point
  P.RotationSpace = std::round(cookParam(c, "RotationSpace", 1.0f));        // .t3 default 1 (Point)

  // Sampler (s0): Nearest + wrap from TextureMode (0 Wrap/1 Clamp/2 Mirror/3 Border; .t3 default Wrap).
  const int texMode = (int)std::lround(cookParam(c, "TextureMode", 0.0f));
  MTL::SamplerAddressMode addr;
  switch (texMode) {
    case 1:  addr = MTL::SamplerAddressModeClampToEdge;        break;
    case 2:  addr = MTL::SamplerAddressModeMirrorRepeat;       break;
    case 3:  addr = MTL::SamplerAddressModeClampToBorderColor; break;
    default: addr = MTL::SamplerAddressModeRepeat;             break;
  }
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(addr);
  sd->setTAddressMode(addr);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SAMPLEATTR_SourcePoints);
  enc->setBuffer(c.output, 0, SAMPLEATTR_ResultPoints);
  enc->setBytes(&P, sizeof(P), SAMPLEATTR_Params);
  enc->setTexture(const_cast<MTL::Texture*>(tex), SAMPLEATTR_InputTexture);
  enc->setSamplerState(samp, SAMPLEATTR_TexSampler);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  samp->release();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

}  // namespace

void registerSamplePointAttributesOp() {
  registerPointOp("SamplePointAttributes", cookSamplePointAttributes);
}

// ============================================================================================
// Golden — TWO direct-cook legs over a hand-built ctx + a UNIFORM texture (coordinate-independent so the
// routing is ANALYTIC).
//
//  (A) POSITION-route leg: a uniform texel value v=(0.5,0.5,0.5,1) -> gray=0.5. Route L (Brightness)=1
//      (For_X) with LFactor=1, LOffset=0 -> ff = Factors[1]*(gray*1+0) = (0.5,0,0,0). Mode=0 (Add),
//      TranslationSpace=0 (Object) -> offset=(0.5,0,0); RotationSpace=1 (Point, default) -> no rot2 rotate
//      (rot2=identity since no channel routes 5/6/7). newPos = pos + (0.5,0,0).
//      For pos.x=0.1 -> newPos.x = 0.6 (FIXED, analytic). injectBug drops the texture -> passthrough ->
//      newPos.x stays 0.1 -> RED.
//
//  (B) W-route leg: same uniform v, route Blue=4 (For_W) with BlueFactor=2, BlueOffset=0 -> ff.w =
//      Factors[4].w*(c.b*2+0) = 1*(0.5*2) = 1.0. Mode=0 (Add) -> p.W (== FX1) = FX1 + 1.0. Start FX1=0.25
//      -> want FX1=1.25 (FIXED). injectBug drops the texture -> passthrough -> FX1 stays 0.25 -> RED.
// ============================================================================================

namespace {

constexpr uint32_t kTexW = 8, kTexH = 8;

MTL::Texture* makeUniformTexF(MTL::Device* dev, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
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

// Run the op over a single hand-built point with the given param map + uniform-(128,128,128,255) texture.
// wireTexture=false (injectBug) drops the texture bind -> passthrough. Returns the cooked point in out.
bool routeLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
              std::map<std::string, float>& params, const SwPoint& in, SwPoint& out) {
  MTL::Buffer* srcBag = dev->newBuffer(&in, sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Texture* tex = makeUniformTexF(dev, 128, 128, 128, 255);  // ~ (0.5,0.5,0.5,1)

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {1};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = 1;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (wireTexture) { c.inputTextures[0] = tex; c.inputTextureCount = 1; }
  else { c.inputTextureCount = 0; }
  cookSamplePointAttributes(c);

  std::memcpy(&out, outBag->contents(), sizeof(SwPoint));
  srcBag->release(); outBag->release(); tex->release();
  return true;
}

}  // namespace

int runSamplePointAttributesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-samplepointattributes] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  // Uniform 128/255 ~= 0.50196 (RGBA8Unorm); use that exact value for analytic asserts.
  const float vv = 128.0f / 255.0f;

  auto baseParams = []() {
    std::map<std::string, float> p;
    p["Scale"] = 1.0f; p["Stretch.x"] = 1.0f; p["Stretch.y"] = 1.0f;
    p["TextureRotate.x"] = 0; p["TextureRotate.y"] = 0; p["TextureRotate.z"] = 0;
    p["Center.x"] = 0; p["Center.y"] = 0; p["Center.z"] = 0;
    p["TextureMode"] = 0; p["Mode"] = 0; p["TranslationSpace"] = 0; p["RotationSpace"] = 1;
    p["Brightness"] = 0; p["BrightnessFactor"] = 0; p["BrightnessOffset"] = 0;
    p["Red"] = 0; p["RedFactor"] = 0; p["RedOffset"] = 0;
    p["Green"] = 0; p["GreenFactor"] = 0; p["GreenOffset"] = 0;
    p["Blue"] = 0; p["BlueFactor"] = 0; p["BlueOffset"] = 0;
    return p;
  };

  // (A) Position-route: L=For_X(1), LFactor=1 -> offset.x = gray = vv. pos.x=0.1 -> newPos.x = 0.1+vv.
  std::map<std::string, float> pa = baseParams();
  pa["Brightness"] = 1.0f;  // For_X
  pa["BrightnessFactor"] = 1.0f;
  SwPoint inA{};
  inA.Position = SW_PACKED3{0.1f, 0.0f, 0.0f};
  inA.Rotation = SW_FLOAT4{0, 0, 0, 1};
  inA.Scale = SW_PACKED3{1, 1, 1};
  inA.FX1 = 0.0f;
  SwPoint outA{};
  routeLeg(dev, q, lib, /*wireTexture=*/!injectBug, pa, inA, outA);
  const float wantAX = 0.1f + vv;
  float errA = std::fabs(outA.Position.x - wantAX);
  bool passA = errA < 2e-3f;

  // (B) W-route: Blue=For_W(4), BlueFactor=2 -> ff.w = c.b*2 = vv*2. Mode=Add -> FX1 = 0.25 + vv*2.
  std::map<std::string, float> pb = baseParams();
  pb["Blue"] = 4.0f;  // For_W
  pb["BlueFactor"] = 2.0f;
  SwPoint inB{};
  inB.Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
  inB.Rotation = SW_FLOAT4{0, 0, 0, 1};
  inB.Scale = SW_PACKED3{1, 1, 1};
  inB.FX1 = 0.25f;
  SwPoint outB{};
  routeLeg(dev, q, lib, /*wireTexture=*/!injectBug, pb, inB, outB);
  const float wantBW = 0.25f + vv * 2.0f;
  float errB = std::fabs(outB.FX1 - wantBW);
  bool passB = errB < 2e-3f;

  bool pass = passA && passB;
  std::printf("[selftest-samplepointattributes] POS-route: newPosX=%.5f want=%.5f err=%.5f pass=%d | "
              "W-route: FX1=%.5f want=%.5f err=%.5f pass=%d | injectBug=%d -> %s\n",
              outA.Position.x, wantAX, errA, passA ? 1 : 0, outB.FX1, wantBW, errB, passB ? 1 : 0,
              injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
