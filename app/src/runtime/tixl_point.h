// TiXL-faithful SwPoint / Particle layout — the binding spine of "Mac 版 TiXL".
// Ported 1:1 from external/tixl .../Assets/shaders/shared/point.hlsl.
//
// SwPoint and Particle share the SAME 64-byte stride; attributes reinterpret:
//   SwPoint.FX1   <-> Particle.Radius      (offset 12)
//   SwPoint.Scale <-> Particle.Velocity    (offset 48)
//   SwPoint.FX2   <-> Particle.BirthTime   (offset 60)
//
// CRITICAL (metal-cpp-discipline): HLSL `float3` inside a struct packs to 12
// bytes; MSL `float3` / simd `float3` are 16. To match TiXL's exact 64-byte
// stride we MUST use packed_float3 (12 bytes) for the float3 members — otherwise
// every field after offset 12 misaligns and the GPU reads garbage (no crash,
// silent corruption). Included by BOTH .metal and .cpp so the compiler proves it.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
  #define SW_PACKED3 packed_float3  // native 12-byte float3 in MSL
  #define SW_FLOAT4  float4
#else
  #include <simd/simd.h>
  #include <cstddef>
  #include <cstdint>
  // Apple's host simd has no packed_float3 (it's MSL-only). Match MSL's 12-byte
  // packed_float3 with an explicit 3-float struct so host & GPU strides agree.
  struct sw_packed3 { float x, y, z; };
  #define SW_PACKED3 sw_packed3
  #define SW_FLOAT4  simd::float4
#endif

#include "eval_context.h"  // EvaluationContext — so TUs on the tixl_point.h path
                           // (main.cpp via particle_system.h) get it without Particle.h

// Renderable point — flows through the graph as the point buffer.
struct SwPoint {
  SW_PACKED3 Position;  // @0   (12 bytes)
  float      FX1;       // @12
  SW_FLOAT4  Rotation;  // @16
  SW_FLOAT4  Color;     // @32
  SW_PACKED3 Scale;     // @48  (12 bytes)
  float      FX2;       // @60
};                      // 64

// Simulation state — internal to ParticleSystem. Same stride as SwPoint.
struct Particle {
  SW_PACKED3 Position;   // @0
  float      Radius;     // @12
  SW_FLOAT4  Rotation;   // @16
  SW_FLOAT4  Color;      // @32
  SW_PACKED3 Velocity;   // @48
  float      BirthTime;  // @60
};                       // 64

#ifndef __METAL_VERSION__
static_assert(sizeof(SwPoint) == 64, "SwPoint must match TiXL's 64-byte stride");
static_assert(sizeof(Particle) == 64, "Particle must match TiXL's 64-byte stride");
static_assert(offsetof(SwPoint, FX1) == 12, "SwPoint.FX1 @12");
static_assert(offsetof(SwPoint, Rotation) == 16, "SwPoint.Rotation @16 (float4 16-aligned)");
static_assert(offsetof(SwPoint, Color) == 32, "SwPoint.Color @32");
static_assert(offsetof(SwPoint, Scale) == 48, "SwPoint.Scale @48");
static_assert(offsetof(SwPoint, FX2) == 60, "SwPoint.FX2 @60");
static_assert(offsetof(Particle, Velocity) == 48, "Particle.Velocity @48 (== SwPoint.Scale)");
static_assert(offsetof(Particle, BirthTime) == 60, "Particle.BirthTime @60 (== SwPoint.FX2)");
#endif
