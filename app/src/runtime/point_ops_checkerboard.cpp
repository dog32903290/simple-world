// CheckerBoard image generator op (Phase C leaf, C-1).
// TiXL authority: Operators/Lib/image/generate/basic/CheckerBoard.cs (ColorA/ColorB/Stretch/
// Scale/UseAspectRatio/Offset/Resolution/GenerateMips inputs) + CheckerBoard.t3 (defaults:
// ColorA~(0.202,0.202,0.202,1), ColorB~(0.121,0.121,0.121,1), Stretch=(1,1), Scale=1,
// UseAspectRatio=true, Offset=(0,0), Resolution=0=WindowFollow, GenerateMips=false) +
// Assets/shaders/img/generate/CheckerBoard.hlsl (single-pass UV checkerboard: floor/mod hard
// edges, lerp ColorA/ColorB).
//
// PORTABILITY HARDGATE (Cut-49/55/58 discipline):
//   - Zero texture inputs (pure generator). No source-op seam required: cookCheckerBoard runs
//     unconditionally (unlike filter ops that guard on !c.inputTexture). TiXL CheckerBoard.cs
//     has no Image [Input] slot — a generator, not a filter.
//   - FloatsToBuffer routing: STEP-0 backward-trace of CheckerBoard.t3 connections to slot
//     4ef6f204 shows ZERO math-node intermediates (no Multiply/IntToFloat). All 14 floats flow
//     directly: ColorA(Vec4Components X/Y/Z/W) -> ColorB(Vec4Components X/Y/Z/W) ->
//     Stretch(Vec2Components X/Y) -> BoolToFloat(UseAspectRatio) -> Scale(direct) ->
//     Offset(Vec2Components X/Y). cbuffer b0 = CheckerBoardParams (64 bytes); b1 =
//     CheckerBoardResolution (TargetWidth/TargetHeight for aspect ratio).
//   - No gradient / asset-texture / mip dependency.
//   - _ImageFxShaderSetupStatic class (Source="Lib:shaders/img/generate/CheckerBoard.hlsl").
//
// HLSL->MSL port: CheckerBoard.hlsl psMain ported verbatim to checkerboard.metal. The HLSL
// `#define mod(x,y) (x-y*floor(x/y))` with y=1 = fract(p) in Metal (named fork: mod-macro).
//
// NodeSpec: ColorA/ColorB as Vec4 components (.r/.g/.b/.a ports), Stretch as Vec2 (.x/.y),
// Offset as Vec2 (.x/.y), Scale/UseAspectRatio as Float. UseAspectRatio = Bool in TiXL (default
// true -> 1.0f host-side), passed to shader as float (BoolToFloat in the .t3).
// FORK (named): TiXL GenerateMips=false (CheckerBoard.t3 default). This op does NOT register
// in imageFilterMippedOutputTypes() — per-op-TYPE fork, matches TiXL default (false).
//
// Self-contained leaf: cookCheckerBoard + ImageFilterOp self-registration + runCheckerBoardSelfTest.
// CMake glob auto-picks this file (CMakeLists.txt CONFIGURE_DEPENDS point_ops_*.cpp).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/checkerboard_params.h"          // CheckerBoardParams, CheckerBoardResolution
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"     // ImageFilterOp self-registration
#include "runtime/point_graph.h"                  // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"                 // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar (below the anonymous namespace)
// can reference runCheckerBoardSelfTest, which is defined after it.
int runCheckerBoardSelfTest(bool injectBug);

namespace {

// CheckerBoard generator op: single fullscreen pass. No texture input; pure UV-based pattern.
// Runs unconditionally (unlike filter ops that guard on !c.inputTexture — generator has no Image slot).
void cookCheckerBoard(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  // NOTE: no c.inputTexture guard — CheckerBoard is a generator (zero Image input slots).
  // c.inputTexture will be null; this is correct and expected.

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "checkerboard_vs", "checkerboard_fs", fmt);
  if (!rps) return;

  // TiXL params: CheckerBoard.t3 defaults. cbuffer b0 field order verbatim from .hlsl.
  CheckerBoardParams p{};
  // ColorA (Vec4, TiXL default: X=0.20212764, Y=Z=0.20212561, W=1.0)
  p.ColorAR = cookParam(c, "ColorA.r", 0.20212764f);
  p.ColorAG = cookParam(c, "ColorA.g", 0.20212561f);
  p.ColorAB = cookParam(c, "ColorA.b", 0.20212561f);
  p.ColorAA = cookParam(c, "ColorA.a", 1.0f);
  // ColorB (Vec4, TiXL default: X=0.12056738, Y=Z=0.120566174, W=1.0)
  p.ColorBR = cookParam(c, "ColorB.r", 0.12056738f);
  p.ColorBG = cookParam(c, "ColorB.g", 0.120566174f);
  p.ColorBB = cookParam(c, "ColorB.b", 0.120566174f);
  p.ColorBA = cookParam(c, "ColorB.a", 1.0f);
  // Stretch (Vec2 -> "Size" in shader, TiXL default: (1,1))
  p.SizeX = cookParam(c, "Stretch.x", 1.0f);
  p.SizeY = cookParam(c, "Stretch.y", 1.0f);
  // UseAspectRatio (bool -> float; TiXL default true -> 1.0)
  p.UseAspectRatio = cookParam(c, "UseAspectRatio", 1.0f);
  // Scale (Single, TiXL default 1.0)
  p.Scale = cookParam(c, "Scale", 1.0f);
  // Offset (Vec2, TiXL default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);

  // Resolution cbuffer (b1): TargetWidth/TargetHeight for aspect ratio in the shader.
  CheckerBoardResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  // No texture/sampler bind (pure generator — no Image input).
  enc->setFragmentBytes(&p,   sizeof(CheckerBoardParams),     CHECKERBOARD_Params);
  enc->setFragmentBytes(&res, sizeof(CheckerBoardResolution), CHECKERBOARD_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // rps is cache-owned (tex_op_cache), not released here.
}

}  // namespace

// Self-registration. File-scope static ImageFilterOp feeds imageFilterSpecSink() + texReg()
// + imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_checkerboard{
    // CheckerBoard (TiXL Lib.image.generate.basic.CheckerBoard): UV-based two-color checkerboard
    // pattern generator. Zero Texture2D inputs — pure generator. Texture2D out.
    // Params mirror CheckerBoard.cs: ColorA(Vec4)/ColorB(Vec4)/Stretch(Vec2)/Scale/
    // UseAspectRatio/Offset(Vec2). Resolution pin = standard WindowFollow/HD/4K/Custom enum.
    // FORKS (named): TiXL GenerateMips=false default kept (no mip registration); mod macro
    // implemented as fract() in checkerboard.metal; no sampler (generator, no input tex).
    {"CheckerBoard", "CheckerBoard",
     {// No Image input — pure generator.
      {"out", "out", "Texture2D", false},
      // ColorA (Vec4, TiXL default ~(0.202,0.202,0.202,1) — dark grey A)
      {"ColorA.r", "ColorA", "Float", true, 0.20212764f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorA.g", "ColorA.g", "Float", true, 0.20212561f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.b", "ColorA.b", "Float", true, 0.20212561f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.a", "ColorA.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ColorB (Vec4, TiXL default ~(0.121,0.121,0.121,1) — dark grey B)
      {"ColorB.r", "ColorB", "Float", true, 0.12056738f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorB.g", "ColorB.g", "Float", true, 0.120566174f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.b", "ColorB.b", "Float", true, 0.120566174f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.a", "ColorB.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Stretch (Vec2, TiXL default (1,1)) -> "Size" in the shader
      {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 1},
      // Scale (Single, TiXL default 1.0)
      {"Scale", "Scale", "Float", true, 1.0f, 0.01f, 16.0f, Widget::Slider},
      // UseAspectRatio (Bool -> float param; TiXL default true -> 1.0f)
      {"UseAspectRatio", "UseAspectRatio", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
       {"No", "Yes"}},
      // Offset (Vec2, TiXL default (0,0))
      {"Offset.x", "Offset", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "CheckerBoard", cookCheckerBoard, "checkerboard", runCheckerBoardSelfTest};

// --- CheckerBoard MATH golden -----------------------------------------------------------------
// Ground truth from TiXL CheckerBoard.hlsl (hand-derived, independent of the port):
//
//   With default params: Stretch=(1,1), Scale=1.0, UseAspectRatio=true (so p-=0.5, p.x*=aspect),
//   Offset=(0,0). On a SQUARE texture (aspect=1.0) UseAspectRatio centering is applied but
//   aspect correction is a no-op.
//
//   texCoord -> p = texCoord. UseAspectRatio=true on square: p -= 0.5, p.x *= 1 (no-op).
//   p /= (1,1)*1.0. p += (0,0)*(-1,1) = p. a = fract(p).
//
//   Hand-derived cell-center pins (all texCoords via UV=(pixel+0.5)/W on 256x256, approx 0.25/0.75):
//
//   PIN1 at texCoord ~(0.25, 0.25): after centering p=(-0.25,-0.25), a=fract(-0.25,-0.25)=(0.75,0.75)
//     a.x=0.75>0.5, a.y=0.75>0.5 -> t=1 -> ColorB (blue).
//   PIN2 at texCoord ~(0.75, 0.25): p=(0.25,-0.25), a=(0.25,0.75)
//     a.x=0.25<0.5, a.y=0.75>0.5 -> t=0 -> ColorA (red).
//   PIN3 at texCoord ~(0.25, 0.75): p=(-0.25,0.25), a=(0.75,0.25)
//     a.x=0.75>0.5, a.y=0.25<0.5 -> t=0 -> ColorA (red).
//   PIN4 at texCoord ~(0.75, 0.75): p=(0.25,0.25), a=(0.25,0.25)
//     a.x=0.25<0.5, a.y=0.25<0.5 -> t=1 -> ColorB (blue).
//
//   Expected: PIN1=blue, PIN2=red, PIN3=red, PIN4=blue.
//
// injectBug: Offset.x=0.5. After Offset: p += (0.5,0)*(-1,1) = p + (-0.5, 0).
//   PIN1: p=(-0.25,-0.25)+(-0.5,0)=(-0.75,-0.25), a=fract(-0.75,-0.25)=(0.25,0.75).
//     a.x=0.25<0.5, a.y=0.75>0.5 -> t=0 -> ColorA (RED). Was blue -> FLIPPED.
//   Assertion "PIN1 blue (B>200, R<50)" FAILS under injectBug (teeth).
int runCheckerBoardSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  // Square texture: aspect=1.0 so UseAspectRatio has no effect (clean baseline).
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-checkerboard] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // ColorA=(1,0,0,1) red, ColorB=(0,0,1,1) blue. UseAspectRatio=true (1.0), Square -> no effect.
  std::map<std::string, float> params;
  params["ColorA.r"] = 1.0f; params["ColorA.g"] = 0.0f;
  params["ColorA.b"] = 0.0f; params["ColorA.a"] = 1.0f;
  params["ColorB.r"] = 0.0f; params["ColorB.g"] = 0.0f;
  params["ColorB.b"] = 1.0f; params["ColorB.a"] = 1.0f;
  params["Stretch.x"] = 1.0f; params["Stretch.y"] = 1.0f;
  params["Scale"] = 1.0f;
  params["UseAspectRatio"] = 1.0f;
  params["Offset.x"] = injectBug ? 0.5f : 0.0f;  // bug: Offset.x=0.5 shifts tiles -> PIN1 flips blue->red
  params["Offset.y"] = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // generator: no input
  c.output = dst;
  c.params = &params;
  cookCheckerBoard(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Sample the 4 cell centers. Each pin: read (R,B) from RGBA8.
  // texCoord maps linearly: pixel (x,y) -> uv = ((x+0.5)/W, (y+0.5)/H).
  // Pin at uv=(0.25,0.25) -> pixel (W*0.25 - 0.5) ~ pixel (63.5) -> pixel 63.
  // We use W/4 and H/4 as pixel coordinates for the cell centers (close enough for 256x256).
  uint32_t px1 = W / 4, py1 = H / 4;    // uv ~(0.25, 0.25) -> ColorB (blue)
  uint32_t px2 = 3 * W / 4, py2 = H / 4; // uv ~(0.75, 0.25) -> ColorA (red) [flips under bug]
  uint32_t px3 = W / 4, py3 = 3 * H / 4; // uv ~(0.25, 0.75) -> ColorA (red)
  uint32_t px4 = 3 * W / 4, py4 = 3 * H / 4; // uv ~(0.75, 0.75) -> ColorB (blue)

  auto readPixel = [&](uint32_t x, uint32_t y, int ch) -> int {
    return (int)out[((size_t)y * W + x) * 4 + ch];
  };

  // PIN1: top-left quad (0.25,0.25) -> ColorB -> blue (B>200, R<50).
  //   After UseAspectRatio centering on square: p=(-0.25,-0.25), a=fract=>(0.75,0.75) -> t=1 -> B.
  //   Under injectBug (Offset.x=0.5): p shifts to (-0.75,-0.25), a=(0.25,0.75) -> t=0 -> RED. FAILS.
  int p1R = readPixel(px1, py1, 0), p1B = readPixel(px1, py1, 2);
  // PIN2: top-right quad (0.75,0.25) -> ColorA -> red (R>200, B<50).
  //   p=(0.25,-0.25), a=(0.25,0.75) -> t=0 -> ColorA (red).
  int p2R = readPixel(px2, py2, 0), p2B = readPixel(px2, py2, 2);
  // PIN3: bottom-left quad (0.25,0.75) -> ColorA -> red (R>200, B<50).
  //   p=(-0.25,0.25), a=(0.75,0.25) -> t=0 -> ColorA (red).
  int p3R = readPixel(px3, py3, 0), p3B = readPixel(px3, py3, 2);
  // PIN4: bottom-right quad (0.75,0.75) -> ColorB -> blue (B>200, R<50).
  //   p=(0.25,0.25), a=(0.25,0.25) -> t=1 -> ColorB (blue).
  int p4R = readPixel(px4, py4, 0), p4B = readPixel(px4, py4, 2);

  // Assertions: PIN1=blue, PIN2=red, PIN3=red, PIN4=blue.
  bool pin1blue = (p1B > 200 && p1R < 50);  // FAILS under injectBug (Offset.x=0.5 -> PIN1 becomes red)
  bool pin2red  = (p2R > 200 && p2B < 50);
  bool pin3red  = (p3R > 200 && p3B < 50);
  bool pin4blue = (p4B > 200 && p4R < 50);

  bool pass = pin1blue && pin2red && pin3red && pin4blue;
  printf("[selftest-checkerboard] "
         "pin1(0.25,0.25) R=%d B=%d(want blue) "
         "pin2(0.75,0.25) R=%d B=%d(want red) "
         "pin3(0.25,0.75) R=%d B=%d(want red) "
         "pin4(0.75,0.75) R=%d B=%d(want blue) "
         "-> %s\n",
         p1R, p1B, p2R, p2B, p3R, p3B, p4R, p4B,
         pass ? "PASS" : "FAIL");

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
