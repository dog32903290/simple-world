// Shared host<->shader params for the TiXL-ported AttributesFromImageChannels — a Points op that
// samples a Texture2D INPUT and routes the sampled R/G/B/Brightness channels (each with a per-channel
// Factor/Offset gain) into a SELECTED point attribute (position xyz / F1 / F2 / rotate xyz / scale).
// Faithful port of external/tixl
// .../Operators/Lib/Assets/shaders/points/modify/AttributesFromImageChannels.hlsl (.hlsl, the kernel)
// + .../point/modify/AttributesFromImageChannels.cs (the ports + the Attributes enum) + its .t3
// (defaults + the FloatsToBuffer/IntsToBuffer cbuffer routing, traced 1:1 below).
//
// The .hlsl has TWO cbuffers (AttributesFromImageChannels.hlsl:27-56):
//   cbuffer Params : register(b0) {
//     float4x4 transformSampleSpace;          // composed from Stretch/Scale/TextureRotate/Aspect (.t3)
//     float LFactor, LOffset, RFactor, ROffset;
//     float GFactor, GOffset, BFactor, BOffset;
//     float3 Center; float Strength;
//     float2 GainAndBias;
//   }
//   cbuffer Params : register(b1) {
//     int L, R, G, B, Mode, TranslationSpace, RotationSpace, StrengthFactor;
//   }
// We MERGE both into one host struct (the Mac runtime binds a single Params buffer). transformSampleSpace
// is NOT a packed host float4x4 — same discipline as samplepointcolorattributes_params.h: composed
// in-shader from Scale3 (Aspect + the .t3 TransformMatrix UniformScale=0.5 folded host-side) + the
// TextureRotate Euler. See attributesfromimagechannels.metal.
//
// ── .t3 cbuffer ROUTING TRACE (NOT assumed 1:1; the FloatsToBuffer/IntsToBuffer fill order is the
//    cbuffer field order — verified against the .t3 connection list, Cut55 discipline) ──
//  FloatsToBuffer (#3c44cbfd, b0 float list, .t3 connection order):
//    BrightnessFactor(c91340d9)→LFactor   BrightnessOffset(7f1959f5)→LOffset
//    RedFactor(88978d11)→RFactor          RedOffset(88b13f5a)→ROffset
//    GreenFactor(98415cd6)→GFactor        GreenOffset(b6b0362c)→GOffset
//    BlueFactor(09e4c062)→BFactor         BlueOffset(91739865)→BOffset
//    Center.x/.y/.z (b7b3e4c6 Vector3Components) → Center
//    Strength(44b76ec7)→Strength
//    GainAndBias.x/.y (044b3a55 Vector2Components) → GainAndBias
//    + the matrix slot (914ea6e8) ← TransformMatrix(1a1e129f) → transformSampleSpace
//  IntsToBuffer (#650aa5e9, b1 int list, .t3 connection order):
//    Brightness(68087e68)→L  Red(bb19d1e6)→R  Green(faf18518)→G  Blue(69f76f56)→B
//    Mode(6957dfd9)→Mode  TranslationSpace(f936817b)→TranslationSpace
//    RotationSpace(e864651e)→RotationSpace  StrengthFactor(e29b049f)→StrengthFactor
//  NOTE: "Brightness" is the .cs port name; the .hlsl cbuffer field is L (luminance). They are the SAME
//  channel — gray = (r+g+b)/3. The .cs enum field names them Brightness/Red/Green/Blue (each MappedType
//  Attributes), the .hlsl names them L/R/G/B.
//
// ── transformSampleSpace COMPOSITION (.t3) ──
//   M = TransformMatrix(1a1e129f): CreateTransformationMatrix(scaling = Scale3·UniformScale,
//       rotation = PitchYawRoll(TextureRotate), translation = Center).Transpose().
//       UniformScale = 0.5 (.t3 child 1a1e129f InputValues, line 189-193) — DIFFERS from SPCA (1.0).
//   Scale3 (ScaleVector3 dab74335): A = Vec2ToVec3( ScaleVector2(Stretch,(Aspect,1)), z=1 )
//                                     = (Stretch.x·Aspect, Stretch.y, 1)
//                                   ScaleUniform = Scale (.t3 default 1.0 — DIFFERS from SPCA's 2.0)
//     => Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, 1·Scale)   [Aspect = texW/texH, .t3 Div]
//   The shader samples with mul(float4(pos,0), M): w=0 DROPS the translation row (Center is applied
//   separately by `pos -= Center`), so posInObject == qRotateVec3(pos · (Scale3·0.5), R),
//   R = PitchYawRoll(TextureRotate) (Y·X·Z, the project's refuter-P-verified Euler order). The host
//   folds (Scale3 · UniformScale 0.5) into ScaleX/Y/Z and passes the TextureRotate Euler degrees.
//
// ── UV (AttributesFromImageChannels.hlsl:116 — DIFFERS from SPCA) ──
//   uv = posInObject.xy * float2(0.5, -0.5) + float2(0.5, 0.5)
//   (SPCA used float2(1,-1); AFIC uses float2(0.5,-0.5) — its OWN scale, kept in-shader.)
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params (16-byte rows). Count is OUR addition (not in the .hlsl cbuffer): TiXL reads the count
// via ResultPoints.GetDimensions(); we pass it + guard tid>=Count (same as samplepointcolorattributes).
struct AficParams {
#ifdef __METAL_VERSION__
  uint  Count;                      // point count (guard); not in the .hlsl cbuffer
#else
  uint32_t Count;
#endif
  float CenterX, CenterY, CenterZ;            // -> 16. TiXL Center (Vector3), subtracted from position

  // composed Scale3 (Aspect + UniformScale 0.5 folded host-side) + TextureRotate Euler degrees (Y·X·Z)
  float ScaleX, ScaleY, ScaleZ, RotX;         // -> 16
  float RotY, RotZ, _pad0, _pad1;             // -> 16

  // b0 per-channel gains: L=Brightness/luminance, R/G/B channels (each Factor + Offset)
  float LFactor, LOffset, RFactor, ROffset;   // -> 16
  float GFactor, GOffset, BFactor, BOffset;   // -> 16

  float Strength, GainAndBiasX, GainAndBiasY, _pad2;  // -> 16

  // b1 routing ints (each selects an Attribute target 0..12) + Mode/Spaces/StrengthFactor
  int   L, R, G, B;                           // -> 16. Brightness/Red/Green/Blue -> Attribute index
  int   Mode, TranslationSpace, RotationSpace, StrengthFactor;  // -> 16
};

enum AficBinding {
  AFIC_SourcePoints = 0,  // StructuredBuffer<Point>   (t0)
  AFIC_ResultPoints = 1,  // RWStructuredBuffer<Point> (u0)
  AFIC_Params       = 2,  // constant AficParams&      (b0/b1 merged)
};
enum AficTexBinding {
  AFIC_InputTexture = 0,  // Texture2D<float4> inputTexture (t1 in HLSL; texture(0) in MSL)
};
enum AficSamplerBinding {
  AFIC_TexSampler = 0,    // sampler texSampler (s0)
};

#ifndef __METAL_VERSION__
// 16 (Count+Center) + 16 (Scale3+RotX) + 16 (RotY/Z+pad) + 16 (L/L/R/R gains) + 16 (G/G/B/B gains)
// + 16 (Strength+GainBias+pad) + 16 (L/R/G/B ints) + 16 (Mode/Spaces/StrengthFactor) = 128 bytes.
static_assert(sizeof(AficParams) == 128, "AficParams must be 128 bytes (8x16)");
#endif
