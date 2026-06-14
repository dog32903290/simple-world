// Shared host<->shader params for the TiXL-ported PointAttributeFromNoise MODIFIER (batch 24).
// Mirrors external/tixl/Operators/Lib/point/modify/PointAttributeFromNoise.cs (.cs ports) +
// .../Assets/shaders/points/modify/PointAttributesFromNoise.hlsl (.hlsl math).
//
// A count-preserving MODIFIER: samples a 3D simplex-noise field per point and writes it into
// chosen attributes (position X/Y/Z/W or rotation X/Y/Z) via 4 channels (Brightness/R/G/B), each
// with its own attribute target + Factor + Offset.
//
// Per point (.hlsl lines 90-139):
//   variationOffset = hash31((i.x%1234)/0.123) * Variation
//   c = snoiseVec3((P.Position + Center)*0.91 + variationOffset + Phase) * Frequency)
//   c *= Amount / 100                       (.hlsl else branch line 100; see FORK below)
//   gray = (c.r+c.g+c.b)/3
//   ff = Factors[L]*(gray*LF+LO) + Factors[R]*(c.r*RF+RO) + Factors[G]*(c.g*GF+GO) + Factors[B]*(c.b*BF+BO)
//   Position += ff.xyz;  W = clamp(W + ff.w, 0, 10000)
//   rot{X,Y,Z}Factor accumulate the channels whose attribute == Rotate_{X,Y,Z}(5/6/7)
//   rot = qMul(rot, qFromAngleAxis(rotXF, X)) then Y then Z   (.hlsl lines 134-136, order X->Y->Z)
//
// TiXL .cs ports: GPoints(buffer), Brightness/Red/Green/Blue(int attribute enum, MappedType=
//   Attributes), {Brightness,Red,Green,Blue}{Factor,Offset}(float), Center(Vector3), Phase(float),
//   Frequency(float), Amount(float), Variation(float).
//
// FORK (named): TiXL has optional RemapNoise(Gradient) + UseRemapCurve(bool) + remapCurveTexture
//   ports/branch (.hlsl lines 92-98). Per the batch-24 work order we do NOT wire the RemapNoise
//   gradient port (faithful UNDER-wiring, not a logic change): we bake UseRemapCurve = false, so
//   the kernel always takes the else branch `c *= Amount/100` (.hlsl line 100). Everything else is
//   verbatim. (Wiring a Gradient texture port is out of scope; baking it off is the honest no-op.)
//
// Attributes enum (.cs lines 81-91), shared with the kernel's Factors[] table:
//   NotUsed=0, For_X=1, For_Y=2, For_Z=3, For_W=4, Rotate_X=5, Rotate_Y=6, Rotate_Z=7.
//
// 16-byte alignment maintained via static_assert (mirrors the .hlsl cbuffer Params layout b1).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct PointAttributeFromNoiseParams {
#ifdef __METAL_VERSION__
  uint  Count;   // inherited from upstream Points bag
  float _pad0;
  float _pad1;
  float _pad2;   // -> 16 bytes
#else
  uint32_t Count;
  float    _pad0;
  float    _pad1;
  float    _pad2;
#endif
  // .hlsl cbuffer Params (b1): L/R/G/B attribute selectors (float-coded enum) + factor + offset.
  float L, LFactor, LOffset;   // .cs Brightness + BrightnessFactor + BrightnessOffset
  float _padL;                 // pad row -> 16
  float R, RFactor, ROffset;   // .cs Red + RedFactor + RedOffset
  float _padR;
  float G, GFactor, GOffset;   // .cs Green + GreenFactor + GreenOffset
  float _padG;
  float B, BFactor, BOffset;   // .cs Blue + BlueFactor + BlueOffset
  float _padB;
  float CenterX, CenterY, CenterZ;  // .cs Center (Vector3)
  float _padC;                       // .hlsl __padding -> 16
  float Phase;       // .cs Phase
  float Frequency;   // .cs Frequency
  float Amount;      // .cs Amount
  float Variation;   // .cs Variation -> 16
};

enum PointAttributeFromNoiseBinding {
  POINTATTRNOISE_SourcePoints = 0,  // const device SwPoint* (t0)
  POINTATTRNOISE_ResultPoints = 1,  // device SwPoint*       (u0)
  POINTATTRNOISE_Params       = 2,  // constant PointAttributeFromNoiseParams& (b0)
};

#ifndef __METAL_VERSION__
// Count+3pad(16) | L/LF/LO/pad(16) | R(16) | G(16) | B(16) | Center+pad(16) | Phase/Freq/Amt/Var(16)
// = 7 rows * 16 = 112 bytes
static_assert(sizeof(PointAttributeFromNoiseParams) == 112,
              "PointAttributeFromNoiseParams must be 112 bytes (7x16)");
#endif
