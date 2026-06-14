// Shared host<->shader params for the TiXL-ported SelectPoints MODIFIER (lane point_modify).
// Mirrors external/tixl .../point/modify/SelectPoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/SelectPoints.hlsl (.hlsl math).
// A count-preserving MODIFIER: computes a per-point volume-selection scalar (Sphere/Box/Plane/
// Zebra/Noise) shaped by FallOff + GainAndBias, combined with the point's existing FX1/FX2 weight
// by SelectMode, written into FX1 or FX2 (WriteTo).  Position is UNTOUCHED.  Count INHERITED.
//
// TiXL inputs (SelectPoints.cs): Points, Strength, StrengthFactor(FModes), WriteTo(FModes),
//   Mode(Modes), ClampResult(bool), VolumeShape(Shapes), VolumeCenter(Vec3), VolumeStretch(Vec3),
//   VolumeScale(float), VolumeRotate(Vec3), FallOff(float), GainAndBias(Vec2), Scatter(float),
//   Phase(float), Threshold(float), Visibility(GizmoVisibility,dead), DiscardNonSelected(bool),
//   SetW(bool,dead).  Visibility/SetW are commented-out / unused in the .hlsl -> dropped.
//
// NAMED FORK — TransformVolume composed IN-shader from the VolumeCenter/Stretch/Scale/Rotate
//   scalars (TiXL builds the float4x4 host-side via a wired TransformMatrix child).  For a pure
//   TRS volume (no shear), `posInVolume = mul(float4(posInObject,1), TransformVolume)` ==
//   qRotateVec3(posInObject - Center, conj(R)) / (Stretch*VolumeScale) — the world→volume map.
//   Euler order Y·X·Z (= CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z)), same as the other point ops.
//
// Flattened to a single struct (16-byte rows; static_assert).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SelectPointsParams {
#ifdef __METAL_VERSION__
  uint  Count;
  int   VolumeShape;   // 0=Sphere,1=Box,2=Plane,3=Zebra,4=Noise
  int   SelectMode;    // 0=Override,1=Add,2=Sub,3=Multiply,4=Invert
  int   WriteTo;       // 0=None,1=F1,2=F2
#else
  uint32_t Count;
  int32_t  VolumeShape;
  int32_t  SelectMode;
  int32_t  WriteTo;
#endif
#ifdef __METAL_VERSION__
  int   StrengthFactor; // 0=None,1=F1,2=F2
  int   ClampResult;    // bool
  int   DiscardNonSelected; // bool
  int   _pad0;          // -> 16 bytes
#else
  int32_t  StrengthFactor;
  int32_t  ClampResult;
  int32_t  DiscardNonSelected;
  int32_t  _pad0;
#endif
  // Volume frame scalars (compose the world->volume map in-shader).
  float VolumeCenterX, VolumeCenterY, VolumeCenterZ;  float _pad1;   // -> 16
  float VolumeStretchX, VolumeStretchY, VolumeStretchZ; float VolumeScale;  // -> 16
  float VolumeRotateX, VolumeRotateY, VolumeRotateZ;  float _pad2;   // -> 16 (Euler degrees)
  // Selection scalars.
  float FallOff, Strength;          // 8
  float GainAndBiasX, GainAndBiasY; // gain, bias  (-> 16)
  float Phase, Threshold, Scatter;  float _pad3;     // -> 16
};

enum SelectPointsBinding {
  SELECTPOINTS_SourcePoints = 0,  // const device SwPoint* (t0)
  SELECTPOINTS_ResultPoints = 1,  // device SwPoint*       (u0)
  SELECTPOINTS_Params       = 2,  // constant SelectPointsParams& (b0..b2 merged)
};

#ifndef __METAL_VERSION__
// 16 (Count+VolumeShape+SelectMode+WriteTo) + 16 (StrengthFactor+ClampResult+Discard+pad)
// + 16 (Center+pad) + 16 (Stretch+VolumeScale) + 16 (Rotate+pad)
// + 16 (FallOff+Strength+Gain+Bias) + 16 (Phase+Threshold+Scatter+pad) = 112 bytes
static_assert(sizeof(SelectPointsParams) == 112, "SelectPointsParams must be 112 bytes (7x16)");
#endif
