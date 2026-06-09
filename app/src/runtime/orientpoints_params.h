// Shared host<->shader params for the TiXL-ported OrientPoints MODIFIER (lane A, batch 2).
// Mirrors external/tixl .../Assets/shaders/points/modify/OrientPoints.hlsl. A count-preserving
// orientation op: reads an input bag, writes the SAME points with each Rotation quaternion
// re-aimed (Z toward a Target, UpVector for roll). All params are scalar (Vector3 -> X/Y/Z
// components) — no packed_float3 / matrix alignment traps (the particle_params.h discipline).
//
// Copies the modifier leaf TEMPLATE shape from transformpoints_params.h (Count + scalar params,
// 16-byte multiple, static_assert).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — OrientPoints. Target/UpVector (Vector3) + scalars, all scalar components.
struct OrientParams {
#ifdef __METAL_VERSION__
  uint Count;          // inherited from the input bag (modifier: count comes from upstream)
  int  AmountFactor;   // FModes: 0=None(1.0), 1=F1(p.FX1), 2=F2(p.FX2) — weights Amount
  int  Flip;           // 0|1: flip the aim direction (sign = Flip ? -1 : +1)
  int  OrientationMode;// OModes: 0=LookAtTarget, 1=Screen(baked), 2=LookAtCamera(baked)
#else
  uint32_t Count;
  int32_t  AmountFactor;
  int32_t  Flip;
  int32_t  OrientationMode;
#endif
  float TargetX, TargetY, TargetZ;        // TiXL Target (Vector3, world position to aim Z at)
  float UpVectorX, UpVectorY, UpVectorZ;  // TiXL UpVector (Vector3, roll reference)
  float Amount;                           // TiXL Amount (0..1 slerp old->new orientation)
  float _pad0;                            // pad to a 16-byte multiple
};

enum OrientBinding {
  ORIENT_SourcePoints = 0,  // const device SwPoint* (t0)
  ORIENT_ResultPoints = 1,  // device SwPoint*       (u0)
  ORIENT_Params = 2,        // constant OrientParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(OrientParams) == 48, "OrientParams 48 bytes (16-byte multiple)");
#endif
