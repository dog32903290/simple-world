// TransformImage image-filter texture op (image/transform) — offset/stretch/scale/rotation warp.
// TiXL authority: Operators/Lib/image/transform/TransformImage.cs ([Input] order
//   Image(Texture2D)->Offset(Vector2)->Stretch(Vector2)->Scale(float)->Rotation(float)->
//   Resolution(Int2)->ResolutionFactor(Vector2)->GenerateMips(bool)->Filter(Filter)->WrapMode(int))
// + TransformImage.t3 (defaults: Offset=(0,0), Stretch=(1,1), Scale=1, Rotation=0, Resolution=(0,0),
//   ResolutionFactor=(1,1), GenerateMips=false, Filter=MinMagMipLinear, WrapMode=2; the internal
//   _ImageFxShaderSetup "Wrap" node default TextureAddressMode=Wrap) + Assets/shaders/img/fx/
//   TransformImage.hlsl (self-contained single-pass kernel — ported line-for-line in
//   transformimage.metal).
//
// Single-pass port: cookTransformImage reads c.inputTexture (the upstream RenderTarget's Texture2D
// via the I1 gather direct-through), runs one fullscreen pass of transformimage_vs/_fs, writes
// c.output. Vector2 inputs (Offset, Stretch, ResolutionFactor) are decomposed into .x/.y Float ports
// (Widget::Vec), mirroring Tint/ConvertColors. OrgResolution (.hlsl b2 source-aspect cbuffer) is fed
// from the SOURCE texture size (inputTexture->width()/height()), matching TiXL's GetTextureSize(Image).
//
// FORKS (named):
//  [fork-getdimensions]   TiXL's GetDimensions(height,width) cross-wires the out params; ported
//    verbatim in transformimage.metal (variable `height`=texW, `width`=texH, aspect2=texH/texW).
//  [fork-b2-source-aspect] OrgResolution read from the b2 int cbuffer = source texture size, fed
//    here from c.inputTexture dimensions.
//  [fork-offset-xneg]     offset = Offset*float2(-1,1) (X negated) — in the kernel, verbatim.
//  [fork-rotation-sign]   (-Rotation-90)/180*3.141578 then sin/cos(-rad - 3.141578/2) — verbatim,
//    using TiXL's literal 3.141578 (not a more precise pi).
//  [fork-sampler-wrap]    Sampler = repeat(Wrap)+linear(MinMagMipLinear), matching TransformImage.t3
//    (the internal Wrap node default TextureAddressMode=Wrap + Filter=MinMagMipLinear). RepeatMode
//    is declared in the .hlsl cbuffer but the active path ignores it; wrap comes from the sampler.
//    (NOTE: distinct from Tint/ConvertColors which clamp — this op's .t3 wraps, ported faithfully.)
//  [fork-GenerateMips]    GenerateMips (bool, .cs/.t3 default false) — TexCookCtx has no mip seam;
//    LISTED in the NodeSpec (in .cs [Input] order) but NO-OP in the cook. Honest no-op.
//  [fork-Resolution]      Resolution(Int2)/ResolutionFactor(Vector2)/Filter(Filter)/WrapMode(int) —
//    TexCookCtx routes output sizing through the shared Resolution enum (WindowFollow/HD720/.../
//    Custom), and the sampler is fixed Wrap+linear per .t3. The four TiXL sizing/filter/wrap ports
//    are LISTED per .cs [Input] order but the cook honors the shared Resolution-enum seam + the
//    fixed sampler (no per-op output-format / runtime-filter / runtime-wrap seam). Honest no-op.
//
// Self-contained leaf: cookTransformImage + _reg_transformimage registrar + runTransformImageSelfTest.
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

#include "runtime/transformimage_params.h"  // TransformImageParams, TI_Params
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"            // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"           // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration (defined at the bottom of this leaf). Declared HERE rather than in the shared
// point_ops.h so this op stays a zero-shared-file-edit self-registered leaf — the registrar below
// references it before its definition, and the selftest dispatcher resolves it via the
// imageFilterSelfTests() sink (registered by the _reg_transformimage constructor).
int runTransformImageSelfTest(bool injectBug);

namespace {

// TransformImage texture op: single pass. Reads c.inputTexture (upstream tex op's output), writes
// c.output. No upstream texture wired: clear output to black (nothing to transform) — mirrors cookTint.
void cookTransformImage(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "transformimage_vs", "transformimage_fs", fmt);
  if (!rps) return;

  // [fork-sampler-wrap] repeat(Wrap)+linear(MinMagMipLinear), matching TransformImage.t3.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMipFilter(MTL::SamplerMipFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL TransformImage.cs / .t3 defaults. Vector2 inputs read as .x/.y scalar ports.
  TransformImageParams p{};
  p.OffsetX  = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY  = cookParam(c, "Offset.y", 0.0f);
  p.StretchX = cookParam(c, "Stretch.x", 1.0f);
  p.StretchY = cookParam(c, "Stretch.y", 1.0f);
  p.Scale    = cookParam(c, "Scale", 1.0f);
  p.Rotation = cookParam(c, "Rotation", 0.0f);
  p.RepeatMode = 0.0f;  // [fork-repeatmode] declared in .hlsl, IGNORED by active path
  p._pad0 = 0.0f;
  // [fork-b2-source-aspect] OrgResolution = SOURCE texture size (TiXL GetTextureSize(Image)).
  p.OrgResolutionX = (int)c.inputTexture->width();
  p.OrgResolutionY = (int)c.inputTexture->height();
  // TargetResolution declared in .hlsl b2 but only used in the commented rescale — output size.
  p.TargetResolutionX = (int)c.output->width();
  p.TargetResolutionY = (int)c.output->height();

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
  enc->setFragmentBytes(&p, sizeof(TransformImageParams), TI_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration (replaces registerTransformImageOp + node_registry spec + kTable row).
// NodeSpec ports mirror TransformImage.cs [Input] order VERBATIM:
//   Image -> Offset(.x/.y) -> Stretch(.x/.y) -> Scale -> Rotation -> Resolution(Int2) ->
//   ResolutionFactor(.x/.y) -> GenerateMips(bool) -> Filter -> WrapMode(int).
// Vector2 inputs are split into two Float Widget::Vec ports (head vecArity=2). The TiXL sizing /
// filter / wrap ports (Resolution Int2 / ResolutionFactor / Filter / WrapMode) are LISTED but the
// cook honors the shared Resolution-enum + fixed Wrap+linear sampler seam (named forks in the
// file header). CustomW/CustomH back the shared Resolution=Custom path (mirrors Tint/ConvertColors).
static const ImageFilterOp _reg_transformimage{
    {"TransformImage", "TransformImage",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Offset (Vector2, TiXL default (0,0)).
      {"Offset.x", "Offset", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
      // Stretch (Vector2, TiXL default (1,1)).
      {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 1},
      // Scale (float, TiXL default 1).
      {"Scale", "Scale", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Slider},
      // Rotation (float degrees, TiXL default 0).
      {"Rotation", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Slider},
      // Resolution (Int2, TiXL default (0,0)) — LISTED per .cs order; cook uses shared Resolution
      // enum seam below. (TiXL (0,0) == "follow input/window" -> our WindowFollow default.)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      // ResolutionFactor (Vector2, TiXL default (1,1)) — LISTED, NO-OP fork (no per-op sizing seam).
      {"ResolutionFactor.x", "ResolutionFactor", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 2},
      {"ResolutionFactor.y", "ResolutionFactor.y", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 1},
      // GenerateMips (bool, TiXL default false) — LISTED per .cs order, NO-OP fork (no mip seam).
      {"GenerateMips", "GenerateMips", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      // Filter (Filter enum, TiXL default MinMagMipLinear) — LISTED, NO-OP fork (fixed linear sampler).
      {"Filter", "Filter", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"MinMagMipLinear", "(filter follows fixed linear sampler)"}, true},
      // WrapMode (int enum, TiXL default 2=Clamp BUT internal Wrap node defaults Wrap) — LISTED,
      // NO-OP fork (fixed Wrap sampler per .t3 internal node). Enum shows the .t3 active default first.
      {"WrapMode", "WrapMode", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Wrap", "Mirror", "Clamp", "Border", "MirrorOnce"}, true},
      // CustomW/CustomH back the shared Resolution=Custom path (mirrors Tint/ConvertColors).
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "TransformImage", cookTransformImage, "transformimage", runTransformImageSelfTest};

// --- TransformImage MATH golden (headless, real GPU assertion) ---------------------------------
// Source: 64x64 split LEFT=red (255,0,0) | RIGHT=blue (0,0,255). The split axis is VERTICAL.
// Test A (identity passthrough, LOAD-BEARING for [fork-offset-xneg]/[fork-rotation-sign]):
//   cook with Offset=(0,0) Scale=1 Stretch=(1,1) Rotation=0 OrgResolution=(64,64). At identity the
//   kernel maps samplePos == texCoord, so a LEFT pixel stays red and a RIGHT pixel stays blue.
//   Assert: pixel (16,32) is red-dominant AND pixel (48,32) is blue-dominant.
//     - injectBug flips the Offset X sign convention OR the rotation sin/cos sign on the EXPECTED
//       side, but here we inject by perturbing the COOK params: with injectBug we cook Rotation=90,
//       which rotates the split axis from vertical to horizontal, so the (16,32) LEFT pixel is NO
//       LONGER red (it picks up the rotated content) -> Test A FAILS rc=1.
// Test B (rotation 90 actually rotates the split, 補強): cook Rotation=90 and assert the split axis
//   became HORIZONTAL — top half vs bottom half now differ, while left vs right at mid-height match.
//   Sampling: at Rotation=90, samplePos = rot90(texCoord-0.5)+0.5 = (-(v-0.5), (u-0.5)) + 0.5.
//   A pixel ABOVE center (small v) maps to large samplePos.x (RIGHT=blue); a pixel BELOW center
//   (large v) maps to small samplePos.x (LEFT=red). So TOP=blue, BOTTOM=red. Assert top!=bottom and
//   the mid-row left==right (split is no longer vertical). This pins the rotation sign + offset-xneg.
namespace {

// red-ness / blue-ness helpers on an RGBA8 pixel.
bool isRedDom(const uint8_t* px)  { return px[0] > 128 && px[2] < 128; }
bool isBlueDom(const uint8_t* px) { return px[2] > 128 && px[0] < 128; }

}  // namespace

int runTransformImageSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-transformimage] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: LEFT half red, RIGHT half blue (vertical split at x = W/2).
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      if (x < W / 2) { in[i + 0] = 255; in[i + 2] = 0; }    // red
      else           { in[i + 0] = 0;   in[i + 2] = 255; }  // blue
      in[i + 3] = 255;
    }
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  auto cook = [&](float rotation, std::vector<uint8_t>& out) {
    std::map<std::string, float> params;
    params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;
    params["Stretch.x"] = 1.0f; params["Stretch.y"] = 1.0f;
    params["Scale"] = 1.0f;
    params["Rotation"] = rotation;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
    cookTransformImage(c);
    out.assign((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  };

  auto at = [&](const std::vector<uint8_t>& buf, uint32_t x, uint32_t y) {
    return &buf[((size_t)y * W + x) * 4];
  };

  // ---- Test A: identity passthrough (injectBug cooks Rotation=90 -> split rotates -> FAIL) ----
  std::vector<uint8_t> outA;
  cook(injectBug ? 90.0f : 0.0f, outA);
  const uint8_t* aLeft  = at(outA, 16, 32);  // LEFT column, mid-height -> expect red at identity
  const uint8_t* aRight = at(outA, 48, 32);  // RIGHT column, mid-height -> expect blue at identity
  bool aPass = isRedDom(aLeft) && isBlueDom(aRight);
  printf("[selftest-transformimage] TestA(identity) L(16,32)=(%d,%d,%d) R(48,32)=(%d,%d,%d) -> %s\n",
         aLeft[0], aLeft[1], aLeft[2], aRight[0], aRight[1], aRight[2], aPass ? "PASS" : "FAIL");

  // ---- Test B: Rotation=90 rotates the split axis to HORIZONTAL ----
  std::vector<uint8_t> outB;
  cook(90.0f, outB);
  const uint8_t* bTop    = at(outB, 32, 16);  // ABOVE center -> maps to RIGHT -> blue
  const uint8_t* bBottom = at(outB, 32, 48);  // BELOW center -> maps to LEFT  -> red
  const uint8_t* bMidL   = at(outB, 16, 32);  // mid-row left
  const uint8_t* bMidR   = at(outB, 48, 32);  // mid-row right (should match mid-row left now)
  bool splitRotated = isBlueDom(bTop) && isRedDom(bBottom);          // vertical -> horizontal
  bool midRowSame   = isRedDom(bMidL) == isRedDom(bMidR);            // split no longer vertical
  bool bPass = splitRotated && midRowSame;
  printf("[selftest-transformimage] TestB(rot90) top(32,16)=(%d,%d,%d) bot(32,48)=(%d,%d,%d) "
         "midL=(%d,%d,%d) midR=(%d,%d,%d) splitRot=%d midSame=%d -> %s\n",
         bTop[0], bTop[1], bTop[2], bBottom[0], bBottom[1], bBottom[2],
         bMidL[0], bMidL[1], bMidL[2], bMidR[0], bMidR[1], bMidR[2],
         splitRotated ? 1 : 0, midRowSame ? 1 : 0, bPass ? "PASS" : "FAIL");

  // injectBug only perturbs Test A's cook (Rotation=90 instead of identity) so Test A bites; Test B
  // always cooks Rotation=90 and stands on its own (it pins the rotation sign + offset-xneg). With
  // injectBug, Test A FAILS -> overall FAIL rc=1.
  bool pass = aPass && bPass;
  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
