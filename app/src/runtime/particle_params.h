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

// DirectionalForce params — mirrors DirectionalForce.hlsl cbuffer Params (b0):
//   float3 Direction; float Amount; float RandomAmount; float SpeedFactor;
// + Count (no GetDimensions in MSL). Direction is split into 3 scalars (Dir{X,Y,Z}) — same
// all-scalar discipline as RadialParams: NO packed_float3 in a cbuffer, so zero alignment
// traps; the shader reassembles float3(DirX,DirY,DirZ). The .hlsl declares Direction first,
// but cbuffer field ORDER is irrelevant once both sides agree on this struct (it IS the
// contract); we lead with the scalars so the layout reads like TurbParams. NO field path
// (DirectionalForce.hlsl has none) — a plain directional push.
struct DirForceParams {
  float DirX;
  float DirY;
  float DirZ;
  float Amount;
  float RandomAmount;
  float SpeedFactor;
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float _pad;  // -> 32 bytes
};

// VectorFieldForce params — mirrors VectorFieldForce-sg.hlsl cbuffer Params (b0):
//   float Amount; float Variation;  (+ ParticleCount from b2). NO ShaderGraph field bound
// (fork-VFF, see below): GetField() returns f=1, so the field vector is the constant (1,1,1)
// and field.w==1. The shader bakes that default; Amount/Variation/SpeedFactor stay live.
struct VecFieldForceParams {
  float Amount;
  float Variation;
  float SpeedFactor;
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
};

// VelocityForce params — mirrors TiXL VelocityForce.hlsl cbuffer Params (b0):
//   float Amount; float Acelleration; float MinSpeed; float MaxSpeed;
//   float Variation; float2 VariationGainAndBias;
// (external/tixl .../particle/force/VelocityForce.cs + .../shaders/particles/VelocityForce.hlsl.)
// Reads each particle's CURRENT velocity, rescales its SPEED (magnitude) along its existing
// direction, clamping to [MinSpeed,MaxSpeed]. Pure value params, no field/buffer. +Count
// (no GetDimensions in MSL). float2 VariationGainAndBias carried as 2 scalars (no vec2 in a
// cbuffer = zero alignment traps); the shader reassembles float2(GainBiasX,GainBiasY).
struct VelForceParams {
  float Amount;
  float Accelerate;   // .hlsl cbuffer field "Acelleration" (TiXL's spelling); .cs slot "Accelerate"
  float MinSpeed;
  float MaxSpeed;
  float Variation;
  float GainBiasX;    // VariationGainAndBias.x
  float GainBiasY;    // VariationGainAndBias.y
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
};

// AxisStepForce params — mirrors TiXL AxisStepForce.hlsl cbuffer Params (b0):
//   float ApplyTrigger; float Strength; float RandomizeStrength; float SelectRatio;
//   float3 AxisDistribution; float AddOriginalVelocity;
//   float3 StrengthDistribution; float Seed; float AxisSpace;
// (external/tixl .../particle/force/AxisStepForce.cs + .../shaders/particles/AxisStepForce.hlsl.)
// hash41u(index + Seed*k) per-particle picks a random dominant axis + signed strength; SelectRatio
// gates which particles get hit; velocity is lerp'd toward (origVel*AddOriginalVelocity + dir*f).
// Pure value params + per-particle hash, stateless (no sim buffer). Vec3 inputs carried as 3
// scalars each (no packed in a cbuffer). AxisSpace int mode: 0=ObjectSpace, 1=RotationSpace
// (rotates the chosen axis by the particle's own Rotation quat — uses Particle.Rotation, already
// in the shared buffer, so still stateless). Seed/AxisSpace are float here (host casts int->float;
// the .hlsl reads them as float and casts (uint)Seed / compares AxisSpace<0.5,<1.5).
struct AxisStepForceParams {
  float ApplyTrigger;
  float Strength;
  float RandomizeStrength;
  float SelectRatio;
  float AxisDistributionX;
  float AxisDistributionY;
  float AxisDistributionZ;
  float AddOriginalVelocity;
  float StrengthDistributionX;
  float StrengthDistributionY;
  float StrengthDistributionZ;
  float Seed;
  float AxisSpace;
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float _pad0;
  float _pad1;  // -> 64 bytes (16-byte multiple)
};

// SnapToAnglesForce params — mirrors TiXL SnapOrientationForce.hlsl cbuffer Params (b0) AND the
// SnapToAnglesForce.t3 input->cbuffer wiring (the .cs slot names differ from the .hlsl cbuffer
// field names; the .t3 FloatsToBuffer connection ORDER is the authority — see NAMED FORK in
// snaptoanglesforce.metal). Positional map (FloatsToBuffer order in SnapToAnglesForce.t3):
//   [0] Amount             -> Amount
//   [1] AngleCount         -> SnapAngle     (degrees per step; subdivisions = 360/AngleCount)
//   [2] Twist              -> PhaseAngle    (degrees, added to the aligned angle every frame)
//   [3] Variation          -> Variation
//   [4] VariationThreshold -> VariationRatio
//   [5] KeepPlanar         -> KeepPlanar
//   [6] Mode(int->float)   -> SpaceAndPlane (0=CameraSpace,1=WorldXY,2=WorldXZ,3=WorldYZ)
// + RandomSeed (b1 in .hlsl); + Count. Quantizes each particle's velocity DIRECTION on a plane to
// the nearest of (360/AngleCount) discrete angles, lerp'd by Amount. Pure value params + per-
// particle hash41u, stateless.
struct SnapAnglesForceParams {
  float Amount;
  float SnapAngle;       // = AngleCount (.cs)
  float PhaseAngle;      // = Twist (.cs), in degrees
  float Variation;
  float VariationRatio;  // = VariationThreshold (.cs)
  float KeepPlanar;
  float SpaceAndPlane;   // = Mode (.cs): 0..3
  float RandomSeed;      // .hlsl b1 (host casts int->float; shader casts (uint))
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float _pad0;
  float _pad1;
  float _pad2;  // -> 48 bytes (16-byte multiple)
};

enum ForceBinding {
  FORCE_Particles = 0,  // device Particle* (u0) — shared by all force kernels
  FORCE_Params = 1,     // constant {Turb,DirForce,VecFieldForce,Vel,AxisStep,SnapAngles}Params& (b0)
  // PF-a: assembled FieldParams buffer for the runtime-compiled VectorFieldForce compute kernel
  // (the field-into-force bridge). Distinct slot from FORCE_Params so the force's b0 (Amount/
  // Variation) and the field's packed param buffer never overwrite each other; the field op's
  // members carry the "ToroidalVortexField_<id>_" prefix so there is no name collision either.
  // Only the runtime-compiled vector_field_force_template kernel binds this; the baked
  // vector_field_force.metal (fork-VFF, no field) never declares it -> byte-identical.
  FORCE_FieldParams = 2,
};

// Force-op discriminator. A ParticleForce node carries a pinless `_ForceKind` Float whose spec
// default tags which kernel cookParticleSim dispatches for the wired force (the cook ctx hides
// node TYPE on purpose — ops are graph-agnostic — so the kind travels through the param map,
// the only channel available). Absent/0 -> Turbulence keeps every pre-existing graph correct.
enum ForceKind {
  FORCE_KIND_TURBULENCE = 0,
  FORCE_KIND_DIRECTIONAL = 1,
  FORCE_KIND_VECTORFIELD = 2,
  // 批次24 — appended (NOT inserted: pre-existing .swproj _ForceKind overrides keep their meaning).
  FORCE_KIND_VELOCITY = 3,    // VelocityForce
  FORCE_KIND_AXISSTEP = 4,    // AxisStepForce
  FORCE_KIND_SNAPANGLES = 5,  // SnapToAnglesForce
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
static_assert(sizeof(DirForceParams) == 32, "DirForceParams 32 bytes");
static_assert(sizeof(VecFieldForceParams) == 16, "VecFieldForceParams 16 bytes");
static_assert(sizeof(VelForceParams) == 32, "VelForceParams 32 bytes");
static_assert(sizeof(AxisStepForceParams) == 64, "AxisStepForceParams 64 bytes");
static_assert(sizeof(SnapAnglesForceParams) == 48, "SnapAnglesForceParams 48 bytes");
static_assert(sizeof(EmitParams) == 16, "EmitParams 16 bytes");
static_assert(sizeof(RadialParams) == 48, "RadialParams 48 bytes");
#endif
