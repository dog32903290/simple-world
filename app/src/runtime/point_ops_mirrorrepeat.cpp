// MirrorRepeat image-filter texture op (image/transform) — mirror/rotate kaleidoscope fold.
// TiXL authority: Operators/Lib/image/transform/MirrorRepeat.cs ([Input] order
//   Image(Texture2D)->RotateMirror(float)->RotateImage(float)->Width(float)->Offset(float)->
//   OffsetEdge(float)->Offsetimage(Vector2)->ShadeAmount(float)->ShadeColor(Vector4)->Resolution(Int2))
// + MirrorRepeat.t3 (defaults: RotateMirror=0, RotateImage=0, Width=1.0, Offset=0, OffsetEdge=0,
//   Offsetimage=(0,0), ShadeAmount=0, ShadeColor=(1e-6,1e-6,1e-6,1.0), Resolution=(-1,-1);
//   _ImageFxShaderSetupStatic Wrap=Mirror, GenerateMips=false; default filter linear)
// + Assets/shaders/img/fx/MirrorRepeat.hlsl (self-contained single-pass fold kernel).
//
// Single-pass port: cookMirrorRepeat reads c.inputTexture (the upstream RenderTarget's Texture2D
// via the I1 gather direct-through), runs one fullscreen pass of mirrorrepeat_vs/mirrorrepeat_fs,
// writes c.output.
//
// FORKS (named):
//  [fork-cbuffer-order] The HLSL cbuffer ParamConstants order is NOT the .cs [Input] order; it puts
//    `float __dummy__` between OffsetImage and ShadeAmount as a 16-byte packing slot. MirrorRepeatParams
//    (mirrorrepeat_params.h) is laid out in CBUFFER order INCLUDING __dummy__ (dropping it shifts
//    ShadeColor/OffsetEdge = silent corruption). NodeSpec ports below follow .cs [Input] order; the
//    cook re-assembles the struct in cbuffer order. (護欄: ports=cs-order, struct=cbuffer-order.)
//  [fork-sampler] linear filter + MIRROR-REPEAT wrap = MirrorRepeat.t3's _ImageFxShaderSetupStatic
//    Wrap=Mirror (no Point filter set in this .t3, so filter stays linear). MTL::SamplerAddressModeMirrorRepeat.
//  [fork-Resolution] Resolution(Int2) is LISTED in the NodeSpec (.cs [Input] order) but the cook
//    drives the output size via the Resolution ENUM (same WindowFollow/HD720/.../Custom enum as
//    RenderTarget/Blur), not the raw Int2 — TexCookCtx has the texture pre-sized; TargetWidth/
//    TargetHeight are read from c.output dims (= the b1 ResolutionConstants in the HLSL).
//  [fork-pi] kernel uses TiXL's literal pi 3.141578 verbatim (see mirrorrepeat.metal).
//
// Self-contained leaf: cookMirrorRepeat + the _reg_mirrorrepeat registrar + runMirrorRepeatSelfTest.
// Shares the PSO+scratch cache seam (tex_op_cache.h) with Tint/Blur/Displace/ConvertColors.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/mirrorrepeat_params.h"        // MirrorRepeatParams, MR_Params
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runMirrorRepeatSelfTest(bool injectBug);  // fwd (defined below registrar)

namespace {

// MirrorRepeat texture op: single pass. Reads c.inputTexture (upstream tex op's output), writes
// c.output. No upstream texture wired: clear output to black — mirrors cookTint/cookConvertColors.
void cookMirrorRepeat(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "mirrorrepeat_vs", "mirrorrepeat_fs", fmt);
  if (!rps) return;

  // fork-sampler: linear filter + MIRROR-REPEAT wrap = MirrorRepeat.t3 Wrap=Mirror, default filter.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params (MirrorRepeat.cs / .t3 defaults). Ports are read by .cs name; the struct is filled
  // in CBUFFER order (the __dummy__ slot is left at its default 0 — it is a packing pad, not read).
  MirrorRepeatParams p{};
  p.RotateMirror = cookParam(c, "RotateMirror", 0.0f);
  p.RotateImage = cookParam(c, "RotateImage", 0.0f);
  p.Width = cookParam(c, "Width", 1.0f);
  p.Offset = cookParam(c, "Offset", 0.0f);
  p.OffsetImageX = cookParam(c, "Offsetimage.x", 0.0f);
  p.OffsetImageY = cookParam(c, "Offsetimage.y", 0.0f);
  p.__dummy__ = 0.0f;  // HLSL packing slot (fork-cbuffer-order); not a knob
  p.ShadeAmount = cookParam(c, "ShadeAmount", 0.0f);
  p.ShadeColorX = cookParam(c, "ShadeColor.x", 1e-6f);
  p.ShadeColorY = cookParam(c, "ShadeColor.y", 1e-6f);
  p.ShadeColorZ = cookParam(c, "ShadeColor.z", 1e-6f);
  p.ShadeColorW = cookParam(c, "ShadeColor.w", 1.0f);
  p.OffsetEdge = cookParam(c, "OffsetEdge", 0.0f);
  p.TargetWidth = (float)c.output->width();   // b1 ResolutionConstants (fork-Resolution)
  p.TargetHeight = (float)c.output->height();

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
  enc->setFragmentBytes(&p, sizeof(MirrorRepeatParams), MR_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. NodeSpec ports mirror MirrorRepeat.cs [Input] order verbatim:
// Image -> RotateMirror -> RotateImage -> Width -> Offset -> OffsetEdge -> Offsetimage(Vec2) ->
// ShadeAmount -> ShadeColor(Vec4) -> Resolution. (The HLSL cbuffer's different order + __dummy__
// pad live in MirrorRepeatParams, not here — fork-cbuffer-order.)
static const ImageFilterOp _reg_mirrorrepeat{
    {"MirrorRepeat", "MirrorRepeat",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"RotateMirror", "RotateMirror", "Float", true, 0.0f, -360.0f, 360.0f},
      {"RotateImage", "RotateImage", "Float", true, 0.0f, -360.0f, 360.0f},
      {"Width", "Width", "Float", true, 1.0f, 0.0f, 4.0f},
      {"Offset", "Offset", "Float", true, 0.0f, -2.0f, 2.0f},
      {"OffsetEdge", "OffsetEdge", "Float", true, 0.0f, -2.0f, 2.0f},
      // Offsetimage (Vector2, TiXL .cs) -> two Float Vec ports (.cs id "Offsetimage").
      {"Offsetimage.x", "Offsetimage", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Offsetimage.y", "Offsetimage.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ShadeAmount", "ShadeAmount", "Float", true, 0.0f, 0.0f, 1.0f},
      // ShadeColor (Vector4, TiXL .t3 default ~(0,0,0,1)) -> four Float ports.
      {"ShadeColor.x", "ShadeColor", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ShadeColor.y", "ShadeColor.y", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ShadeColor.z", "ShadeColor.z", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ShadeColor.w", "ShadeColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Resolution (Int2 .cs) -> the WindowFollow/HD/Custom enum (fork-Resolution).
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "MirrorRepeat", cookMirrorRepeat, "mirrorrepeat", runMirrorRepeatSelfTest};

// --- MirrorRepeat MATH golden (headless, real GPU assertion) ----------------------------------
// Setup: 64x64 source = a horizontal brightness gradient (col x -> value x/(W-1)), all other
// channels equal so brightness == x position. Cook with RotateMirror=0/RotateImage=0/Width=0.2/
// Offset=0/OffsetEdge=0/Offsetimage=0/ShadeAmount=0. With those params the fold is a HORIZONTAL
// mirror-fold about vertical lines: output column u and column (1-u) sample the SAME source column
// (proven by hand), so the output center scanline is left-right SYMMETRIC; and Width=0.2 < the dist
// half-range (0.5) so the reflection branch (the sign-bearing `d`) actually fires (refuteFocus).
//
// Test A (symmetry, geometry+alignment guard): read back the center row of the output; assert
//   out[x] == out[W-1-x] within ±3. This bites the [fork-cbuffer-order] alignment: if __dummy__
//   were dropped, ShadeColor/OffsetEdge shift and OffsetEdge would read garbage -> an asymmetric
//   horizontal shift breaks the mirror symmetry.
// Test B (forward fold sign, LOAD-BEARING — refuteFocus): a CPU model of the fold computes, for
//   each output column, the source column it samples; expected brightness = that source column's
//   gradient value. Assert GPU center row == CPU-modeled row within ±4. injectBug FLIPS the fold
//   sign in the CPU model (d = +2*(mDist-Width) instead of -2*(mDist-Width)) so the (correct) GPU
//   row no longer matches the (now-wrong) CPU expectation -> Test B FAILS rc=1. (We inject on the
//   CPU side because the shader is compiled, same pattern as ConvertColors Test A.)
namespace {

// CPU mirror of mirrorrepeat.metal's fold, for RotateMirror=RotateImage=Offset=OffsetEdge=0,
// Offsetimage=0, aspect=1, imageAspect=1. Returns the sampled source u' in [0,1] (pre mirror-wrap)
// for an output coordinate u in [0,1]. injectBug flips the fold-`d` sign.
float cpuFoldU(float u, float Width, bool injectBug) {
  const float PI = 3.141578f;
  // rotateScreenRad = -90/180*PI = -PI/2; sina=sin(PI/2-PI/2)=0, cosa=cos(0)=1 -> p unrotated.
  float px = (u - 0.5f);  // p.x after *aspect(1); p.y irrelevant (angle.y=0)
  // mirrorRotationRad = -90/180*PI = -PI/2; angle = (sin(-PI/2),cos(-PI/2)) = (-1, 0).
  float angleX = std::sin((-90.0f) / 180.0f * PI);  // == -1
  float dist = px * angleX;       // dot(p, angle) = px*(-1)
  float offset = std::fmod(0.0f, 2.0f);  // Offset=0
  dist += offset;
  float d = 0.0f;
  float mDist = std::fmod(dist, 2.0f * Width);
  float foldSign = injectBug ? +2.0f : -2.0f;  // BUG flips the reflection sign
  if (dist > Width) {
    if (mDist > Width) { d = foldSign * (mDist - Width); }
  } else if (dist < 0.0f) {
    mDist *= -1.0f;
    if (mDist < Width) { /* shade only */ }
    else { d = foldSign * (mDist - Width); }
  }
  d -= dist - mDist;
  d += offset;  // +OffsetEdge(0)
  float pxn = px + d * angleX;    // p += d*angle
  // p.x /= aspect(1); p *= aspect/imageAspect (=1); p += 0.5; +Offsetimage(0)
  float up = pxn + 0.5f;
  return up;
}

// Mirror-repeat wrap of a coordinate into [0,1] (matches the GPU sampler's mirror addressing),
// then read the horizontal-gradient source value at that column.
float mirrorWrap01(float v) {
  float t = std::fmod(std::fabs(v), 2.0f);
  return t > 1.0f ? 2.0f - t : t;
}

}  // namespace

int runMirrorRepeatSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-mirrorrepeat] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: horizontal brightness gradient. Column x -> value round(x/(W-1)*255) on R=G=B, A=255.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      uint8_t v = (uint8_t)std::lround((float)x / (float)(W - 1) * 255.0f);
      size_t i = ((size_t)y * W + x) * 4;
      in[i + 0] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Width=0.2 (NOT 0.5): with dist ∈ [-0.5,0.5] this makes the fold reflection branch
  // (d = -2*(mDist-Width)) actually FIRE for columns where dist>Width — that branch is the
  // refuteFocus, and a too-large Width (e.g. 0.5) would keep dist within [-Width,Width] so the
  // sign-bearing `d` stays 0 and injectBug couldn't bite. 0.2 exercises the real fold.
  const float Width = 0.2f;
  std::map<std::string, float> params;
  params["RotateMirror"] = 0.0f;
  params["RotateImage"] = 0.0f;
  params["Width"] = Width;
  params["Offset"] = 0.0f;
  params["OffsetEdge"] = 0.0f;
  params["Offsetimage.x"] = 0.0f;
  params["Offsetimage.y"] = 0.0f;
  params["ShadeAmount"] = 0.0f;
  params["ShadeColor.x"] = 0.0f;
  params["ShadeColor.y"] = 0.0f;
  params["ShadeColor.z"] = 0.0f;
  params["ShadeColor.w"] = 1.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookMirrorRepeat(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  uint32_t midY = H / 2;
  auto pix = [&](uint32_t x) -> int {
    return out[((size_t)midY * W + x) * 4 + 0];
  };

  // ---- Test A: center-row left-right symmetry (geometry + cbuffer alignment) ----
  int aMaxErr = 0;
  for (uint32_t x = 0; x < W; ++x) {
    int e = std::abs(pix(x) - pix(W - 1 - x));
    aMaxErr = std::max(aMaxErr, e);
  }
  bool aPass = aMaxErr <= 3;
  printf("[selftest-mirrorrepeat] TestA(symmetry) maxErr=%d -> %s\n", aMaxErr,
         aPass ? "PASS" : "FAIL");

  // ---- Test B: forward fold sign vs CPU model (injectBug flips CPU sign) ----
  int bMaxErr = 0;
  uint32_t bWorst = 0;
  for (uint32_t x = 0; x < W; ++x) {
    float u = ((float)x + 0.5f) / (float)W;  // pixel-center sample coordinate
    float up = cpuFoldU(u, Width, injectBug);
    float wrapped = mirrorWrap01(up);
    // gradient value at the wrapped source column (linear over [0,1] -> 0..255).
    int expect = (int)std::lround(wrapped * 255.0f);
    int got = pix(x);
    int e = std::abs(got - expect);
    if (e > bMaxErr) { bMaxErr = e; bWorst = x; }
  }
  bool bPass = bMaxErr <= 6;  // ±6: nearest-vs-linear sampler edge slop on a 64px gradient
  printf("[selftest-mirrorrepeat] TestB(foldsign%s) maxErr=%d @x=%u -> %s\n",
         injectBug ? ",BUG" : "", bMaxErr, bWorst, bPass ? "PASS" : "FAIL");

  bool pass = aPass && bPass;
  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
