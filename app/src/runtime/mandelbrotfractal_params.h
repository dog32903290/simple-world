// Shared host<->shader params for TiXL-ported MandelbrotFractal IMAGE GENERATOR (Phase C leaf).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/MandelbrotFractal.hlsl and
// MandelbrotFractal.cs/.t3.
//
// TiXL authority:
//   MandelbrotFractal.cs   — slot declarations: Phase, Scale, Offset(Vec2), ColorScale, Gradient.
//   MandelbrotFractal.t3   — defaults (Scale=-0.5, ColorScale=10.0, Phase=0.0, Offset=(0.251,0)),
//                            Gradient default = black->white 2-stop Linear; the op rides
//                            _ImageFxShaderSetupStatic (Wrap=Wrap, OutputFormat=R16G16B16A16_Float),
//                            the Gradient feeds GradientsToTexture -> GradientImage t0.
//   MandelbrotFractal.hlsl — cbuffer b0 ParamConstants (lines 3-10) + b1 Resolution (lines 12-16).
//
// ★Param routing trace (MandelbrotFractal.t3 Connections):
//   shader Offset      <- op Offset      (via Vector2Components unwrapper, .t3 default (0.251,0))
//   shader Scale       <- op Scale       (.t3 default -0.5)
//   shader ColorScale  <- op ColorScale  (.t3 default 10.0)
//   shader ColorPhase  <- op Phase       (.t3 default 0.0)   [.cs name "Phase" -> hlsl "ColorPhase"]
//   shader AspectRatio <- host-computed = TargetWidth / TargetHeight  (RequestedResolution child)
//   GradientImage t0   <- op Gradient    (via GradientsToTexture; .t3 default black->white)
//
// b0 ParamConstants layout (VERBATIM HLSL cbuffer field order, MandelbrotFractal.hlsl lines 3-10):
//   float2 Offset;       offset 0   (default (0.251, 0.0))
//   float  Scale;        offset 8   (default -0.5)
//   float  AspectRatio;  offset 12  (host-computed; fills register 0 [0-15])
//   float  ColorScale;   offset 16  (default 10.0)
//   float  ColorPhase;   offset 20  (<- Phase input; default 0.0; register 1 [16-31])
// Total: 6 floats x 4 = 24 bytes -> pad to 32 (16-byte multiple; ColorScale/ColorPhase in register 1).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct MandelbrotFractalParams {
  // b0 ParamConstants — field order MUST match MandelbrotFractal.hlsl cbuffer verbatim (lines 3-10).
  float OffsetX, OffsetY;  // float2 Offset,    t3 default (0.251, 0.0)
  float Scale;             // TiXL Scale (Single),      t3 default -0.5  (p /= pow(10, Scale))
  float AspectRatio;       // host-computed = TargetWidth / TargetHeight
  float ColorScale;        // TiXL ColorScale (Single), t3 default 10.0  (f / ColorScale)
  float ColorPhase;        // <- TiXL Phase (Single),   t3 default 0.0   (f + ColorPhase)
  float _pad[2];           // pad 24 -> 32 (16-byte multiple)
};

struct MandelbrotFractalResolution {
  // MandelbrotFractal.hlsl b1 Resolution cbuffer (TargetWidth/TargetHeight); host-filled from output.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16
};

enum MandelbrotFractalBinding {
  MANDELBROTFRACTAL_Params     = 0,  // constant MandelbrotFractalParams& (b0)
  MANDELBROTFRACTAL_Resolution = 1,  // constant MandelbrotFractalResolution& (b1)
  // texture(0) = GradientImage (rasterized 1xN gradient row), sampler(0) = linear+ClampToEdge.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(MandelbrotFractalParams) == 32, "MandelbrotFractalParams 32 bytes (16-byte multiple)");
static_assert(sizeof(MandelbrotFractalResolution) == 16, "MandelbrotFractalResolution 16 bytes");
#endif
