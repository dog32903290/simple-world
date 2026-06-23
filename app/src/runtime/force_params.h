// force_params.h — shared host<->shader params for the TiXL-ported ParticleForce kernels.
// PEELED out of particle_params.h when FieldVolumeForce landed, to keep particle_params.h ≤400 lines
// (ARCHITECTURE.md rule 4; the cap was reached at 399 after RandomJumpForce). Holds ONLY the force-pass
// structs + the shared ForceBinding slots + the ForceKind discriminator. particle_params.h #includes this,
// so every existing `#include "particle_params.h"` (both .cpp and the precompiled .metal kernels, which get
// it via `-I src/runtime`) still resolves all force structs + FORCE_* bindings — zero caller edits.
//
// Each struct mirrors the corresponding TiXL .hlsl cbuffer Params (b0) + Count (MSL has no
// StructuredBuffer.GetDimensions(); the host supplies the count). Vec inputs are carried as scalars (no
// packed_float3/float2 in a cbuffer = zero alignment traps); the shader reassembles them.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

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

// FieldDistanceForce params — mirrors TiXL FieldDistanceForce.hlsl cbuffer Params (b0):
//   float Amount; float Attraction; float Repulsion; float NormalSamplingDistance;
//   float DecayWithDistance;
// (external/tixl .../particle/force/FieldDistanceForce.cs + .../shaders/particles/FieldDistanceForce.hlsl.)
// Samples the WIRED SDF field's distance .w at each particle's raw Position, computes a finite-difference
// surface normal (GetFieldNormal: 4x GetField().w taps offset by NormalSamplingDistance), then pushes the
// particle along that normal: d>0 (outside) attract toward the surface scaled by pow(d+1,-DecayWithDistance);
// d<=0 (inside) repel away. NaN guards on d and n preserved. The field arrives via FORCE_FieldParams (the
// PF-a bridge) exactly like VectorFieldForce; with no field wired the kernel sees the baked seed (.w=1) and
// degenerates (baked fallback). +Count (no GetDimensions in MSL). Pure value params + the wired field.
struct FieldDistForceParams {
  float Amount;
  float Attraction;
  float Repulsion;
  float NormalSamplingDistance;
  float DecayWithDistance;
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float _pad0;
  float _pad1;  // -> 32 bytes (16-byte multiple)
};

// RandomJumpForce params — mirrors RandomJumpForceTemplate.hlsl cbuffer Params (b0). Full rationale + the
// param-mapping discipline + NAMED FORK fork-RandomJump-position-write in random_jump_force_template.metal.
struct RandomJumpForceParams {
  float Amount;
  float Frequency;
  float Phase;
  float Variation;
  float AmountDistributionX;  // = DirectionDistribution.x (.cs); Y/Z follow
  float AmountDistributionY;
  float AmountDistributionZ;
  uint32_t Count;  // no METAL guard: struct IS in metallib glob but bare uint32_t==uint in MSL (4B, static_assert holds)
};

// FieldVolumeForce params — mirrors TiXL FieldVolumeForce.hlsl b1 cbuffer Params (the FORCE params), in the
// .t3 FloatsToBuffer connection ORDER (traced, NOT assumed 1:1 — see field_volume_force_template.metal for
// the routing forks). Reflects the velocity off a wired SDF surface on a boundary crossing (bounce), else
// attracts (outside) / repels (inside). The field arrives via FORCE_FieldParams (the PF bridge); with no
// field wired the baked seed (.w=1) degenerates to a NaN-guarded no-op (fork-FieldVolume-baked,
// field_volume_force.metal). Three of these slots are NOT 1:1 with the .cs inputs (the cook applies the
// fork host-side so the kernel reads ready values):
//   Attraction          = (.cs Attraction) * 0.425   (Multiply node on the Attraction path)
//   InvertVolumeFactor  = InvertVolume ? -1 : +1      (BoolToFloat node)
//   SpeedFactor         = 1.0                         (fork-FieldVolume-speedfactor: runtime PS value, not an input)
// EnableBounce/ApplyColorOnCollision are b2 ints in the .hlsl, carried here as 0/1 floats (kernel compares !=0).
// All-scalar (no packed in a cbuffer). +Count (no GetDimensions in MSL).
struct FieldVolumeForceParams {
  float Amount;
  float Attraction;             // = (.cs Attraction) * 0.425
  float AttractionDecay;
  float Repulsion;
  float Bounciness;
  float RandomizeBounce;
  float RandomizeReflection;
  float InvertVolumeFactor;     // = InvertVolume ? -1 : +1
  float NormalSamplingDistance;
  float SpeedFactor;            // = 1.0 (fork-FieldVolume-speedfactor)
  float EnableBounce;           // = ReflectOnCollision ? 1 : 0 (b2 int as float)
  float ApplyColorOnCollision;  // = ApplyColorOnCollision ? 1 : 0 (b2 int as float)
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float _pad0;
  float _pad1;
  float _pad2;  // -> 64 bytes (16-byte multiple)
};

enum ForceBinding {
  FORCE_Particles = 0,  // device Particle* (u0) — shared by all force kernels
  FORCE_Params = 1,     // constant {Turb,DirForce,VecField,Vel,AxisStep,SnapAngles,FieldDist,RandomJump,FieldVolume}Params& (b0)
  // PF-a: assembled FieldParams buffer for the runtime-compiled field-into-force compute kernels (VFF/
  // FieldDistance/RandomJump/FieldVolume). Distinct slot from FORCE_Params so the force's b0 and the field's
  // packed param buffer never overwrite each other; the field op's members carry an id prefix so there is no
  // name collision either. Only the runtime-compiled *_template kernels bind this; the baked fallbacks
  // (fork-VFF / fork-FieldDistance-baked / fork-FieldVolume-baked) never declare it -> byte-identical.
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
  // PF-bridge field-distance — appended (NOT inserted: pre-existing .swproj _ForceKind overrides keep
  // their meaning, append-only discipline).
  FORCE_KIND_FIELDDISTANCE = 6,  // FieldDistanceForce (SDF-distance attract/repel via FORCE_FieldParams)
  // RandomJumpForce — appended (NOT inserted; append-only). Writes POSITION (fork-RandomJump-position-write).
  FORCE_KIND_RANDOMJUMP = 7,  // RandomJumpForce (curlNoise jump modulated by field color via FORCE_FieldParams)
  // FieldVolumeForce — appended (NOT inserted; append-only). Reflects velocity off the SDF surface (bounce)
  // / attracts / repels via FORCE_FieldParams (fork-FieldVolume-baked when no field wired).
  FORCE_KIND_FIELDVOLUME = 8,
};

#ifndef __METAL_VERSION__
static_assert(sizeof(TurbParams) == 32, "TurbParams 32 bytes");
static_assert(sizeof(DirForceParams) == 32, "DirForceParams 32 bytes");
static_assert(sizeof(VecFieldForceParams) == 16, "VecFieldForceParams 16 bytes");
static_assert(sizeof(VelForceParams) == 32, "VelForceParams 32 bytes");
static_assert(sizeof(AxisStepForceParams) == 64, "AxisStepForceParams 64 bytes");
static_assert(sizeof(SnapAnglesForceParams) == 48, "SnapAnglesForceParams 48 bytes");
static_assert(sizeof(FieldDistForceParams) == 32, "FieldDistForceParams 32 bytes");
static_assert(sizeof(RandomJumpForceParams) == 32, "RandomJumpForceParams 32 bytes");
static_assert(sizeof(FieldVolumeForceParams) == 64, "FieldVolumeForceParams 64 bytes");
#endif
