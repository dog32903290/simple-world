// Shared host<->shader params for the TiXL-ported CombineMaterialChannels IMAGE FILTER (lane
// multi-image, image/use). Faithful 1:1 mirror of
//   external/tixl Operators/Lib/Assets/shaders/img/use/CombineMaterialChannels.hlsl (Thomas Helzle 2022).
//
// CombineMaterialChannels packs three PBR material maps into one texture's R/G/B/A:
//   out.r = isRoughnessConnected ? pow(Roughness.r, 1) : 0.5      (then remapped through RemapCurves LUT)
//   out.g = isMetallicConnected  ? Metallic.g          : 0.0
//   out.b = isOcclusionConnected ? Occlusion.r         : 1.0
//   out.a = 1.0
//   out.r = RemapCurves.Sample(clampedSampler, float2(out.r, 0.25)).r   // curve remap of roughness
//
// This is the SIBLING of CombineMaterialChannels2 (point_ops_combinematerialchannels2.cpp): CMC2 is the
// generic 15-way channel packer (img-combine-3.hlsl), CMC is the FIXED roughness/metallic/occlusion
// packer with its OWN shader (CombineMaterialChannels.hlsl) + a roughness remap CURVE.
//
// cbuffer ParamConstants (HLSL b0) field order — packed here as all-scalar floats (particle_params.h
// discipline). CombineMaterialChannels.hlsl b0 is exactly three floats:
//     float IsRoughnessConnected;  float IsMetallicConnected;  float IsOcclusionConnected;
// These three are the .t3's BoolToFloat(GetTextureSize(input).IsConnected) flags — in sw they are simply
// whether each fixed Texture2D port is wired (c.inputTextures[i] != null), 1.0 wired / 0.0 unwired.
//
// .t3 STEP-0 (atomic op, OWN fullscreen pixel shader): the .t3 is the standard _ImageFxShaderSetup render
// pipeline boilerplate (VertexShaderStage / PixelShaderStage / Draw / RenderTarget / 2 SamplerStates)
// wrapping CombineMaterialChannels.hlsl, PLUS one CurvesToTexture child that rasterizes the RemapRoughness
// Curve input into the t3 RemapCurves LUT texture. Fixed numbered Texture2D ports (Roughness t0 / Metallic
// t1 / Occlusion t2), NOT MultiInput.
//
// SAMPLERS (CombineMaterialChannels.t3, verbatim): texSampler(s0)=MinMagMipLinear, AddressU/V=Mirror;
// clampedSampler(s1)=MinMagMipLinear, AddressU/V/W=Clamp.
//
// CURVE (RemapRoughness): default = identity ramp (0,0)->(1,1) Linear (CombineMaterialChannels.t3) so at
// production default the remap is a passthrough roughness->roughness. Rasterized via the SAME Curve->row
// math CurvesToTexture uses (curve.sample(i/N)); there is no Curve PRODUCER op yet so production uses the
// embedded default (TexCookCtx::inputCurves is null/empty — [fork-cmc-embedded-default-curve], identical
// to CurvesToTexture's documented fork).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct CombineMaterialChannelsParams {
  // cbuffer ParamConstants (HLSL b0), same field order:
  float IsRoughnessConnected;   // 1.0 if Roughness  (t0) wired, else 0.0
  float IsMetallicConnected;    // 1.0 if Metallic   (t1) wired, else 0.0
  float IsOcclusionConnected;   // 1.0 if Occlusion  (t2) wired, else 0.0
  float _pad;                   // 16-byte multiple (host/shader layout parity)
};

enum CombineMaterialChannelsBinding {
  COMBINEMATERIALCHANNELS_Params = 0,  // constant CombineMaterialChannelsParams& (b0)
  // texture(0)=Roughness, texture(1)=Metallic, texture(2)=Occlusion, texture(3)=RemapCurves LUT.
  // sampler(0)=texSampler (linear/Mirror), sampler(1)=clampedSampler (linear/Clamp).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(CombineMaterialChannelsParams) == 16,
              "CombineMaterialChannelsParams 16 bytes (3 connected-flags + 1 pad)");
#endif
