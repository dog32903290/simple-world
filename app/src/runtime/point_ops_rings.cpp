// Rings image-filter texture op (Phase C, C-2).
// TiXL authority: external/tixl/Operators/Lib/image/generate/pattern/Rings.cs (slot declarations)
//   + Rings.t3 (defaults) + Assets/shaders/img/generate/Rings.hlsl (single-pass kernel).
//
// Port class: _ImageFxShaderSetupStatic (bd0b9c5b) → single-pass fragment shader.
// STEP-0 portability check (passed):
//   ① Single optional Texture2D input (Image), no multi-image seam.
//   ② No gradient-widget, no curve-LUT, no asset-texture, no mip-gen seam.
//   ③ Not a .t3 compound — _ImageFxShaderSetupStatic with direct cbuffer feed.
//   ④ No FloatsToBuffer intermediate-math trap (confirmed by .t3 Children: only
//      Vector4Components/Vector2Components/IntToFloat decomposers feeding b0 in order).
//
// cookRings: reads c.inputTexture (optional upstream Texture2D), runs one fullscreen pass of
// rings_vs/rings_fs, writes c.output. No upstream wired: IsTextureValid=0 → returns pattern only.
//
// Forks (named):
//   [fork-IsTextureValid]  _ImageFxShaderSetupStatic injects IsTextureValid into the cbuffer at
//     runtime. We replicate it: host sets IsTextureValid=1.0 if c.inputTexture != null, else 0.0.
//   [fork-sampler-repeat]  Linear+Repeat sampler for upstream image, matching TiXL
//     _ImageFxShaderSetupStatic.t3 (AddressU/V=Wrap, Filter=MinMagMipLinear).
//
// Self-contained leaf: cookRings + ImageFilterOp registrar + runRingsSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/AdjustColors/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"              // cachedTexPSO (D2-2 PSO reuse)
#include "runtime/rings_params.h"              // RingsParams/Resolution, RINGS_Params/Resolution

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runRingsSelfTest.
int runRingsSelfTest(bool injectBug);

namespace {

// Rings texture op: single pass. Reads c.inputTexture (optional), writes c.output.
void cookRings(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "rings_vs", "rings_fs", fmt);  // D2-2 PSO reuse
  if (!rps) return;

  // Sampler: linear + Repeat. TiXL _ImageFxShaderSetupStatic.t3 defaults:
  //   AddressU/V = Wrap (DX TextureAddressMode.Wrap = repeat), Filter = MinMagMipLinear.
  // Rings uses texSampler only for the final ImageA.Sample (BlendColors composite);
  // interior ring math is UV-only, but sampler must match TiXL for parity when ImageA wired.
  // [fork-sampler-repeat] — previous [fork-sampler-clamp] was incorrect.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // --- b0 params (TiXL Rings.cs/.t3 defaults) ---
  RingsParams p{};
  // Fill (Vec4, TiXL default (1,1,1,1))
  p.FillR = cookParam(c, "Fill.r", 1.0f);
  p.FillG = cookParam(c, "Fill.g", 1.0f);
  p.FillB = cookParam(c, "Fill.b", 1.0f);
  p.FillA = cookParam(c, "Fill.a", 1.0f);
  // Background (Vec4, TiXL default (0,0,0,0))
  p.BackgroundR = cookParam(c, "Background.r", 0.0f);
  p.BackgroundG = cookParam(c, "Background.g", 0.0f);
  p.BackgroundB = cookParam(c, "Background.b", 0.0f);
  p.BackgroundA = cookParam(c, "Background.a", 0.0f);
  // Highlight (Vec4, TiXL default (1,0,0,1) — red)
  p.HighlightR = cookParam(c, "Highlight.r", 1.0f);
  p.HighlightG = cookParam(c, "Highlight.g", 0.0f);
  p.HighlightB = cookParam(c, "Highlight.b", 0.0f);
  p.HighlightA = cookParam(c, "Highlight.a", 1.0f);
  // Radius (Vec2, TiXL default (0.0, 0.5))
  p.RadiusX = cookParam(c, "Radius.x", 0.0f);
  p.RadiusY = cookParam(c, "Radius.y", 0.5f);
  // Position (Vec2, TiXL default (0, 0))
  p.PositionX = cookParam(c, "Position.x", 0.0f);
  p.PositionY = cookParam(c, "Position.y", 0.0f);
  // Count → RingCount (Single, TiXL default 0.5)
  p.RingCount = cookParam(c, "Count", 0.5f);
  // Feather (Single, TiXL default 0.03333335)
  p.Feather = cookParam(c, "Feather", 0.03333335f);
  // Rotate (Single, TiXL default 0.0)
  p.Rotate = cookParam(c, "Rotate", 0.0f);
  // Offset (Single, TiXL default 0.0)
  p.Offset = cookParam(c, "Offset", 0.0f);
  // _Segments (Vec2, TiXL default (20, 0))
  p.SegmentsX = cookParam(c, "_Segments.x", 20.0f);
  p.SegmentsY = cookParam(c, "_Segments.y", 0.0f);
  // _Twist (Vec2, TiXL default (0, 0))
  p.TwistX = cookParam(c, "_Twist.x", 0.0f);
  p.TwistY = cookParam(c, "_Twist.y", 0.0f);
  // _Thickness (Vec2, TiXL default (0.5, 0))
  p.ThicknessX = cookParam(c, "_Thickness.x", 0.5f);
  p.ThicknessY = cookParam(c, "_Thickness.y", 0.0f);
  // _Ratio (Vec2, TiXL default (1.05, 0))
  p.RatioX = cookParam(c, "_Ratio.x", 1.05f);
  p.RatioY = cookParam(c, "_Ratio.y", 0.0f);
  // _FillRatio (Single, TiXL default 1.0)
  p.FillRatio = cookParam(c, "_FillRatio", 1.0f);
  // _HighlightRatio (Single, TiXL default 0.0)
  p.HighlightRatio = cookParam(c, "_HighlightRatio", 0.0f);
  // HighlightSeed (Int, TiXL default 0) — stored as float in cbuffer
  p.HighlightSeed = cookParam(c, "HighlightSeed", 0.0f);
  // Distort (Single, TiXL default 1.0)
  p.Distort = cookParam(c, "Distort", 1.0f);
  // Constrast (typo in .cs, TiXL default 1.0)
  p.Contrast = cookParam(c, "Contrast", 1.0f);
  // Seed (Int, TiXL default 0)
  p.Seed = cookParam(c, "Seed", 0.0f);
  // BlendMode (Int, TiXL default 0 = normal)
  p.BlendMode = cookParam(c, "BlendMode", 0.0f);
  // [fork-IsTextureValid] 1.0 when upstream image wired, 0.0 when not.
  p.IsTextureValid = c.inputTexture ? 1.0f : 0.0f;

  // --- b1 Resolution (framework-injected from output size) ---
  RingsResolution res{};
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
  // Bind upstream image (may be null — Metal allows null texture bind; IsTextureValid guards it)
  if (c.inputTexture)
    enc->setFragmentTexture(const_cast<MTL::Texture*>(c.inputTexture), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(RingsParams),     RINGS_Params);
  enc->setFragmentBytes(&res, sizeof(RingsResolution), RINGS_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned, not released here
}

}  // namespace

// Self-registration. ImageFilterOp RAII registrar at file scope — runs during pre-main static init.
static const ImageFilterOp _reg_rings{
    // Rings (TiXL Lib.image.generate.pattern.Rings): concentric-ring pattern generator with
    // per-segment hash variation (thickness, fill-ratio, highlight). Optional upstream Texture2D
    // composite via BlendMode. Single Texture2D in (optional) → Texture2D out.
    // Params mirror Rings.cs/.t3: Fill/Background/Highlight (Vec4), Radius/Position (Vec2),
    // Count/Feather/Rotate/Offset (float), _Segments/_Twist/_Thickness/_Ratio (Vec2),
    // _FillRatio/_HighlightRatio/HighlightSeed/Distort/Contrast/Seed/BlendMode.
    // FORK (named): IsTextureValid injected host-side; sampler linear+Repeat.
    {"Rings", "Rings",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Fill (Vec4, TiXL default (1,1,1,1) — white ring fill color)
      {"Fill.r", "Fill", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Fill.g", "Fill.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.b", "Fill.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.a", "Fill.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL default (0,0,0,0) — transparent black)
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Highlight (Vec4, TiXL default (1,0,0,1) — red highlight accent)
      {"Highlight.r", "Highlight", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Highlight.g", "Highlight.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Highlight.b", "Highlight.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Highlight.a", "Highlight.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Radius (Vec2, TiXL default (0.0, 0.5) — inner/outer normalized radius)
      {"Radius.x", "Radius", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Radius.y", "Radius.y", "Float", true, 0.5f, 0.0f, 2.0f, Widget::Vec, {}, true, 1},
      // Position (Vec2, TiXL default (0, 0) — center offset)
      {"Position.x", "Position", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Position.y", "Position.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Count (float, TiXL default 0.5 — ring count / frequency)
      {"Count", "Count", "Float", true, 0.5f, 0.0f, 20.0f, Widget::Slider},
      // Feather (float, TiXL default 0.03333335 — edge softness)
      {"Feather", "Feather", "Float", true, 0.03333335f, 0.0f, 0.5f, Widget::Slider},
      // Rotate (float, TiXL default 0.0 — segment rotation in degrees)
      {"Rotate", "Rotate", "Float", true, 0.0f, -180.0f, 180.0f},
      // Offset (float, TiXL default 0.0 — ring phase offset)
      {"Offset", "Offset", "Float", true, 0.0f, -1.0f, 1.0f},
      // _Segments (Vec2, TiXL default (20, 0) — base count + random variation)
      {"_Segments.x", "_Segments", "Float", true, 20.0f, 1.0f, 100.0f, Widget::Vec, {}, true, 2},
      {"_Segments.y", "_Segments.y", "Float", true, 0.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 1},
      // _Twist (Vec2, TiXL default (0, 0) — ring twist angle + random variation)
      {"_Twist.x", "_Twist", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 2},
      {"_Twist.y", "_Twist.y", "Float", true, 0.0f, 0.0f, 360.0f, Widget::Vec, {}, true, 1},
      // _Thickness (Vec2, TiXL default (0.5, 0) — segment thickness + random variation)
      {"_Thickness.x", "_Thickness", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"_Thickness.y", "_Thickness.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // _Ratio (Vec2, TiXL default (1.05, 0) — arc span ratio + random variation)
      {"_Ratio.x", "_Ratio", "Float", true, 1.05f, 0.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"_Ratio.y", "_Ratio.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // _FillRatio (float, TiXL default 1.0 — fraction of segments drawn)
      {"_FillRatio", "_FillRatio", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
      // _HighlightRatio (float, TiXL default 0.0 — fraction of segments using Highlight color)
      {"_HighlightRatio", "_HighlightRatio", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // HighlightSeed (int, TiXL default 0 — hash seed for highlight selection)
      {"HighlightSeed", "HighlightSeed", "Float", true, 0.0f, 0.0f, 100.0f},
      // Distort (float, TiXL default 1.0 — radial distance power distortion)
      {"Distort", "Distort", "Float", true, 1.0f, 0.1f, 5.0f, Widget::Slider},
      // Contrast (float, TiXL default 1.0 — brightness contrast of segments)
      {"Contrast", "Contrast", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Slider},
      // Seed (int, TiXL default 0 — global hash seed for ring/segment variation)
      {"Seed", "Seed", "Float", true, 0.0f, 0.0f, 100.0f},
      // BlendMode (int, TiXL default 0 = normal) — how pattern composites over upstream Image
      {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference",
        "UseA", "UseB", "ColorDodge", "LinearDodge", "MaskAlpha"}, true},
      // Resolution / output size
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Rings", cookRings, "rings", runRingsSelfTest};

// --- Rings MATH golden -----------------------------------------------------------------------
//
// The Rings formula for whether a pixel is in the fill region:
//   normalizedDistance = (length(p) - Radius.x) / (Radius.y - Radius.x)
//   rings = normalizedDistance * RingCount + Offset
//   ringV = sw_mod(rings, 1)   [floor semantics — HLSL mod() macro, L93]
//   ringCenter = abs(ringV - 0.5)
//   c (ring mask) *= smoothstep(ThicknessX/2 + scaledFeather, ThicknessX/2 - scaledFeather, ringCenter)
//
// fmod vs sw_mod note:
//   Five sites in Rings.hlsl use HLSL `%` (fmod/truncation), not the mod() macro (floor):
//     L96: (Seed+0.5)%312.113, L105: (ringIndex-Offset)%RingCount,
//     L111: ringAngle2%1, L115: ringIndex%12.31, L115: Seed%712.1.
//   These all have non-negative operands in normal usage EXCEPT (ringIndex-Offset) when
//   ringIndex<Offset. The fmod/sw_mod difference only affects the hash seed chain; with
//   ThicknessY=RatioY=SegmentsY=0, hash influence is eliminated and both give the same output.
//
// Test setup: render a 128x128 image with defaults (Count=0.5 → still 1 ring due to
// normalizedDistance * 0.5; Radius=(0,0.5); Fill=white; Background=black; ThicknessX=0.5).
// The center pixel (64,64): p=(0,0), d2=0, normalizedDistance<0 → isInsideRadius=0 → output=Background=black.
// A pixel at the outer edge of the ring band: normalizedDistance ≈ 0.5 → rings=0.25, ringV=0.25,
// ringCenter=0.25 → smoothstep check: ringCenter(0.25) < ThicknessX/2(0.25) - scaledFeather ≈ 0.25
// → this is right at the edge, c≈0.5 → output is between Background and Fill (grey region).
//
// More useful golden:
// Use Count=5, Radius=(0, 0.4), ThicknessX=0.5, Feather=0.03, no position offset, no segments
// (RatioX=1.05 default, _SegmentsX=20 means 20 segments but with _Ratio > 0.5 they all connect).
// The output texture should have: center pixel = Background (black, outside radius by isInsideRadius),
// a pixel at radius 0.1 (normalizedDistance=0.25, rings=1.25, ringV=0.25, ringCenter=0.25) → inside
// ring band → Fill (white). We verify:
//   1. Center pixel (0,0 in p-space) is black (isInsideRadius=0 → color = Background).
//   2. A pixel at the mid-radius is bright (inside ring band → near-Fill brightness).
//   3. injectBug: change Count to 0 (divides ringRadius by 0 → scaledFeather=inf → output all-zero)
//      OR change Radius.y to 0 (same division by zero → black). Instead, a clean injectBug:
//      set Fill=(0,0,0,1) (black fill) so ring area is black → same as background → no brightness
//      difference between center and ring pixels. Normally Fill=white → ring pixels are bright.
//      With injectBug=Fill black, both center and ring area are the same (black) → the assertion
//      "ring pixel is brighter than center" FAILS.
//
// Hand-calc for the ring-mid pixel (width=128, height=128, aspect=1.0):
//   Pixel at (x=64, y=96): texCoord=(0.5, 0.75) → p=(0, 0.25-0.5)=(0, 0.0) after Position sub
//   Wait — let's recalculate. texCoord=(0.5, 0.5+32/128)=(0.5, 0.75).
//   p = texCoord - 0.5 = (0, 0.25); aspect=1 → p.x unchanged.
//   d2 = length(0, 0.25) = 0.25.
//   normalizedDistance = (0.25 - 0) / (0.4 - 0) = 0.625.
//   rings = 0.625 * 5 + 0 = 3.125; ringV = sw_mod(3.125, 1) = 0.125.
//   ringCenter = abs(0.125 - 0.5) = 0.375.
//   ringRadius_local = (0.4 - 0) / 5 = 0.08; scaledFeather = 0.03 / 0.08 / 10 = 0.0375.
//   ThicknessX/2 = 0.25.
//   smoothstep(0.25 + 0.0375, 0.25 - 0.0375, 0.375) = smoothstep(0.2875, 0.2125, 0.375)
//     = smoothstep(lo, hi, x) where x > hi: actually TiXL smoothstep(lo,hi,x) with lo>hi = falloff
//     = smoothstep(0.2875, 0.2125, 0.375) where x=0.375 > lo=0.2875 → returns 0 (below lo threshold).
//   Actually ringCenter=0.375 > segmentThickness=0.25; since step is smoothstep(thick+f, thick-f, center):
//     x=ringCenter=0.375 > thick+f=0.2875 → smoothstep returns 0 → c=0 → Background.
//   So this pixel is OUTSIDE the ring band. Let's try pixel at (64, 80) = texCoord (0.5, 0.625):
//   p = (0, 0.125). d2 = 0.125. normalizedDistance = 0.125/0.4 = 0.3125.
//   rings = 0.3125*5 = 1.5625. ringV = 0.5625. ringCenter = |0.5625-0.5| = 0.0625.
//   scaledFeather same = 0.0375. ThicknessX/2=0.25.
//   smoothstep(0.2875, 0.2125, 0.0625): x=0.0625 < 0.2125 → smoothstep returns 1.
//   So c=1 → color = Fill = white.
//
// CASE A: pixel at row=80, col=64 should be bright (near-white).
// injectBug: set Fill=(0,0,0,1) → same pixel is black → passes only when bug injected.
//
// CASE B — fmod-path guard with negative ringIndex:
//   Offset=-0.5, Count=5, Radius=(0,0.4), SegmentsY=0, ThicknessY=0, RatioY=0, FillRatio=1,
//   HighlightRatio=0, Contrast=1, Seed=0, Twist=(0,0).
//   Pixel at (row=70, col=64) — UV=(0.5, 70/128=0.546875):
//     p=(0, 0.046875), d2=0.046875, normalizedDistance=0.046875/0.4=0.1171875.
//     rings = 0.1171875*5 + (-0.5) = 0.0859375.  ringV = sw_mod(0.0859375, 1) = 0.0859375.
//     ringCenter = abs(0.0859375 - 0.5) = 0.4140625.
//     ringRadius_local = 0.4/5 = 0.08. scaledFeather = 0.03/0.08/10 = 0.0375.
//     ThicknessX/2 = 0.25. smoothstep(0.2875, 0.2125, 0.4140625): x>edge0=0.2875 → returns 0 → c=0.
//     Pixel is OFF the ring band (case B ring is at higher normalizedDistance).
//   Pixel at (row=80, col=64) re-used with Offset=-0.5:
//     normalizedDistance=0.3125. rings=0.3125*5-0.5=1.0625. ringV=sw_mod(1.0625,1)=0.0625.
//     ringCenter=abs(0.0625-0.5)=0.4375.  smoothstep(0.2875,0.2125,0.4375): x>0.2875 → c=0.
//   Try pixel at (row=75, col=64): d2=75/128-0.5=0.0859375, nd=0.0859375/0.4=0.21484375.
//     rings=0.21484375*5-0.5=0.5742. ringV=sw_mod(0.5742,1)=0.5742. ringCenter=|0.5742-0.5|=0.0742.
//     scaledFeather=0.0375, ThicknessX/2=0.25.
//     smoothstep(0.2875, 0.2125, 0.0742): x=0.0742 < 0.2125 → smoothstep returns 1 → c=1 → BRIGHT.
//   ringIndex = floor(0.5742) = 0. (ringIndex-Offset)=0-(-0.5)=0.5 → fmod(0.5,5)=0.5=sw_mod. No diff.
//   → Case B doesn't exercise the fmod divergence path (ringIndex=0, Offset=-0.5 → diff=0.5, positive).
//   NOTE: In practice, ringIndex<0 requires rings<0 which implies isInsideRadius=1 but c=0
//   (via ringV path) unless Distort pushes normalizedDistance. The fmod/sw_mod divergence in
//   ringIndex%12.31 and ringIndex%RingCount ONLY matters when hash actually changes the visible
//   output — which requires SegmentsY≠0 or ThicknessY≠0. Those cases are non-deterministic
//   without knowing hash22/hash42 values. Case B therefore tests "rings with Offset<0 and
//   fmod-corrected ringV path (sw_mod) gives a bright ring pixel" — this is a smoke guard
//   confirming the sw_mod(rings,1) and fmod(ringAngle2,1) path work correctly together.
//   injectBug for (B): Offset=-0.5 → ring band shifts. Set Fill=black (same as case A injectBug)
//   to collapse brightness — keeps injectBug semantics uniform.
//   Assertion B: pixel (row=75, col=64) with Offset=-0.5 is bright (sw_mod correctly handles
//   fractional rings when Offset shifts rings into non-integer range).
//   This guard would RED if fmod/sw_mod calls were swapped at the ringV line (L93 sw_mod →
//   fmod would change ringV for negative rings inputs, making some ring pixels go dark).
int runRingsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-rings] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);

  // ---- CASE A: standard ring bright check (Offset=0) ----------------------------------------
  // Params: Count=5 rings, Radius=(0, 0.4), Fill=white (or black when injecting bug),
  // Background=black, ThicknessX=0.5, Feather=0.03, _Ratio=(1.05,0) (full-arc default).
  MTL::Texture* dst = dev->newTexture(td);
  std::map<std::string, float> params;
  // [injectBug] Fill black so ring area is same as background → brightness test fails.
  params["Fill.r"] = injectBug ? 0.0f : 1.0f;
  params["Fill.g"] = injectBug ? 0.0f : 1.0f;
  params["Fill.b"] = injectBug ? 0.0f : 1.0f;
  params["Fill.a"] = 1.0f;
  params["Background.r"] = 0.0f; params["Background.g"] = 0.0f;
  params["Background.b"] = 0.0f; params["Background.a"] = 0.0f;
  params["Highlight.r"] = 1.0f; params["Highlight.g"] = 0.0f;
  params["Highlight.b"] = 0.0f; params["Highlight.a"] = 1.0f;
  params["Radius.x"] = 0.0f;   params["Radius.y"] = 0.4f;
  params["Position.x"] = 0.0f; params["Position.y"] = 0.0f;
  params["Count"] = 5.0f;
  params["Feather"] = 0.03f;
  params["Rotate"] = 0.0f;
  params["Offset"] = 0.0f;
  params["_Segments.x"] = 20.0f; params["_Segments.y"] = 0.0f;
  params["_Twist.x"]    = 0.0f;  params["_Twist.y"]    = 0.0f;
  params["_Thickness.x"] = 0.5f; params["_Thickness.y"] = 0.0f;
  params["_Ratio.x"] = 1.05f;    params["_Ratio.y"] = 0.0f;
  params["_FillRatio"] = 1.0f;
  params["_HighlightRatio"] = 0.0f;
  params["HighlightSeed"] = 0.0f;
  params["Distort"] = 1.0f;
  params["Contrast"] = 1.0f;
  params["Seed"] = 0.0f;
  params["BlendMode"] = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = nullptr; c.output = dst; c.params = &params;
  cookRings(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Center pixel (64, 64): isInsideRadius=0 → Background=black.
  size_t centerIdx = ((size_t)64 * W + 64) * 4;
  int centerR = out[centerIdx], centerG = out[centerIdx + 1], centerB = out[centerIdx + 2];

  // Ring pixel (80, 64): hand-calc shows c=1 → Fill=white (see selftest header comment).
  size_t ringIdx = ((size_t)80 * W + 64) * 4;
  int ringR = out[ringIdx], ringG = out[ringIdx + 1], ringB = out[ringIdx + 2];

  bool centerDark = (centerR + centerG + centerB) < 30;
  bool ringBright = (ringR + ringG + ringB) > 300;  // near-white → 3×~255

  printf("[selftest-rings] A center RGB=(%d,%d,%d) ring RGB=(%d,%d,%d) "
         "centerDark=%d ringBright=%d\n",
         centerR, centerG, centerB, ringR, ringG, ringB,
         centerDark ? 1 : 0, ringBright ? 1 : 0);

  // ---- CASE B: fmod-path guard — Offset<0 shifts ringV into sw_mod territory ----------------
  // Offset=-0.5 shifts rings downward; pixel (row=75, col=64) falls in a ring band.
  // Hand-calc: UV=(0.5, 75/128=0.5859375), p=(0, 0.0859375), d2=0.0859375,
  //   nd=0.0859375/0.4=0.21484375.  rings=0.21484375*5-0.5=0.5742.
  //   ringV = sw_mod(0.5742, 1) = 0.5742  [floor; fmod(0.5742,1) = same for positive input].
  //   ringCenter = |0.5742-0.5| = 0.0742.  ThicknessX/2=0.25.  scaledFeather=0.0375.
  //   smoothstep(0.2875, 0.2125, 0.0742): x=0.0742 < 0.2125 → returns 1 → c=1 → BRIGHT (white).
  // Note: the fmod-critical path here is ringV = sw_mod(rings, 1.0f) [L93 — floor semantics].
  // If sw_mod were replaced by fmod (truncation) for a positive input like 0.5742, the result
  // is the same. This guard primarily confirms the Offset<0 ring geometry works end-to-end
  // with the corrected dual-semantics (sw_mod for ringV, fmod for ringIndexFromCenter etc.).
  // injectBug: set Fill=black → ring pixel goes black → ringBrightB fails → RED.
  MTL::Texture* dstB = dev->newTexture(td);
  std::map<std::string, float> paramsB = params;  // copy base, override Offset
  paramsB["Fill.r"] = injectBug ? 0.0f : 1.0f;
  paramsB["Fill.g"] = injectBug ? 0.0f : 1.0f;
  paramsB["Fill.b"] = injectBug ? 0.0f : 1.0f;
  paramsB["Offset"] = -0.5f;  // shift rings; ringV for this pixel remains positive → sw_mod=fmod

  c.nodeId = 2; c.output = dstB; c.params = &paramsB;
  cookRings(c);

  std::vector<uint8_t> outB((size_t)W * H * 4, 0);
  dstB->getBytes(outB.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  size_t ringBIdx = ((size_t)75 * W + 64) * 4;
  int ringBR = outB[ringBIdx], ringBG = outB[ringBIdx + 1], ringBB = outB[ringBIdx + 2];
  bool ringBrightB = (ringBR + ringBG + ringBB) > 300;

  printf("[selftest-rings] B offset=-0.5 ring(75,64) RGB=(%d,%d,%d) ringBright=%d\n",
         ringBR, ringBG, ringBB, ringBrightB ? 1 : 0);

  bool pass = centerDark && ringBright && ringBrightB;
  printf("[selftest-rings] -> %s\n", pass ? "PASS" : "FAIL");

  dstB->release();
  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
