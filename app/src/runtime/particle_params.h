// Shared host<->shader params for the TiXL-ported ParticleSystem integrator.
// Mirrors the two cbuffers in external/tixl .../particles/ParticleSystem.hlsl
// (Params @b0, IntParams @b1). Uploaded via setBytes; included by both sides.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params : register(b0)
struct SimParams {
  float Speed;
  float Drag;
  float InitialVelocity;
  float Time;
  float OrientTowardsVelocity;
  float RadiusFromW;
  float LifeTime;
  float _pad;  // -> 32 bytes
};

// cbuffer IntParams : register(b1)
struct SimIntParams {
#ifdef __METAL_VERSION__
  int TriggerEmit;
  int TriggerReset;
  int CollectCycleIndex;
  int SetFx1To;
  int SetFx2To;
  int EmitMode;
  int IsAutoCount;
  int EmitVelocityFactor;
  // MSL has no StructuredBuffer.GetDimensions(); host supplies the counts.
  int EmitCount;         // # of emit points
  int MaxParticleCount;  // capacity of the Particle/Result buffers
#else
  int32_t TriggerEmit;
  int32_t TriggerReset;
  int32_t CollectCycleIndex;
  int32_t SetFx1To;
  int32_t SetFx2To;
  int32_t EmitMode;
  int32_t IsAutoCount;
  int32_t EmitVelocityFactor;
  int32_t EmitCount;
  int32_t MaxParticleCount;
#endif
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SimParams) == 32, "SimParams 32 bytes");
static_assert(sizeof(SimIntParams) == 40, "SimIntParams 40 bytes");
#endif

// Binding slots for the sim integrator (matches the .metal kernel).
enum SimBinding {
  SIM_EmitPoints = 0,    // const device Point*   (t0)
  SIM_Particles = 1,     // device Particle*      (u0)
  SIM_ResultPoints = 2,  // device Point*         (u1)
  SIM_Params = 3,        // constant SimParams&   (b0)
  SIM_IntParams = 4,     // constant SimIntParams& (b1)
};

// TurbulenceForce params — mirrors TurbulanceForce.hlsl cbuffer Params (b0),
// + Count (no GetDimensions in MSL). The field/shader-graph inputs are omitted
// (a plain TurbulenceForce with no field: GetField()==1 -> fieldAmount==1).
struct TurbParams {
  float Amount;
  float Frequency;
  float Phase;
  float Variation;
  float SpeedFactor;
  float VariationGroupCount;
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float _pad;  // -> 32 bytes
};

enum ForceBinding {
  FORCE_Particles = 0,  // device Particle* (u0)
  FORCE_Params = 1,     // constant TurbParams& (b0)
};

// RadialPoints emitter — generates a ring of Points to feed ParticleSystem.
struct EmitParams {
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float Radius;
  float _pad0;
  float _pad1;  // -> 16 bytes
};

enum EmitBinding {
  EMIT_Points = 0,  // device Point* (u0)
  EMIT_Params = 1,  // constant EmitParams& (b0)
};

// RadialPoints (point-operator graph, lane A) — faithful port of TiXL
// .../points/generate/RadialPoints.hlsl SHAPE math (spiral around an axis). Scalar params
// only; TiXL's Vector inputs (Axis/Center/Color) + quaternion orientation are baked to
// TiXL-equivalent defaults in radial_points.metal until vector params land in NodeSpec.
// All-scalar layout (no packed_float3) so there are zero cbuffer alignment traps.
struct RadialParams {
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float Radius;
  float RadiusOffset;
  float StartAngle;  // degrees
  float Cycles;      // turns around the axis (TiXL "Cycles")
  float ScaleBase;   // TiXL PointScaleRange.x
  float ScaleByF;    // TiXL PointScaleRange.y (x normalized index)
  float CenterX;     // TiXL Center (Vector3) — translation added to every point. All-scalar
  float CenterY;     // (no packed_float3) so there are zero cbuffer alignment traps; the
  float CenterZ;     // shader reassembles float3(CenterX,Y,Z). First vector param on the contract.
  float _pad0;
  float _pad1;       // -> 48 bytes (16-byte multiple, like Sim/TurbParams)
};

enum RadialBinding {
  RADIAL_Points = 0,  // device Point* (u0)
  RADIAL_Params = 1,  // constant RadialParams& (b0)
};

enum DrawBinding {
  DRAW_Points = 0,      // device const Point* (vertex buffer)
  DRAW_ViewExtent = 1,  // constant float&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(TurbParams) == 32, "TurbParams 32 bytes");
static_assert(sizeof(EmitParams) == 16, "EmitParams 16 bytes");
static_assert(sizeof(RadialParams) == 48, "RadialParams 48 bytes");
#endif
