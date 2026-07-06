// TimeDisplace: per-pixel TIME displacement. Each pixel reaches a variable number of frames BACK into a
// persistent N-slice history ring (owned by the op, filled from the current Image each cook), chosen by
// the DisplaceMap's brightness. Moving regions smear across time.
//
// Authority (ported VERBATIM): external/tixl Operators/Lib/Assets/shaders/img/fx/TimeDisplace.hlsl:37-48.
//   HLSL Texture2DArray Image → MSL texture2d_array<float>; SampleLevel(uv,0) → sample(...,level(0)).
//   No matrix multiply in psMain, so the mul(m,v)→v*m rule does not apply here.
// See timedisplace_params.h for the param backward-trace (only psMain's DisplaceAmount/SliceIndex/
// ArrayLength are load-bearing; the .hlsl's Twist/Shade/DisplaceMode are unread by the entry point).
#include <metal_stdlib>
#include "timedisplace_params.h"   // TimeDisplaceParams, TIMEDISPLACE_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer), same idiom as blur_vs / afterglow_vs. texCoord
// 0..1, Y-flipped (NDC up vs texture down).
vertex VSOut timedisplace_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// psMain (.hlsl:37-48): DisplaceMap brightness → sliceOffset → slice = (SliceIndex + sliceOffset) % N →
// sample the ring at that slice. Sampler is POINT (the golden reads solid slices; a linear filter across
// distinct slices would blend — the .t3's _multiImageFxSetupStatic sets TextureFilter=MinMagMipPoint,
// .t3:135-138, so point-sampling IS the source).
fragment float4 timedisplace_fs(VSOut in [[stage_in]],
                                texture2d_array<float> ringTex [[texture(0)]],
                                texture2d<float> displaceMap   [[texture(1)]],
                                sampler samPoint               [[sampler(0)]],
                                constant TimeDisplaceParams& P [[buffer(TIMEDISPLACE_Params)]]) {
  float2 uv = in.texCoord;
  float4 rgba = displaceMap.sample(samPoint, uv, level(0));                       // .hlsl:43
  int sliceOffset = (int)floor(((rgba.r + rgba.g + rgba.b) / 3.0f) * P.DisplaceAmount + 0.5f);  // .hlsl:44
  int n = P.ArrayLength < 1 ? 1 : P.ArrayLength;
  int slice = ((P.SliceIndex + sliceOffset) % n + n) % n;                         // .hlsl:46 (+ true-mod)
  return ringTex.sample(samPoint, uv, (uint)slice, level(0));                     // .hlsl:47
}
