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

// Force-pass params (Turb/Dir/VecField/Vel/AxisStep/SnapAngles/FieldDist/RandomJump/FieldVolume) +
// ForceBinding + ForceKind were PEELED into force_params.h when FieldVolumeForce hit the 400-line cap
// (ARCHITECTURE.md rule 4). Included here so every existing `#include "particle_params.h"` (incl. the
// precompiled .metal kernels via -I src/runtime) still resolves them — zero caller edits.
#include "force_params.h"  // bare path: resolves under -I src/runtime (.metal) AND -I src (.cpp, via this dir)

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

#ifndef __METAL_VERSION__
// ---------------------------------------------------------------------------
// Host emission/recycle policy — the parity gap batch 6 closed.
//
// TiXL's ParticleSystem (external/tixl .../particle/ParticleSystem.t3) drives the
// integrator every frame as a CYCLE BUFFER, not a one-shot:
//   * Emit input default = true  -> TriggerEmit fires EVERY frame, not just on seed.
//   * A CountInt advances CollectCycleIndex by `newPointCount` (the emit-bag size) each
//     frame (.t3 CountInt.Delta <- GetBufferComponents of EmitPoints), so a fresh block
//     of pool slots gets (re-)emitted while the rest keep integrating.
//   * MaxParticleCount defaults to 100000 >> newPointCount, and IsAutoCount = (Max < 0)
//     -> 0 by default. With IsAutoCount=0 the shader ages particles out
//     (lifeTime = maxParticleCount/(newPointCount*60) s) right as the cycle wraps back to
//     overwrite them. Emit-per-frame + cycle-advance + aging = the closed recycle loop.
//
// Our graph wires RadialPoints(emitCount) -> ParticleSystem; the faithful behaviour is a
// pool LARGER than the emit ring so the cycle can rotate (with pool==emit every slot
// re-emits every frame and motion freezes). kPoolLifeFrames sets that ratio: pool =
// emitCount * kPoolLifeFrames, giving a recycle period (== particle lifetime) of
// kPoolLifeFrames/60 s. Capped by kMaxPool so a large ring stays within a memory budget
// (the lifetime shortens past the cap, recycle still holds).
constexpr int   kPoolLifeFrames = 180;     // ~3 s recycle period at 60 fps
constexpr int   kMaxPool = 262144;         // pool particle cap (~16 MB at 64 B/Particle)

// Pool (= MaxParticleCount) for an emit ring of `emitCount` points. >= emitCount always.
inline uint32_t particlePoolCount(uint32_t emitCount) {
  if (emitCount == 0) return 0;
  uint64_t pool = (uint64_t)emitCount * (uint64_t)kPoolLifeFrames;
  if (pool > (uint64_t)kMaxPool) pool = (uint64_t)kMaxPool;
  return pool < emitCount ? emitCount : (uint32_t)pool;
}

// Build the per-frame IntParams for the cycle buffer. `frame` is this system's own
// monotonic step count (drives CollectCycleIndex = frame*emitCount, the shader wraps it
// mod pool). emit/reset are the seed/teardown triggers; on steady frames emit stays true.
inline SimIntParams makeSimIntParams(bool emit, bool reset, uint32_t frame, uint32_t emitCount,
                                     uint32_t poolCount) {
  SimIntParams I{};
  I.TriggerEmit = emit ? 1 : 0;
  I.TriggerReset = reset ? 1 : 0;
  // Advance one emit-block per frame; the shader takes (gi + cyc) % poolCount, so this is
  // the rolling write head. int32 wrap after ~years of frames is harmless (shader re-mods).
  I.CollectCycleIndex = (int32_t)((uint64_t)frame * (uint64_t)emitCount % (poolCount ? poolCount : 1));
  I.SetFx1To = 0;
  I.SetFx2To = 0;
  I.EmitMode = 0;
  I.IsAutoCount = 0;  // TiXL default (Max>=0): age particles out so the cycle recycles
  I.EmitVelocityFactor = 0;
  I.EmitCount = (int32_t)emitCount;        // newPointCount = the full emit ring (sampled fully)
  I.MaxParticleCount = (int32_t)poolCount;  // pool capacity (> emitCount -> cycle rotates)
  return I;
}
#endif  // !__METAL_VERSION__


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
// .../points/generate/RadialPoints.hlsl (spiral around an axis). Every TiXL input is now a
// live inspector knob (param-completion gate): the former baked-default fields (Axis,
// GainAndBias, CloseCircle, Scale, F1/F2, Color, Orientation*) are real struct members the
// cook fills from the NodeSpec and the kernel reads.
// ALL-SCALAR layout (no packed_float3): every TiXL Vector is flattened to consecutive floats
// so there is zero cbuffer alignment trap (the established convention for this struct; the
// kernel reassembles float3(X,Y,Z) on read). 32 floats == 128 bytes (16-byte multiple).
struct RadialParams {
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float Radius;
  float RadiusOffset;
  float StartAngle;  // degrees
  float Cycles;      // turns around the axis (TiXL "Cycles"/Rotations)
  float ScaleBase;   // TiXL PointScaleRange.x (Scale.x)
  float ScaleByF;    // TiXL PointScaleRange.y (Scale.y; × normalized index)
  float CenterX;     // TiXL Center (Vector3) — translation added to every point.
  float CenterY;
  float CenterZ;
  float AxisX;       // TiXL Axis (Vector3) — spiral axis (.t3 default +Z).
  float AxisY;
  float AxisZ;
  float OffsetCenterX;  // TiXL OffsetCenter (CenterOffset) — Center += OffsetCenter·f.
  float OffsetCenterY;
  float OffsetCenterZ;
  float GainX;       // TiXL GainAndBias.x (gain)  — ApplyGainAndBias remaps f (.t3 default 0.5).
  float GainY;       // TiXL GainAndBias.y (bias)
  float CloseCircle; // TiXL CloseCircleLine (bool→float; >0.5). angleStepCount + NaN terminator.
  float F1Base;      // TiXL F1.x  — FX1 = F1.x + F1.y·f (.t3 default F1=(1,0)).
  float F1ByF;       // TiXL F1.y
  float F2Base;      // TiXL F2.x  — FX2 = F2.x + F2.y·f (.t3 default F2=(1,0)).
  float F2ByF;       // TiXL F2.y
  float ColorR;      // TiXL Color (Vector4) — per-point color (.t3 default white).
  float ColorG;
  float ColorB;
  float ColorA;
  float OrientAxisX;  // TiXL OrientationAxis (Vector3) (.t3 default +Z).
  float OrientAxisY;
  float OrientAxisZ;
  float OrientAngle;  // TiXL OrientationAngle (degrees).
  float OrientMode;   // TiXL OrientationMode (enum→float; <0.5 Classic, else AlignedToCurvature).
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
static_assert(sizeof(EmitParams) == 16, "EmitParams 16 bytes");
static_assert(sizeof(RadialParams) == 128, "RadialParams 128 bytes (32 floats, all-scalar)");
#endif
