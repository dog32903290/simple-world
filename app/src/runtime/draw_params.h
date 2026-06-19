// Shared host<->shader params for the screen-space draw ops DrawLines / DrawBillboards
// (batch 13 lane L). Parallels particle_params.h: one struct per shader cbuffer, 16-byte
// aligned (zero packed_float3 → zero alignment traps), included by both .metal and .cpp so
// the compiler proves the layout. The executor (cookRenderTarget) fills these from a
// RenderDrawItem's draw params and binds them per draw.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// DrawLines cbuffer (b0). color = tint (TiXL DrawLines.Color, .t3 default white); lineWidth =
// world-space band width (TiXL LineWidth, .t3 default 0.02); viewExtent = ortho half-extent.
struct DrawLineParams {
#ifdef __METAL_VERSION__
  float4 color;
#else
  float color[4];
#endif
  float lineWidth;
  float viewExtent;
  float _pad0;
  float _pad1;  // -> 32 bytes (16-byte multiple)
};

// DrawBillboards cbuffer (b0). color = tint (TiXL Color); size = sprite scale (TiXL Scale, .t3
// default 1.0); viewExtent = ortho half-extent.
struct DrawBillboardParams {
#ifdef __METAL_VERSION__
  float4 color;
#else
  float color[4];
#endif
  float size;
  float viewExtent;
  float _pad0;
  float _pad1;  // -> 32 bytes
};

// DrawScreenQuad Params cbuffer (b0), 1:1 with vs-draw-viewport-quad.hlsl's Params register(b0):
//   float4 Color; float2 Position; float Width; float Height;
// VS sizes/places a clip-space quad by Width/Height/Position; PS tints the sampled texture by
// Color and HDR-clamps to [0, clampMax]. clampMax is the TiXL HDR-permissive constant
// float4(1000,1000,1000,1) (the cook path ALWAYS sets it to that — it is not a node input);
// it lives in the cbuffer so the clamp golden can drive the real shader's clamp ceiling and a
// -bug injection can move it (proving the shader clamp, not a flipped expected value).
// 48 bytes: float4 + float2 + 2*float + float4 = 48 (16-byte multiple).
struct DrawScreenQuadParams {
#ifdef __METAL_VERSION__
  float4 color;
  float2 position;
#else
  float color[4];
  float position[2];
#endif
  float width;
  float height;
#ifdef __METAL_VERSION__
  float4 clampMax;  // HDR clamp upper bound (TiXL constant (1000,1000,1000,1))
#else
  float clampMax[4];
#endif
};

enum DrawLineBinding {
  DRAWLINE_Points = 0,  // device const SwPoint* (vertex buffer)
  DRAWLINE_Params = 1,  // constant DrawLineParams&
};

enum DrawBillboardBinding {
  DRAWBB_Points = 0,  // device const SwPoint* (vertex buffer)
  DRAWBB_Params = 1,  // constant DrawBillboardParams&
};

// DrawScreenQuad bindings. The VS reads no vertex buffer (6 hardcoded clip verts by vertex_id),
// only the Params cbuffer; the FS samples the source texture. texture(0)/sampler(0) mirror the
// HLSL t0/s0.
enum DrawScreenQuadBinding {
  DRAWSQ_Params = 0,  // constant DrawScreenQuadParams& (vertex + fragment)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DrawLineParams) == 32, "DrawLineParams 32 bytes");
static_assert(sizeof(DrawBillboardParams) == 32, "DrawBillboardParams 32 bytes");
static_assert(sizeof(DrawScreenQuadParams) == 48, "DrawScreenQuadParams 48 bytes");
#endif
