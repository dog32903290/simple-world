// DrawScreenQuad: a textured fullscreen-ish quad (TiXL Operators/Lib/render/basic/DrawScreenQuad +
// Lib/Assets/shaders/img/analyze/vs-draw-viewport-quad.hlsl). A 6-vertex clip-space quad samples
// an input Texture2D, tinted by Color, sized by (Width,Height) and offset by Position. Ported 1:1
// from vs-draw-viewport-quad.hlsl (vsMain + psMain):
//
//   VS: quadVertex = Quad[id].xy;  pos = float4(quadVertex*(Width,Height) + Position, 0, 1);
//       texCoord = quadVertex*(0.5,-0.5) + 0.5;
//   PS: c = InputTexture.SampleLevel(s, uv, 0);  return clamp(float4(1)*Color*c, 0, (1000,1000,1000,1));
//
// CLEAN LEAF (named, matches TiXL): the HLSL's `mul(..., ObjectToClipSpace)` is COMMENTED OUT
// (vs-draw-viewport-quad.hlsl:51) — DrawScreenQuad has NO camera/Transforms dependency, it draws
// straight in clip space. DrawScreenQuadAdvanced (the camera/depth variant) is deferred. The
// 0..1000 RGB clamp (alpha capped at 1) is the verbatim TiXL HDR-permissive constant.
#include <metal_stdlib>
#include "draw_params.h"  // DrawScreenQuadParams, DRAWSQ_Params
using namespace metal;

// The 6 clip-space quad corners, verbatim from vs-draw-viewport-quad.hlsl's Quad[] (two tris:
// (-1,-1)(1,-1)(1,1) and (1,1)(-1,1)(-1,-1)). z=0; only xy used.
constant float2 kQuad[6] = {
  float2(-1, -1), float2( 1, -1), float2( 1,  1),
  float2( 1,  1), float2(-1,  1), float2(-1, -1),
};

struct ScreenQuadVSOut {
  float4 position [[position]];
  float2 texCoord;
};

vertex ScreenQuadVSOut draw_screenquad_vs(uint vid [[vertex_id]],
                                          constant DrawScreenQuadParams& P [[buffer(DRAWSQ_Params)]]) {
  ScreenQuadVSOut o;
  float2 quadVertex = kQuad[vid];
  float2 quadVertexInObject = quadVertex * float2(P.width, P.height);
  // ObjectToClipSpace mul is commented out in TiXL → draw straight in clip space (clean leaf).
  o.position = float4(quadVertexInObject + P.position, 0.0f, 1.0f);
  o.texCoord = quadVertex * float2(0.5f, -0.5f) + 0.5f;
  return o;
}

fragment float4 draw_screenquad_fs(ScreenQuadVSOut in [[stage_in]],
                                   constant DrawScreenQuadParams& P [[buffer(DRAWSQ_Params)]],
                                   texture2d<float> src [[texture(0)]],
                                   sampler texSampler [[sampler(0)]]) {
  float4 c = src.sample(texSampler, in.texCoord, level(0));
  // Upper clamp bound = P.clampMax (the cook path sets it to the TiXL constant (1000,1000,1000,1);
  // it lives in the cbuffer so the clamp golden exercises the real shader ceiling per-channel).
  return clamp(float4(1, 1, 1, 1) * P.color * c, float4(0), P.clampMax);
}
