// Shared host<->shader params for the TiXL-ported SpherePoints generator (lane A).
// Mirrors the cbuffer Params (b0) in external/tixl
// .../Assets/shaders/points/generate/SpherePoints.hlsl:
//   float3 Center; float Radius; float StartAngle; float Scatter;
//
// All-scalar layout (NO packed_float3) — the particle_params.h discipline. TiXL's
// Center (Vector3) is laid out as CenterX/Y/Z scalars; the shader reassembles
// float3(CenterX,Y,Z). Pad to a 16-byte multiple and static_assert the size so the
// cbuffer has zero alignment traps.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params : register(b0) — SpherePoints.hlsl
struct SphereParams {
#ifdef __METAL_VERSION__
  uint Count;        // TiXL Count (MSL has no GetDimensions(); host supplies it)
#else
  uint32_t Count;
#endif
  float Radius;      // TiXL Radius — sphere radius; every point sits at this distance from Center
  float StartAngle;  // TiXL StartAngle (degrees) — rotates the spiral
  float Scatter;     // TiXL Scatter — per-point angular jitter via hash11
  float CenterX;     // TiXL Center (Vector3) — sphere center. All-scalar (no packed_float3)
  float CenterY;     // so there are zero cbuffer alignment traps; the shader reassembles
  float CenterZ;     // float3(CenterX,Y,Z).
  float _pad0;       // -> 32 bytes (16-byte multiple, like Sim/TurbParams)
};

enum SphereBinding {
  SPHERE_Points = 0,  // device SwPoint* (u0)
  SPHERE_Params = 1,  // constant SphereParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SphereParams) == 32, "SphereParams 32 bytes");
#endif
