// TiXL ParticleSystem integrator, ported 1:1 from
// external/tixl .../particles/ParticleSystem.hlsl. emit (cycle buffer) -> drag
// -> integrate -> orient-to-velocity -> copy to ResultPoints -> lifetime.
#include <metal_stdlib>
#include "tixl_point.h"         // Point, Particle (64B, packed_float3)
#include "particle_params.h"    // SimParams, SimIntParams, SimBinding
#include "shared/quat.metal.h"  // qRotateVec3, qSlerp, qLookAt
using namespace metal;

#define W_KEEP_ORIGINAL 0
#define W_PARTICLE_AGE 1
#define W_PARTICLE_SPEED 2

kernel void particle_sim(const device SwPoint*  EmitPoints   [[buffer(SIM_EmitPoints)]],
                         device Particle*        Particles    [[buffer(SIM_Particles)]],
                         device SwPoint*         ResultPoints [[buffer(SIM_ResultPoints)]],
                         constant SimParams&     P            [[buffer(SIM_Params)]],
                         constant SimIntParams&  I            [[buffer(SIM_IntParams)]],
                         uint3                   tid          [[thread_position_in_grid]]) {
  uint newPointCount = (uint)I.EmitCount;
  uint maxParticleCount = (uint)I.MaxParticleCount;
  uint gi = tid.x;
  if (gi >= maxParticleCount) return;

  if (I.TriggerReset > 0) {
    Particles[gi].BirthTime = NAN;
    Particles[gi].Position = float3(NAN);
    ResultPoints[gi].Scale = float3(NAN);
  }

  // Insert emit points (cycle buffer).
  int addIndex = 0;
  uint cyc = (uint)I.CollectCycleIndex;
  if (I.EmitMode == 0) {
    addIndex = (int)((gi + cyc + maxParticleCount) % maxParticleCount);
  } else {
    int t = (int)((gi + cyc / newPointCount) % maxParticleCount);
    int blockSize = (int)(maxParticleCount / newPointCount);
    int particleBlock = t / blockSize;
    int t2 = t - (particleBlock * blockSize);
    addIndex = t2 > 0 ? -1 : particleBlock;
  }

  if (I.TriggerEmit != 0 && addIndex >= 0 && addIndex < (int)newPointCount) {
    if (I.EmitMode != 0) {
      Particles[(gi - 1) % maxParticleCount].BirthTime = NAN;
      Particles[(gi - 1) % maxParticleCount].Radius = NAN;
    }
    SwPoint emitPoint = EmitPoints[addIndex];
    Particles[gi].Position = emitPoint.Position;
    Particles[gi].Rotation = emitPoint.Rotation;
    Particles[gi].Radius = emitPoint.Scale.x * P.RadiusFromW;
    Particles[gi].BirthTime = P.Time;

    float emitVelocity = P.InitialVelocity *
        (I.EmitVelocityFactor == 0 ? 1.0f : (I.EmitVelocityFactor == 1 ? emitPoint.FX1 : emitPoint.FX2));
    Particles[gi].Velocity = qRotateVec3(float3(0, 0, 1), normalize(Particles[gi].Rotation)) * emitVelocity;

    // These will not change over lifetime.
    Particles[gi].Color = emitPoint.Color;
    ResultPoints[gi].Scale = emitPoint.Scale;
    ResultPoints[gi].FX1 = emitPoint.FX1;
    ResultPoints[gi].FX2 = emitPoint.FX2;
    ResultPoints[gi].Color = emitPoint.Color;
  }

  // Faithful to TiXL: (x == NAN) is always false, so this guard is a no-op.
  // Un-emitted particles are hidden via Scale = NAN (reset / tooOld), not here.
  if (Particles[gi].BirthTime == NAN) return;

  float3 velocity = Particles[gi].Velocity;
  velocity *= pow(1.0f - P.Drag, P.Speed);  // frame-rate-aligned drag
  Particles[gi].Velocity = velocity;

  float3 pos = Particles[gi].Position;
  pos += velocity * P.Speed * 0.01f;
  Particles[gi].Position = pos;

  float speed = length(velocity);
  if (speed > 0.0001f) {
    float f = saturate(speed * P.OrientTowardsVelocity);
    Particles[gi].Rotation = qSlerp(Particles[gi].Rotation, qLookAt(velocity / speed, float3(0, 1, 0)), f);
  }

  ResultPoints[gi].Position = Particles[gi].Position;
  ResultPoints[gi].Rotation = Particles[gi].Rotation;
  ResultPoints[gi].Color = Particles[gi].Color;

  float lifeTime = P.LifeTime < 0.0f
                       ? (I.IsAutoCount ? 100000.0f : (float)(maxParticleCount / (newPointCount * 60.0f)))
                       : P.LifeTime;
  float normalizedAge =
      (I.IsAutoCount && P.LifeTime < 0.0f) ? 1.0f : (P.Time - Particles[gi].BirthTime) / lifeTime;
  bool tooOld = normalizedAge > 1.0f;

  if (I.SetFx1To == W_PARTICLE_AGE)
    ResultPoints[gi].FX1 = normalizedAge;
  else if (I.SetFx1To == W_PARTICLE_SPEED)
    ResultPoints[gi].FX1 = speed * 100.0f;

  if (I.SetFx2To == W_PARTICLE_AGE)
    ResultPoints[gi].FX2 = normalizedAge;
  else if (I.SetFx2To == W_PARTICLE_SPEED)
    ResultPoints[gi].FX2 = speed * 100.0f;

  if (tooOld) ResultPoints[gi].Scale = float3(NAN);
}
