// DisplacePoints2d — a texture-into-points seam consumer (PointCookCtx::inputTextures[0]). Faithful port
// of external/tixl .../Assets/shaders/points/modify/DisplacePoints2d.hlsl, driven by the TiXL op
// SimDisplacePoints2d.{cs,t3} (a single-pass in-place MODIFIER despite the "Sim" name — its .t3 is one
// ComputeShader+Dispatch, NO feedback/cross-frame state). A count-preserving MODIFIER: each point samples
// a DisplaceMap, takes the central-difference GRADIENT of the gray map (±SampleRadius), and shifts
// Position.xy along the gradient angle by DisplaceAmount/100. See displacepoints2d_params.h / .metal.
//
// .t3 DEFAULT AUDIT (external/tixl .../point/sim/SimDisplacePoints2d.t3):
//   DisplaceAmount=0 | DisplaceOffset=0 (DEAD in kernel) | Twist=0 | SampleRadius=0 | Center=0 |
//   TextureScale=(1,1) | TextureRotate=0 | TextureMode=Wrap | Texture=null.
//   At ALL defaults the op is a NO-OP twice over: DisplaceAmount=0 (zero shift) AND SampleRadius=0 (all 4
//   gradient samples collapse to the same uv -> d=0 -> len=0 -> the `len>0.0001` branch never fires). So
//   the golden drives DisplaceAmount>0 AND SampleRadius>0 to exercise the displacement.
//   fork-worldtoobject-op-local: the .t3 pushes an OP-LOCAL Transform (Center/TextureRotate/TextureScale)
//   as ObjectToWorld; we compose its inverse host-side (NO camera input exists on the op).
//   fork-displaceoffset-dead: DisplaceOffset is read by the .cs but never used in the .hlsl (not passed).
#include "runtime/point_ops.h"
#include "runtime/tex_op_cache.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"
#include "runtime/displacepoints2d_params.h"  // DisplaceParams2d, DISP2D_* bindings
#include "runtime/graph.h"
#include "runtime/point_graph.h"
#include "runtime/tixl_point.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookDisplacePoints2d(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  const MTL::Texture* tex = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;
  if (!tex) {
    std::memcpy(c.output->contents(), const_cast<MTL::Buffer*>(srcBag)->contents(),
                (size_t)c.count * sizeof(SwPoint));
    return;
  }

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "displacepoints2d");
  if (!pso) return;

  DisplaceParams2d P{};
  P.Count = c.count;
  float center[3] = {0, 0, 0};
  cookVecN(c, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  // Op-local transform: Scale3 = (TextureScale.x·Aspect, TextureScale.y, 1); Aspect = mapW/mapH (no .t3
  // scale-uniform on this op — TextureScale is the only scale). The shader applies the INVERSE.
  float texScale[2] = {1.0f, 1.0f};
  cookVecN(c, "TextureScale", texScale, 2, texScale);
  const float mapW = (float)tex->width(), mapH = (float)tex->height();
  const float aspect = (mapH != 0.0f) ? (mapW / mapH) : 1.0f;
  P.ScaleX = texScale[0] * aspect;
  P.ScaleY = texScale[1];
  P.ScaleZ = 1.0f;
  float rot[3] = {0, 0, 0};
  cookVecN(c, "TextureRotate", rot, 3, rot);
  P.RotX = rot[0]; P.RotY = rot[1]; P.RotZ = rot[2];

  P.DisplaceAmount = cookParam(c, "DisplaceAmount", 0.0f);
  P.Twist = cookParam(c, "Twist", 0.0f);
  P.SampleRadius = cookParam(c, "SampleRadius", 0.0f);

  // Sampler: LINEAR (TiXL reads a gradient map — the .t3 SamplerState default filter is linear) + wrap.
  const int texMode = (int)std::lround(cookParam(c, "TextureMode", 0.0f));
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
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, DISP2D_SourcePoints);
  enc->setBuffer(c.output, 0, DISP2D_ResultPoints);
  enc->setBytes(&P, sizeof(P), DISP2D_Params);
  enc->setTexture(const_cast<MTL::Texture*>(tex), DISP2D_DisplaceMap);
  enc->setSamplerState(samp, DISP2D_TexSampler);
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

void registerDisplacePoints2dOp() {
  registerPointOp("DisplacePoints2d", cookDisplacePoints2d);
}

// ============================================================================================
// Golden — a DIAGONAL-GRADIENT ramp DisplaceMap makes the displacement ANALYTIC (and avoids the atan2
// branch-cut singularity at d.y==0 that a pure-axis ramp would hit — under Metal fast-math atan2(+,-0)
// is unstable, so we tilt the gradient OFF the axis where atan2 is unambiguous on every platform).
//
//   Ramp: a 256×256 square texture where gray = 0.5·u + 0.5·v (a constant DIAGONAL gradient, equal slope
//   in U and V). Square -> Aspect=1 -> the op-local transform is identity at default params. For a point
//   at pos=(0,0,0) -> uv=(0.5,0.5) (ramp center, away from wrap seams):
//     x1 = gray(u+sx) ; x2 = gray(u-sx) -> d.x = 0.5·2sx = sx (>0)
//     y1 = gray(v+sy) ; y2 = gray(v-sy) -> d.y_raw = 0.5·2sy = sy ; d.y *= -1 -> -sy
//     square -> sx==sy==s ; d = (s, -s) ; len = √2·s ; a = atan2(s, -s) = 135°
//     direction = (sin 135°, cos 135°) = (+√2/2, -√2/2)   (stable: atan2(+,-) away from any cut)
//     shift = direction · DisplaceAmount/100
//   With DisplaceAmount=100 -> shift = (+0.70711, -0.70711). pos=(0,0,0) -> newPos = (0.70711, -0.70711, 0).
//   FIXED, analytic. (The 135° angle is the FAITHFUL TiXL formula's output — atan2(d.x,d.y) with d.y flipped.)
//
//   SampleRadius = 1 px -> s = 1/256 -> len = √2/256 ≈ 0.0055 > 0.0001 (the branch fires).
//   injectBug drops the texture -> passthrough -> newPos stays (0,0,0) -> RED.
// ============================================================================================

namespace {

// A 256×256 RGBA8 DIAGONAL ramp: gray(x,y) = 0.5·(x/255) + 0.5·(y/255). Row 0 = top (v small). Linear-
// samples to gray ≈ 0.5u + 0.5v. Equal slope in U and V -> a clean 45° gradient (atan2 off the axis).
MTL::Texture* makeRampTex(MTL::Device* dev) {
  const uint32_t W = 256, H = 256;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  std::vector<uint8_t> px((size_t)W * H * 4);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      float gf = 0.5f * (x / 255.0f) + 0.5f * (y / 255.0f);
      uint8_t g = (uint8_t)std::lround(gf * 255.0f);
      px[i + 0] = g; px[i + 1] = g; px[i + 2] = g; px[i + 3] = 255;
    }
  t->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, px.data(), W * 4);
  return t;
}

bool dispLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture, SwPoint& out) {
  SwPoint in{};
  in.Position = SW_PACKED3{0.0f, 0.0f, 0.0f};  // -> uv center (0.5,0.5), away from ramp wrap seams
  in.Rotation = SW_FLOAT4{0, 0, 0, 1};
  in.Scale = SW_PACKED3{1, 1, 1};

  MTL::Buffer* srcBag = dev->newBuffer(&in, sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Texture* tex = makeRampTex(dev);

  std::map<std::string, float> params;
  params["Center.x"] = 0; params["Center.y"] = 0; params["Center.z"] = 0;
  params["TextureScale.x"] = 1.0f; params["TextureScale.y"] = 1.0f;
  params["TextureRotate.x"] = 0; params["TextureRotate.y"] = 0; params["TextureRotate.z"] = 0;
  params["TextureMode"] = 1.0f;  // Clamp (avoid wrap artifacts at the ramp ends for the ±sx probe)
  params["DisplaceAmount"] = 100.0f;  // -> shift magnitude 1.0
  params["Twist"] = 0.0f;
  params["SampleRadius"] = 1.0f;      // sx = 1/256

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {1};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = 1;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (wireTexture) { c.inputTextures[0] = tex; c.inputTextureCount = 1; }
  else { c.inputTextureCount = 0; }
  cookDisplacePoints2d(c);

  std::memcpy(&out, outBag->contents(), sizeof(SwPoint));
  srcBag->release(); outBag->release(); tex->release();
  return true;
}

}  // namespace

int runDisplacePoints2dSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-displacepoints2d] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  SwPoint out{};
  dispLeg(dev, q, lib, /*wireTexture=*/!injectBug, out);
  // Diagonal ramp -> a=135° -> direction=(+√2/2,-√2/2), DisplaceAmount=100 -> shift=(0.70711,-0.70711).
  const float kRoot2Half = 0.70710678f;
  const float wantX = kRoot2Half, wantY = -kRoot2Half;
  float errX = std::fabs(out.Position.x - wantX);
  float errY = std::fabs(out.Position.y - wantY);
  bool pass = (errX < 8e-3f) && (errY < 8e-3f);
  std::printf("[selftest-displacepoints2d] newPos=(%.5f,%.5f,%.5f) want=(%.5f,%.5f,*) errX=%.5f errY=%.5f "
              "injectBug=%d -> %s\n",
              out.Position.x, out.Position.y, out.Position.z, wantX, wantY, errX, errY,
              injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
