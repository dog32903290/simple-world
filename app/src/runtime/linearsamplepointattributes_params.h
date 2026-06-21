// Shared host<->shader params for the TiXL-ported LinearSamplePointAttributes — a Points op that
// samples a Texture2D INPUT along the point INDEX (uv = (i/pointCount, 0.5), a 1D LINEAR strip — hence
// the name) and routes the sampled R/G/B/Brightness channels (each with a per-channel Factor/Offset
// gain) into a SELECTED point attribute (position xyz / F1 / rotate xyz / stretch xyz / F2), all by a
// blend Strength. Faithful port of external/tixl
// .../Operators/Lib/Assets/shaders/points/modify/LinearSamplePointAttributes.hlsl (the kernel)
// + .../point/modify/LinearSamplePointAttributes.cs (the ports + the Attributes enum).
//
// UNLIKE SamplePointColorAttributes / AttributesFromImageChannels, this op has NO transformSampleSpace
// and NO Center: the .hlsl samples at uv = (index / divider, 0.5) where divider = pointCount<2?1:pointCount
// (LinearSamplePointAttributes.hlsl:62-66). So there is nothing position-derived in the sample — the
// texture is read as a 1D gradient strip indexed by point order. The cbuffer is therefore a pure scalar
// list (no matrix slot) and there is NO .t3 FloatsToBuffer routing trap to trace: the .hlsl's two
// cbuffers map their fields 1:1 to the .cs [Input] ports.
//
// The .hlsl has TWO cbuffers (LinearSamplePointAttributes.hlsl:24-46):
//   cbuffer FloatParams : register(b0) {
//     float LFactor, LOffset, RFactor, ROffset;
//     float GFactor, GOffset, BFactor, BOffset;
//     float Strength;
//   }
//   cbuffer IntParams : register(b1) {
//     int L, R, G, B, Mode, TranslationSpace, RotationSpace, StrengthFactor;
//   }
// We MERGE both into one host struct (the Mac runtime binds a single Params buffer). L = the
// "Brightness" .cs port (luminance, gray = (r+g+b)/3); R/G/B = the color channels. Each routing int
// selects an Attribute target from the .cs Attributes enum:
//   0 NotUsed, 1 For_X, 2 For_Y, 3 For_Z, 4 For_F1, 5 Rotate_X, 6 Rotate_Y, 7 Rotate_Z,
//   8 Stretch_X, 9 Stretch_Y, 10 Stretch_Z, 11 For_F2.
// (NOTE: this enum DIFFERS from AttributesFromImageChannels — there F1=4,F2=5,Rot=6/7/8,Scale=9..12.)
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params (16-byte rows). Count is OUR addition (not in the .hlsl cbuffer): TiXL reads the count
// via ResultPoints.GetDimensions() AND uses it as the sample divider; we pass it + guard tid>=Count
// (same as samplepointcolorattributes / attributesfromimagechannels) AND the shader divides by it.
struct LspaParams {
#ifdef __METAL_VERSION__
  uint  Count;                      // point count: guard + the uv divider (i/Count)
#else
  uint32_t Count;
#endif
  float LFactor, LOffset, RFactor;            // -> 16
  float ROffset, GFactor, GOffset, BFactor;   // -> 16
  float BOffset, Strength, _pad0, _pad1;      // -> 16

  // b1 routing ints (each selects an Attribute target 0..11) + Mode/Spaces/StrengthFactor
  int   L, R, G, B;                           // -> 16. Brightness/Red/Green/Blue -> Attribute index
  int   Mode, TranslationSpace, RotationSpace, StrengthFactor;  // -> 16
};

enum LspaBinding {
  LSPA_SourcePoints = 0,  // StructuredBuffer<Point>   (t0)
  LSPA_ResultPoints = 1,  // RWStructuredBuffer<Point> (u0)
  LSPA_Params       = 2,  // constant LspaParams&      (b0/b1 merged)
};
enum LspaTexBinding {
  LSPA_InputTexture = 0,  // Texture2D<float4> inputTexture (t1 in HLSL; texture(0) in MSL)
};
enum LspaSamplerBinding {
  LSPA_TexSampler = 0,    // sampler texSampler (s0)
};

#ifndef __METAL_VERSION__
// 16 (Count+L/L/R gains) + 16 (R/G/G/B gains) + 16 (B/Strength+pad) + 16 (L/R/G/B ints)
// + 16 (Mode/Spaces/StrengthFactor) = 80 bytes.
static_assert(sizeof(LspaParams) == 80, "LspaParams must be 80 bytes (5x16)");
#endif
