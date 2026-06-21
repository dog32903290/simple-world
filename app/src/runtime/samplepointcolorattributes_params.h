// Shared host<->shader params for the TiXL-ported SamplePointColorAttributes — the FIRST Points op
// with a Texture2D INPUT (the proving op for the texture-into-points seam). Mirrors the cbuffer of
// external/tixl .../Assets/shaders/points/modify/SamplePointColorAttributes.hlsl:6-14 (Cut55 trap:
// the .t3 builds this cbuffer via a FloatsToBuffer node graph — we read the .hlsl cbuffer DIRECTLY
// and map each field by hand, NOT reproduce the node graph). The .hlsl cbuffer is:
//
//   float4x4 transformSampleSpace;  // sample-space transform (Stretch/Scale/TextureRotate/Aspect)
//   float3   Center; float Mode;    // Center subtracted from pos; Mode = RgbBlendMode index
//   float4   BaseColor;             // multiplies the sampled texel before BlendColors
//
// transformSampleSpace COMPOSITION (NOT a host float4x4 — composed in-shader from raw scalars, the
// SAME discipline as transformsomepoints_params.h / polartransformpoints_params.h: avoid a packed
// float4x4 host param). The .t3 builds the matrix via the TransformMatrix child (23b4b95e):
//   M = CreateTransformationMatrix(pivot=0, scalingRotation=Identity, scaling=Scale3,
//                                  rotation=PitchYawRoll(TextureRotate), translation=Center),
//       then .Transpose() for HLSL's row-vector mul(float4(pos,0), M).
//   Scale3 = ScaleVector3 (468d48a7): Result = A · B · ScaleUniform where
//       A = Vec2ToVec3( ScaleVector2(Stretch, (Aspect,1)) , z=1 ) = (Stretch.x·Aspect, Stretch.y, 1)
//       B = (1,1,1) default ; ScaleUniform = Scale (op input, default 2.0)
//     => Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, 1·Scale)   [Aspect = texW/texH, .t3 Div]
//   The shader's sample uses mul(float4(pos,0), M): w=0 DROPS the translation row (Center is applied
//   separately by the kernel's `pos -= Center`), so only the 3×3 Scale·Rotate matters →
//   posInObject == qRotateVec3(pos · Scale3, R), R = PitchYawRoll(TextureRotate) (Y·X·Z, the project's
//   refuter-P-verified Euler order). The host composes Scale3 (Aspect from the bound texture's dims)
//   and passes Scale3.xyz + the TextureRotate Euler degrees; the shader builds R and applies it.
//   Aspect is a one-axis (X) correction that is a NO-OP for square textures (texW==texH → Aspect 1).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — SamplePointColorAttributes (16-byte rows). Count is OUR addition (not in the .hlsl
// cbuffer): the TiXL kernel reads point count via ResultPoints.GetDimensions(); we pass it as a param
// + guard tid>=Count, exactly like transformpoints_params.h (the dispatch is calcDispatchCount(count,tg)
// threadgroups → tid can overrun the bag). transformSampleSpace is composed in-shader from
// Scale3 (ScaleX/Y/Z, Aspect already folded in host-side) + the TextureRotate Euler (RotX/Y/Z).
struct SpcaParams {
#ifdef __METAL_VERSION__
  uint  Count;                      // point count (guard); not in the .hlsl cbuffer (see note)
#else
  uint32_t Count;
#endif
  float CenterX, CenterY, CenterZ;            // -> 16. TiXL Center (Vector3), subtracted from position
  float Mode, BaseColorR, BaseColorG, BaseColorB;  // -> 16. Mode index + BaseColor.rgb
  float BaseColorA, ScaleX, ScaleY, ScaleZ;        // -> 16. BaseColor.a + composed Scale3 (Aspect folded)
  float RotX, RotY, RotZ, _pad0;                    // -> 16. TextureRotate Euler degrees (Y·X·Z) + pad
};

enum SpcaBinding {
  SPCA_SourcePoints = 0,  // const device SwPoint* (t0)
  SPCA_ResultPoints = 1,  // device SwPoint*       (u0)
  SPCA_Params       = 2,  // constant SpcaParams&  (b0)
};
// Texture + sampler bind slots (separate spaces from buffers; mirror crop.metal/blur.metal binds).
enum SpcaTexBinding {
  SPCA_InputTexture = 0,  // Texture2D<float4> inputTexture (t1 in HLSL; texture(0) in MSL)
};
enum SpcaSamplerBinding {
  SPCA_TexSampler = 0,    // sampler texSampler (s0)
};

#ifndef __METAL_VERSION__
// 16 (Count+Center) + 16 (Mode+BaseColor.rgb) + 16 (BaseColor.a+Scale3) + 16 (Rot+pad) = 64 bytes.
static_assert(sizeof(SpcaParams) == 64, "SpcaParams must be 64 bytes (4x16)");
#endif
