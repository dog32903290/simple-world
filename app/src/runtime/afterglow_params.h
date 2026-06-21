// Shared host<->shader params for the TiXL-ported AfterGlow feedback fx (image/fx/feedback).
// AfterGlow is a CROSS-FRAME feedback op composed with an IN-FRAME multi-pass blur. The blur step
// reuses the existing Blur shader (blur_vs/blur_fs + BlurParams); only the cross-frame COMPOSITE
// (decay*prev + glow) needs a dedicated 2-input fragment, whose constants live here.
//
// TiXL authority spine (external/tixl Operators/Lib/image/fx/feedback/AfterGlow.t3):
//   - The persistent trail buffer is a RenderTarget(Clear=false) (.t3:241-269, node 82e6547e).
//   - Each frame two things draw into it via the Camera->Execute 9be0c7e3:
//       * DrawQuad 4f2d6e89 — a BLACK quad (Color=Vector4(0,0,0, DecayRate); .t3:128-133+398-401)
//         drawn with BlendMode 0 (normal/over, DrawQuad.t3 default) -> buffer *= (1 - DecayRate).
//         THIS is the multiplicative trail decay; DecayRate (.t3:9-10, 0.0157) is the per-frame delta.
//       * Layer2d 0750217c — the blurred current image, BlendMode 1 (ADD), tinted by the outer Color
//         pin (.t3:13-20, ~0.59 grey; wired .t3:374-378) -> buffer += Color.rgb * blurred.
//   So the collapsed composite is:  out = prev * (1 - DecayRate)  +  Color.rgb * blurred .
//
// ★FORK / verdict divergence (NAMED, source-grounded): the work-order's stated decay verdict was
//   "decay = the Color pin (~0.59), NOT DecayRate". The .t3 backward-trace says the OPPOSITE: the
//   frame-decay multiplier is (1 - DecayRate) ≈ 0.984 (the DrawQuad black-over), and Color (~0.59)
//   tints the ADDED glow, not the surviving trail. We implement the SOURCE (Guards: 查 TiXL 不發明),
//   and the golden's decay-band assert is sized to (1 - DecayRate). See afterglow.metal / the leaf.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct AfterGlowParams {
  float Decay;     // (1 - DecayRate): per-frame survival multiplier of the existing trail (~0.984)
  float GlowR;     // Color.rgb tint on the ADDED blurred glow (outer Color pin, ~0.59 grey)
  float GlowG;
  float GlowB;
};

enum AfterGlowBinding {
  AFTERGLOW_Params = 0,  // constant AfterGlowParams& (b0)
  // texture(0) = previous trail, texture(1) = blurred current; sampler(0) = linear clamp.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(AfterGlowParams) == 16, "AfterGlowParams 16 bytes (16-byte multiple)");
#endif
