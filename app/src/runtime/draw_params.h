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

enum DrawLineBinding {
  DRAWLINE_Points = 0,  // device const SwPoint* (vertex buffer)
  DRAWLINE_Params = 1,  // constant DrawLineParams&
};

enum DrawBillboardBinding {
  DRAWBB_Points = 0,  // device const SwPoint* (vertex buffer)
  DRAWBB_Params = 1,  // constant DrawBillboardParams&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DrawLineParams) == 32, "DrawLineParams 32 bytes");
static_assert(sizeof(DrawBillboardParams) == 32, "DrawBillboardParams 32 bytes");
#endif
