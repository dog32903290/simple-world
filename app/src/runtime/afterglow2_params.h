// Shared host<->shader params for the TiXL-ported AfterGlow2 feedback fx (image/fx/feedback).
// AfterGlow2 is a near-sibling of AfterGlow: the SAME cross-frame trail (decay×blur×add) spine, but
// with a TWO-COLOR delta. The in-frame blur reuses the existing Blur shader (blur_vs/blur_fs +
// BlurParams). Only the cross-frame COMPOSITE needs a dedicated 2-input fragment, whose constants
// live here.
//
// TiXL authority spine (external/tixl Operators/Lib/image/fx/feedback/AfterGlow2.t3 — backward-trace):
//   The persistent trail buffer is RenderTarget(Clear=false) (.t3:88-127, node 3f277e17). Each frame
//   the Execute 167dd6c5 draws TWO additive layers into it (both BlendMode 1 = add):
//     * DrawScreenQuad 1cb10f4e (.t3:71-81): Texture = Blur(current) (.t3:293-297), Color = the
//       outer COLOR pin (.t3:287-291, default white (1,1,1,1)) -> buffer += Color.rgb * blur(current).
//     * Layer2d 130b531b (.t3:53-63): Texture = the RenderTarget's OWN previous output (.t3:268-273)
//       -> the trail re-adds itself. (Clear=false means the buffer already holds it; the explicit
//       re-add is the .t3 graph's idiom, collapsed away here — we accumulate in-place into writeTo.)
//   The DECAY is the SEPARATE Layer2d e1513904 (.t3:201-235): Color = Vector4(0,0,~0, DecayRate)
//     (the Vector4 node e7b3fe78 routes outer DecayRate -> W, Vector4.cs:28-29 W=6ce53000;
//     .t3:347-381), BlendMode 0 (normal/over) -> buffer *= (1 - DecayRate). IDENTICAL decay mapping
//     to AfterGlow (black/dark-over normal blend). DecayRate (.t3:27-28, 0.0157) is the per-frame
//     decay delta. So the trail fold is:  trail = trail*(1-DecayRate) + Color.rgb * blur(current).
//
//   ★THE AfterGlow2 DELTA (vs AfterGlow) — a FINAL two-color Blend (.t3:128-159, node 5e367fc1):
//     AfterGlow's Output IS the trail. AfterGlow2's Output is a Blend(BlendMode 1 = SCREEN; Blend.cs
//     ColorMode case 1, Blend.hlsl:115-118  rgb = tA.rgb + tB.rgb*tB.a):
//        ImageA (t0) = the trail RenderTarget,  ColorA = white default (.t3:133-141, NOT wired)
//        ImageB (t1) = the ORIGINAL current Image (.t3:329-333),  ColorB = the outer ORGCOLOR pin
//                      (.t3:317-321, default white (1,1,1,1))
//     => out.rgb = trail.rgb*1  +  (Image.rgb * OrgColor.rgb) * Image.a .
//     So COLOR tints the glow that ACCUMULATES into the trail (same role as AfterGlow's Color), and
//     ORGCOLOR tints the crisp ORIGINAL image re-composited on top of the trail each frame (the
//     second glow color — AfterGlow had no such final pass). Both colors are HONOURED.
//
//   Dropped vs AfterGlow (named): ContrastOffset2 (AfterGlow2 Blur.Offset is a static -0.04,
//   .t3:171-174 — no outer pin) and Resolution (the trail tracks the input dims, same as AfterGlow).
//   AfterGlow2 Blur Samples = 8 static (.t3:176-179); Size default BlurAmount = 2.0 (.t3:5-6).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct AfterGlow2Params {
  float Decay;       // (1 - DecayRate): per-frame survival multiplier of the trail (~0.984)
  float GlowR;       // Color.rgb tint on the blurred glow ADDED into the trail (outer Color pin)
  float GlowG;
  float GlowB;
  float OrgR;        // OrgColor.rgb tint on the ORIGINAL image screen-composited on top (OrgColor pin)
  float OrgG;
  float OrgB;
  float OrgA;        // OrgColor.w: scales the terminal contribution. Blend.hlsl:54 tB.a = clamp(orig.a)
                     // * clamp(OrgColor.a) — ImageBColor.a (=OrgColor.w) gates the screen-add (line 117
                     // rgb = tA.rgb + tB.rgb*tB.a). Default 1.0 (white OrgColor). 8 floats = 32 bytes.
};

enum AfterGlow2Binding {
  AFTERGLOW2_Params = 0,  // constant AfterGlow2Params& (b0)
  // texture(0) = previous trail, texture(1) = blurred current, texture(2) = original current image;
  // sampler(0) = linear clamp.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(AfterGlow2Params) == 32, "AfterGlow2Params 32 bytes (16-byte multiple)");
#endif
