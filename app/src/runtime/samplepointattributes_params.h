// Shared host<->shader params for the TiXL-ported SamplePointAttributes_v1 — a texture-into-points
// seam consumer (SAME seam as SamplePointColorAttributes / AttributesFromImageChannels). Mirrors the
// cbuffer of external/tixl .../Assets/shaders/points/modify/SamplePointAttributes.hlsl:21-46. The .hlsl
// cbuffer (b0) is, in order:
//
//   float4x4 transformSampleSpace;          // sample-space transform (Stretch/Scale/TextureRotate/Aspect)
//   float L,LFactor,LOffset;                 // brightness(luminance) channel routing enum + gain
//   float R,RFactor,ROffset;                 // red   channel routing enum + gain
//   float G,GFactor,GOffset;                 // green channel routing enum + gain
//   float B,BFactor,BOffset;                 // blue  channel routing enum + gain
//   float __padding;
//   float3 Center; float Mode;               // Center subtracted from pos; Mode = Add(0)/Multiply(1)
//   float TranslationSpace, RotationSpace;   // Object(0)/Point(1)
//
// (The .cs ALSO has Alpha/AlphaFactor/AlphaOffset inputs, but they are COMMENTED OUT in the kernel —
//  see SamplePointAttributes.hlsl:47-49 — so Alpha is DEAD. The NodeSpec carries the Alpha port for
//  1:1 .cs parity but it routes NOTHING. fork-alpha-dead-in-kernel = faithful to TiXL's commented code.)
//
// transformSampleSpace is COMPOSED IN-SHADER from raw scalars (the SAME discipline as
// samplepointcolorattributes_params.h — avoid a packed host float4x4). The .t3 builds the matrix via the
// SAME _transformSampleSpace compound (TransformMatrix child):
//   M = CreateTransformationMatrix(pivot=0, scalingRotation=Identity, scaling=Scale3,
//                                  rotation=PitchYawRoll(TextureRotate), translation=Center).Transpose()
//   Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, 1·Scale)   [Aspect = texW/texH, .t3 Div]
//   The sample uses mul(float4(pos,0), M): w=0 DROPS translation (Center applied separately by
//   `pos -= Center`), so posInObject == qRotateVec3(pos · Scale3, R), R = PitchYawRoll(TextureRotate)
//   (Y·X·Z Euler order — the project's refuter-P-verified order). The host folds Scale3 (Aspect from the
//   bound texture's dims) + passes Scale3.xyz + the TextureRotate Euler degrees; the shader builds R.
//   (.t3 default Scale = 1.0 — DIFFERENT from SamplePointColorAttributes' 2.0.)
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — SamplePointAttributes (16-byte rows). Count is OUR addition (not in the .hlsl cbuffer):
// the TiXL kernel reads point count via ResultPoints.GetDimensions(); we pass it + guard tid>=Count, like
// samplepointcolorattributes_params.h. transformSampleSpace composed in-shader from Scale3 (ScaleX/Y/Z,
// Aspect folded host-side) + the TextureRotate Euler (RotX/Y/Z). Channel routing = the 4 enum/factor/offset
// triplets (L/R/G/B). Mode + TranslationSpace + RotationSpace floats.
struct SampleAttrParams {
#ifdef __METAL_VERSION__
  uint  Count;                      // point count (guard); not in the .hlsl cbuffer
#else
  uint32_t Count;
#endif
  float CenterX, CenterY, CenterZ;            // -> 16. TiXL Center (Vector3), subtracted from position
  float ScaleX, ScaleY, ScaleZ, _padScale;    // -> 16. composed Scale3 (Aspect folded host-side) + pad
  float RotX, RotY, RotZ, _padRot;            // -> 16. TextureRotate Euler degrees (Y·X·Z) + pad
  float L, LFactor, LOffset, R;               // -> 16. L enum + L gain + R enum
  float RFactor, ROffset, G, GFactor;         // -> 16.
  float GOffset, B, BFactor, BOffset;         // -> 16.
  float Mode, TranslationSpace, RotationSpace, _padTail;  // -> 16. Add/Mul + Object/Point spaces + pad
};

enum SampleAttrBinding {
  SAMPLEATTR_SourcePoints = 0,  // const device SwPoint* (t0)
  SAMPLEATTR_ResultPoints = 1,  // device SwPoint*       (u0)
  SAMPLEATTR_Params       = 2,  // constant SampleAttrParams& (b0)
};
enum SampleAttrTexBinding {
  SAMPLEATTR_InputTexture = 0,  // Texture2D<float4> inputTexture (t1 in HLSL; texture(0) in MSL)
};
enum SampleAttrSamplerBinding {
  SAMPLEATTR_TexSampler = 0,    // sampler texSampler (s0)
};

#ifndef __METAL_VERSION__
// 16 (Count+Center) + 16 (Scale3+pad) + 16 (Rot+pad) + 16 (L/LF/LO/R) + 16 (RF/RO/G/GF) +
// 16 (GO/B/BF/BO) + 16 (Mode/TS/RS+pad) = 112 bytes.
static_assert(sizeof(SampleAttrParams) == 112, "SampleAttrParams must be 112 bytes (7x16)");
#endif
