// Shared host<->shader params for the TiXL-ported SoftTransformPoints MODIFIER (lane point_modify).
// Mirrors external/tixl .../point/transform/SoftTransformPoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/SoftTransformPoints.hlsl (.hlsl math).
// A count-preserving MODIFIER: computes a volume-falloff weight (Sphere/Box/Plane/Zebra, smoothstep)
// shaped by GainAndBias × Strength × StrengthFactor, then SOFT-applies a Translate/Rotate/Scale
// transform to Position (lerp by the weight), composes Rotation by the X-axis rotation, and lerps
// FX1 by ScaleFx1/OffsetFx1.  Count INHERITED from upstream.
//
// TiXL inputs (SoftTransformPoints.cs): Points, Amount, Translate(Vec3), Dither(dead), Stretch(Vec3),
//   Scale, Rotate(Vec3), ScaleW, OffsetW, VolumeCenter(Vec3), VolumeType(Shapes), VolumeStretch(Vec3),
//   VolumeSize, FallOff, Bias(dead), UseWAsWeight(dead), Visibility(dead), GainAndBias(Vec2),
//   StrengthFactor(FModes).
//
// TiXL cbuffer (SoftTransformPoints.hlsl:7-34):
//   b0: float4x4 TransformVolume; float3 Translate; float ScatterTranslate; float3 Scale;
//       float ScaleMagnitude; float3 RotateAxis; float FallOff; float2 GainAndBias; float Phase;
//       float Threshold; float ScaleFx1; float OffsetFx1; float Strength;
//   b1: int VolumeShape; int StrengthFactor;
//   The .hlsl reads: Strength(=Amount), Translate, Scale·ScaleMagnitude, RotateAxis, FallOff,
//   GainAndBias, Phase, Threshold, ScaleFx1(=ScaleW), OffsetFx1(=OffsetW), VolumeShape, StrengthFactor.
//   Dither / Bias / UseWAsWeight / Visibility are NOT read -> dropped.
//
// NAMED FORK — TransformVolume (= TransformMatrix(Invert=true) of Translate(VolumeCenter)·
//   Scale(VolumeStretch·VolumeSize), per SoftTransformPoints.t3) composed IN-shader.  The kernel
//   reads BOTH posInVolume = mul(p, TransformVolume) AND volumeCenter = TransformVolume._m30_m31_m32.
//   We build the forward row-major TRS, transpose, invert, and index exactly like the HLSL so the
//   extracted volumeCenter matches bit-for-bit (no sign guess).  No VOLUME rotate input exists.
//   Strength cbuffer field = Amount (verified: .hlsl multiplies the single Strength scalar; the .cs
//   ITransformable wires Amount through).
//
// Flattened to a single struct (16-byte rows; static_assert).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SoftTransformParams {
#ifdef __METAL_VERSION__
  uint  Count;
  int   VolumeShape;     // 0=Sphere,1=Box,2=Plane,3=Zebra
  int   StrengthFactor;  // 0=None,1=F1,2=F2
  float Strength;        // = Amount (TiXL ITransformable Amount -> shader Strength)
#else
  uint32_t Count;
  int32_t  VolumeShape;
  int32_t  StrengthFactor;
  float    Strength;
#endif
  // Point transform.
  float TranslateX, TranslateY, TranslateZ;  float _pad0;        // -> 16
  float ScaleX, ScaleY, ScaleZ;              float ScaleMagnitude; // -> 16 (Scale=Stretch, Mag=Scale)
  float RotateAxisX, RotateAxisY, RotateAxisZ; float _pad1;      // -> 16 (Euler degrees)
  // Volume frame (build inverse-map in-shader).
  float VolumeCenterX, VolumeCenterY, VolumeCenterZ;  float _pad2; // -> 16
  float VolumeStretchX, VolumeStretchY, VolumeStretchZ; float VolumeSize; // -> 16
  // Falloff shaping.
  float FallOff, Phase, Threshold;  float _pad3;   // -> 16
  float GainAndBiasX, GainAndBiasY;                // gain, bias
  float ScaleFx1, OffsetFx1;                       // -> 16 (= ScaleW / OffsetW)
};

enum SoftTransformBinding {
  SOFTXF_SourcePoints = 0,  // const device SwPoint* (t0)
  SOFTXF_ResultPoints = 1,  // device SwPoint*       (u0)
  SOFTXF_Params       = 2,  // constant SoftTransformParams& (b0/b1 merged)
};

#ifndef __METAL_VERSION__
// 16 (Count+VolumeShape+StrengthFactor+Strength) + 16 (Translate+pad) + 16 (Scale+ScaleMag)
// + 16 (RotateAxis+pad) + 16 (VolumeCenter+pad) + 16 (VolumeStretch+VolumeSize)
// + 16 (FallOff+Phase+Threshold+pad) + 16 (Gain+Bias+ScaleFx1+OffsetFx1) = 128 bytes
static_assert(sizeof(SoftTransformParams) == 128, "SoftTransformParams must be 128 bytes (8x16)");
#endif
