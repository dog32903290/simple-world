// Shared host<->shader params for the TiXL-ported SetPointAttributes MODIFIER (lane A, modify).
// Mirrors external/tixl .../Assets/shaders/points/modify/SetPointAttributes.hlsl. The .hlsl is a
// gated attribute writer: per point it computes a single `strength = Amount * factor` (factor =
// 1 / FX1 / FX2 by AmountFactor) and, for each Set* flag that is on, lerps (Rotation: slerps) the
// old attribute toward the supplied target. count-preserving — every point keeps its index.
//
// All-scalar (the particle_params.h discipline): Vector3/4 targets become NameX/NameY/Z[/W], bool
// flags become int 0/1, enum becomes int. No packed_float3 / matrix alignment traps. Pad to a
// 16-byte multiple.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — SetPointAttributes. Per-attribute Set* gate + target value, plus the
// Amount/AmountFactor strength controls (TiXL b0 + b1 merged into one scalar struct).
struct SetPointAttributesParams {
#ifdef __METAL_VERSION__
  uint Count;         // inherited from the input bag (modifier: count comes from upstream Points)
  int  AmountFactor;  // SetPtAttrBinding StrengthFactors: 0 None(×1) / 1 F1(×FX1) / 2 F2(×FX2)
#else
  uint32_t Count;
  int32_t  AmountFactor;
#endif
  int   SetPosition;                          // bool gate
  float PositionX, PositionY, PositionZ;      // TiXL Position (Vector3)
  int   SetRotation;                          // bool gate
  float RotationAxisX, RotationAxisY, RotationAxisZ;  // TiXL RotationAxis (Vector3)
  float RotationAngle;                        // TiXL RotationAngle (degrees)
  int   SetStretch;                           // bool gate (TiXL .cs SetExtend / .hlsl SetStretch)
  float StretchX, StretchY, StretchZ;         // TiXL Extend/Stretch (Vector3) -> SwPoint.Scale
  int   SetFx1;                               // bool gate
  float Fx1;                                  // TiXL Fx1 (Single)
  int   SetFx2;                               // bool gate
  float Fx2;                                  // TiXL Fx2 (Single)
  int   SetColor;                             // bool gate
  float ColorR, ColorG, ColorB, ColorA;       // TiXL Color (Vector4)
  float Amount;                               // TiXL Amount (Single), base strength
  float _pad0, _pad1, _pad2;                  // pad 100 -> 112 (16-byte multiple)
};

enum SetPtAttrBinding {
  SETPTATTR_SourcePoints = 0,  // const device SwPoint* (t0)
  SETPTATTR_ResultPoints = 1,  // device SwPoint*       (u0)
  SETPTATTR_Params = 2,        // constant SetPointAttributesParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SetPointAttributesParams) == 112,
              "SetPointAttributesParams 112 bytes (16-byte multiple)");
#endif
