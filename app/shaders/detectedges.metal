// DetectEdges: TiXL-ported 4-neighbour absolute-difference edge detector, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/DetectEdges.hlsl psMain.
//
// Kernel (verbatim, HLSL lines 32-64):
//   sx = SampleRadius/width; sy = SampleRadius/height
//   y1 = Image.Sample(uv + (0, sy)); y2 = Image.Sample(uv - (0, sy))
//   x1 = Image.Sample(uv + (sx, 0)); x2 = Image.Sample(uv - (sx, 0)); m = Image.Sample(uv)
//   average = (sum over rgb of |x1-m|+|x2-m|+|y1-m|+|y2-m|) * Strength + Contrast
//   edgeColor = OutputAsTransparent<0.5
//                 ? clamp(float4(average,average,average,1),0,10000) * Color
//                 : float4(Color.rgb, clamp(average,0,1))
//   color2 = lerp(edgeColor, m, MixOriginal)
//   return Invert ? float4(1-color2.rgb, color2.a) : color2
//
// Forks (named, DX11->Metal):
//   - DX11 PS (VS+PS pipeline) -> Metal fullscreen-triangle VS+FS (same fork class as
//     Tint/ChannelMixer/Pixelate).
//   - HLSL Image.GetDimensions(width,height) -> Metal inputTex.get_width()/get_height()
//     (same TargetWidth/TargetHeight the HLSL's b1 Resolution cbuffer would have carried; we
//     read the bound texture's own size, so no Resolution cbuffer is needed).
//   - Invert (HLSL b2 ParamConstants{int Invert}): NOT a DetectEdges.cs input — the host never
//     wires it, so it is always 0. We hardcode the non-inverted branch (named fork
//     [verbatim-unwired-TiXL-field]); not exposed as a port.
//   - Fixed linear+clamp sampler (TiXL .t3 Wrap=MirrorOnce host knob not exposed; clamp matches
//     the edge-magnitude behaviour, same fork class as Sharpen).
#include <metal_stdlib>
#include "detectedges_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut detectedges_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of DetectEdges.hlsl psMain.
fragment float4 detectedges_fs(VSOut in [[stage_in]],
                               texture2d<float> Image     [[texture(0)]],
                               sampler texSampler         [[sampler(0)]],
                               constant DetectEdgesParams& P [[buffer(DETECTEDGES_Params)]]) {
  float width  = (float)Image.get_width();
  float height = (float)Image.get_height();

  float sx = P.SampleRadius / width;
  float sy = P.SampleRadius / height;

  float2 uv = in.texCoord;

  float4 y1 = Image.sample(texSampler, float2(uv.x,      uv.y + sy));
  float4 y2 = Image.sample(texSampler, float2(uv.x,      uv.y - sy));
  float4 x1 = Image.sample(texSampler, float2(uv.x + sx, uv.y));
  float4 x2 = Image.sample(texSampler, float2(uv.x - sx, uv.y));
  float4 m  = Image.sample(texSampler, float2(uv.x,      uv.y));

  float average = (abs(x1.r - m.r) + abs(x2.r - m.r) + abs(y1.r - m.r) + abs(y2.r - m.r) +
                   abs(x1.g - m.g) + abs(x2.g - m.g) + abs(y1.g - m.g) + abs(y2.g - m.g) +
                   abs(x1.b - m.b) + abs(x2.b - m.b) + abs(y1.b - m.b) + abs(y2.b - m.b)) *
                      P.Strength +
                  P.Contrast;

  float4 Color = float4(P.ColorR, P.ColorG, P.ColorB, P.ColorA);

  float4 edgeColor = P.OutputAsTransparent < 0.5f
                         ? clamp(float4(average, average, average, 1.0f), 0.0f, 10000.0f) * Color
                         : float4(Color.rgb, clamp(average, 0.0f, 1.0f));

  float4 color2 = mix(edgeColor, m, P.MixOriginal);

  // Invert is always 0 (HLSL b2 field never wired by DetectEdges.cs) -> non-inverted branch.
  return color2;
}
