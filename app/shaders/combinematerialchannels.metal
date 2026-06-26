// CombineMaterialChannels: TiXL-ported PBR channel-pack image filter (lane multi-image, image/use).
// Faithful 1:1 port of
//   external/tixl Operators/Lib/Assets/shaders/img/use/CombineMaterialChannels.hlsl (Thomas Helzle 2022).
//
// Three FIXED material maps (Roughness @ t0, Metallic @ t1, Occlusion @ t2) are packed into one image:
//   roughness = isRoughnessConnected ? pow(Roughness.r, 1) : 0.5
//   metallic  = isMetallicConnected  ? Metallic.g          : 0.0
//   occlusion = isOcclusionConnected ? Occlusion.r         : 1.0
//   roughness = RemapCurves.Sample(clampedSampler, float2(roughness, 0.25)).r   // curve remap
//   return float4(roughness, metallic, occlusion, 1)
// RemapCurves (t3) = the RemapRoughness Curve rasterized to a 1xN LUT row (cook-side, see
// point_ops_combinematerialchannels.cpp); default identity ramp -> remap is passthrough.
//
// Forks (named, DX11->Metal):
//   (1) texSampler address mode: CombineMaterialChannels.t3 binds texSampler(s0) AddressU/V=Mirror,
//       AddressW=Wrap, Filter=MinMagMipLinear. The Metal equivalent is Mirror/Linear; bound in the op.
//       NOT load-bearing here (all three maps sampled at the SAME in-[0,1] psInput.texCoord, no OOB).
//   (2) clampedSampler(s1) Clamp/Linear -> the LUT row is sampled at x=roughness (clamped to [0,1] by the
//       op), y=0.25 (any row of the 1-row LUT). Clamp matters: roughness can exceed [0,1] post-pow.
//   (3) HLSL pow(x,1) is identity; ported verbatim (keeps the source's intent / future-edit point).
#include <metal_stdlib>
#include "combinematerialchannels_params.h"   // CombineMaterialChannelsParams, COMBINEMATERIALCHANNELS_Params
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer); texCoord 0..1 with Y flipped (NDC up vs
// texture down), same as combine3images_vs / blur_vs.
vertex VSOut combinematerialchannels_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of CombineMaterialChannels.hlsl psMain().
fragment float4 combinematerialchannels_fs(VSOut in [[stage_in]],
                                           texture2d<float> imageRoughness [[texture(0)]],
                                           texture2d<float> imageMetallic  [[texture(1)]],
                                           texture2d<float> imageOcclusion [[texture(2)]],
                                           texture2d<float> remapCurves    [[texture(3)]],
                                           sampler texSampler              [[sampler(0)]],
                                           sampler clampedSampler          [[sampler(1)]],
                                           constant CombineMaterialChannelsParams& P
                                               [[buffer(COMBINEMATERIALCHANNELS_Params)]]) {
  float2 uv = in.texCoord;

  float roughness = (P.IsRoughnessConnected > 0.5f)
                        ? pow(imageRoughness.sample(texSampler, uv).r, 1.0f)
                        : 0.5f;
  float metallic  = (P.IsMetallicConnected > 0.5f)
                        ? imageMetallic.sample(texSampler, uv).g
                        : 0.0f;
  float occlusion = (P.IsOcclusionConnected > 0.5f)
                        ? imageOcclusion.sample(texSampler, uv).r
                        : 1.0f;

  roughness = remapCurves.sample(clampedSampler, float2(roughness, 0.25f)).r;

  return float4(roughness, metallic, occlusion, 1.0f);
}
