// MandelbrotFractal: TiXL-ported Mandelbrot-set image generator, single fullscreen pass.
// VERBATIM port of external/tixl Operators/Lib/Assets/shaders/img/generate/MandelbrotFractal.hlsl
// (psMain + the mandelbrot() escape-time helper). Maps each pixel into complex-plane c, runs the
// smooth-iteration escape-time Mandelbrot, normalizes the iteration count, and samples a rasterized
// gradient ROW (bound at t0 as GradientImage) at (f, 0.5).
//
// Original GLSL by Inigo Quilez (https://www.shadertoy.com/view/4df3Rn), HLSL by TiXL.
//
// ============================== HLSL->MSL NOTES (named forks) ==============================
// [fork-pow10]  HLSL pow(10, Scale) -> MSL pow(10.0f, Scale). 10 is a constant base, not a per-pixel
//   varying, so float literal is bit-identical to the HLSL implicit int->float promotion of 10.
// [fork-samplelevel-grad]  HLSL GradientImage.SampleLevel(texSampler, float2(f,0.5), 0) ->
//   GradientImage.sample(texSampler, float2(f,0.5), level(0.0)). The gradient row is 1px tall, so the
//   v=0.5 coordinate plus ClampToEdge clamp picks the single row; LOD 0 pinned to match TiXL intent.
// [fork-loop-counts]  Cardioid/period-2-bulb early-out (M1/M2 skip) and the 512-iteration cap with
//   B=256 escape radius are ported VERBATIM — these are the parity-load-bearing constants.
// [fork-int-literal-B]  HLSL `const float B = 256;` (int literal assigned to float). MSL 256.0f.
#include <metal_stdlib>
#include "mandelbrotfractal_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as lineargradient_vs / rings_vs.
vertex VSOut mandelbrotfractal_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// mandelbrot() — verbatim MandelbrotFractal.hlsl lines 29-66 (smooth iteration count).
static inline float mandelbrot(float2 c) {
  // Skip computation inside M1 (cardioid) - iquilezles.org/articles/mset1bulb
  {
    float c2 = dot(c, c);
    if (256.0f * c2 * c2 - 96.0f * c2 + 32.0f * c.x - 3.0f < 0.0f) return 0.0f;
    // Skip computation inside M2 (period-2 bulb) - iquilezles.org/articles/mset2bulb
    if (16.0f * (c2 + 2.0f * c.x + 1.0f) - 1.0f < 0.0f) return 0.0f;
  }

  const float B = 256.0f;  // [fork-int-literal-B]
  float l = 0.0f;
  float2 z = float2(0.0f, 0.0f);
  for (int i = 0; i < 512; i++) {
    z = float2(z.x * z.x - z.y * z.y, 2.0f * z.x * z.y) + c;
    if (dot(z, z) > (B * B)) break;
    l += 1.0f;
  }

  if (l > 511.0f) return 0.0f;

  // Equivalent optimized smooth iteration count.
  float sl = l - log2(log2(dot(z, z))) + 4.0f;
  return sl;
}

// Mirror of MandelbrotFractal.hlsl psMain (lines 69-87), line for line.
fragment float4 mandelbrotfractal_fs(VSOut input                              [[stage_in]],
                                     texture2d<float> GradientImage           [[texture(0)]],
                                     sampler texSampler                       [[sampler(0)]],
                                     constant MandelbrotFractalParams& P      [[buffer(MANDELBROTFRACTAL_Params)]],
                                     constant MandelbrotFractalResolution& Res [[buffer(MANDELBROTFRACTAL_Resolution)]]) {
  float2 uv = input.texCoord;            // :71
  float2 p = uv;                          // :72

  p -= 0.5f;                              // :75
  p.y *= -1.0f;                           // :76
  p.x *= P.AspectRatio;                   // :77

  p /= pow(10.0f, P.Scale);               // :79 [fork-pow10]
  p += float2(P.OffsetX, P.OffsetY);      // :80

  float f = mandelbrot(p);                // :82

  f = f / P.ColorScale + P.ColorPhase;    // :84

  return GradientImage.sample(texSampler, float2(f, 0.5f), level(0.0f));  // :86 [fork-samplelevel-grad]
}
