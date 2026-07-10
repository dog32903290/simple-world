#pragma once
// mathv_ref_simulatepoints — CPU scalar oracle for TiXL points/sim/simulate-points (points island;
// owner: external/tixl/Operators/Lib/point/sim/_legacy/_LegacySimForwardMovement.t3 — a legacy
// standalone forward-movement integrator, distinct from sw's existing `particle_sim` kernel which
// ports the different external/tixl .../particles/ParticleSystem.hlsl — see
// runtime/simulatepoints_params.h header note for why these are NOT duplicates).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/points/sim/simulate-points.hlsl — NOT derived from sw's MSL kernel
// (app/shaders/simulatepoints.metal intentionally never opened while writing this file).
//   cbuffer Params (Drag/Speed) :6-10
//   main() body                  :14-36
//
// GetDimensions guard (:17-20 `if(i.x>=numStructs) return;`) is modeled via the `count` parameter
// passed to simulatePointsOne/mathvRefSimulatePoints below (host-ABI substitution, matching the
// kernel adapter's §10.5① treatment — same semantics, just expressed as an explicit count arg on the
// CPU side instead of a buffer-size lookup).
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
// Only app/src/runtime/tixl_point.h is included, for the Particle host struct (data layout, not math).
#include "runtime/tixl_point.h"

#include <cstddef>
#include <cstdint>

namespace sw {
namespace mathv_ref {

// simulatePointsOne — one thread of `main()` (:14-36), operating on a single particle.
//   :29 -- Position += Velocity * 0.01 * Speed
//   :31 -- Velocity *= (1 - Drag)
// Every other field (Radius/Rotation/Color/BirthTime) passes through UNCHANGED (the HLSL reads them
// into locals :23 then writes them straight back :34 verbatim, a faithful round-trip -- not dead
// code, TiXL's literal source shape).
inline void simulatePointsOne(const Particle& in, Particle& out, float drag, float speed) {
  out.Position.x = in.Position.x + in.Velocity.x * 0.01f * speed;  // :29
  out.Position.y = in.Position.y + in.Velocity.y * 0.01f * speed;
  out.Position.z = in.Position.z + in.Velocity.z * 0.01f * speed;
  out.Velocity.x = in.Velocity.x * (1.0f - drag);  // :31
  out.Velocity.y = in.Velocity.y * (1.0f - drag);
  out.Velocity.z = in.Velocity.z * (1.0f - drag);
  out.Radius = in.Radius;      // :23/:34 round-trip, unchanged
  out.Rotation = in.Rotation;  // :24-27 the commented-out q_separate_v/qRotateVec3/q_encode_v lines
                                // are dead HLSL source (never executed) -- Rotation passes through.
  out.Color = in.Color;
  out.BirthTime = in.BirthTime;
}

// mathvRefSimulatePoints — full-buffer CPU oracle over `count` dispatch threads (the GetDimensions
// guard's numStructs, :17-20). Threads with idx>=count are OUT OF SCOPE for a host array sized to
// exactly `count` particles (same AMBIGUITY treatment as mathv_ref_wrappointposition.h's :48-52 note
// — a real GPU dispatch pads to numthreads(64,1,1) and relies on the guard; a host-sized array has no
// padding slot to write into).
inline void mathvRefSimulatePoints(const Particle* in, Particle* out, size_t count, float drag,
                                    float speed) {
  for (size_t i = 0; i < count; ++i) {
    simulatePointsOne(in[i], out[i], drag, speed);
  }
}

}  // namespace mathv_ref
}  // namespace sw
