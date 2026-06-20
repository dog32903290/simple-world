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
// closed/pointsPerShape: DrawClosedLines (TiXL DrawLinesAlt.hlsl GetWrappedIndex) — closed=0 +
// pointsPerShape=0 is the open DrawLines path (the wrap modulo is dead → byte-identical). They
// occupy what were _pad0/_pad1 (the struct stays 32 bytes; old DrawLines callers leave them 0).
struct DrawLineParams {
#ifdef __METAL_VERSION__
  float4 color;
#else
  float color[4];
#endif
  float lineWidth;
  float viewExtent;
  uint  closed;          // 1 = wrap last→first (DrawClosedLines); 0 = open polyline (DrawLines)
  uint  pointsPerShape;  // >0 = per-shape closed loops of this many points; 0 = one shape (all pts)
  // -> 32 bytes (16-byte multiple)
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

// DrawQuadXf (Layer2d) Params cbuffer (b0), 1:1 with draw-Quad-vs.hlsl's Transforms(b0)+Params(b1)
// COLLAPSED into one struct: the xf VS reads ONLY ObjectToClipSpace (F3 — the other 9
// TransformBufferLayout matrices are dead for Layer2d, NOT carried), Color/Width/Height for the quad,
// clampMax for the PS clamp, and `applyTransform` for the drop-mul golden tooth. Layout mirrors
// DrawScreenQuadParams (so the SHARED draw_screenquad_fs reads color/clampMax at the same offsets)
// with the matrix appended. ROW-MAJOR float[16] (m[r*4+c]); the MSL VS rebuilds its float4x4 so
// `mul4(M,v)` == `v·M_rowmajor` (field_camera convention, NO transpose).
// 48 (DrawScreenQuad prefix) + 64 (mat) + 16 (applyTransform+pad) = 128 bytes (16-byte multiple).
struct DrawQuadXfParams {
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
  float4 clampMax;
  // ROW-MAJOR float[16] (m[r*4+c]) — NOT a float4x4 (avoids MSL's column-major reinterpret). The VS
  // does the row-vector multiply by hand (mul4row), identical to field_raymarch_template's mul4, so
  // `mul4row(M,v) == v·M_rowmajor` (field_camera convention, NO transpose anywhere).
  float objectToClipSpace[16];
  uint applyTransform;  // 1 = apply the mul; 0 = raw clip (drop-mul golden tooth)
  uint _pad0;
  uint _pad1;
  uint _pad2;  // -> 16-byte tail
#else
  float clampMax[4];
  float objectToClipSpace[16];  // row-major m[r*4+c]
  uint32_t applyTransform;
  uint32_t _pad0;
  uint32_t _pad1;
  uint32_t _pad2;  // -> 16-byte tail
#endif
};

// DrawPoints2 cbuffer (b0) — TiXL DrawPoints2 → DrawPoints.hlsl. color = tint (Color, .t3 white);
// pointSize = the shader's PointSize (host pre-multiplies Radius*10.8, the .t3 Multiply chain);
// viewExtent = ortho half-extent; useWForSize = TiXL UseWForSize (1 → sprite scaled by Point.FX1=W,
// the ScaleFX==1 path; 0 → factor 1). DrawPoints2 rides DrawKind::Points2 (its own shader/PSO) so
// the v1 DrawPoints path (DrawKind::Points, bare float viewExtent binding) stays byte-identical.
struct DrawPoint2Params {
#ifdef __METAL_VERSION__
  float4 color;
#else
  float color[4];
#endif
  float pointSize;   // = Radius * 10.8 (host MultiplyInt chain)
  float viewExtent;
#ifdef __METAL_VERSION__
  uint  useWForSize; // 1 = scale sprite by Point.FX1 (W); 0 = factor 1
#else
  uint32_t useWForSize;
#endif
  float _pad0;       // -> 32 bytes (16-byte multiple)
};

// DrawLinesBuildup cbuffer (b0) — TiXL DrawLinesBuildup → DrawLinesBuildup.hlsl. color = tint
// (Color, .t3 white); lineWidth = band width (LineWidth, .t3 0.02); viewExtent = ortho half-extent;
// transitionProgress = TiXL TransitionProgress (.t3 0.5; OffsetU = transitionProgress - 0.01 in the
// shader); visibleRange = TiXL VisibleRange (.t3 0.5, the reveal-window width). The reveal alpha is
// computed in the FS from each vert's wAtPoint (Point.FX1). Rides DrawKind::LinesBuildup (its own
// shader/PSO) so DrawLines / DrawClosedLines (DrawKind::Lines) stay byte-identical.
struct DrawLineBuildupParams {
#ifdef __METAL_VERSION__
  float4 color;
#else
  float color[4];
#endif
  float lineWidth;
  float viewExtent;
  float transitionProgress;  // TiXL TransitionProgress (OffsetU = this - 0.01)
  float visibleRange;        // TiXL VisibleRange (reveal-window width)
  // -> 32 bytes (16-byte multiple)
};

enum DrawLineBinding {
  DRAWLINE_Points = 0,  // device const SwPoint* (vertex buffer)
  DRAWLINE_Params = 1,  // constant DrawLineParams&
};

enum DrawPoint2Binding {
  DRAWPOINT2_Points = 0,  // device const SwPoint* (vertex buffer)
  DRAWPOINT2_Params = 1,  // constant DrawPoint2Params&
};

enum DrawLineBuildupBinding {
  DRAWLINEBU_Points = 0,  // device const SwPoint* (vertex buffer)
  DRAWLINEBU_Params = 1,  // constant DrawLineBuildupParams& (vertex + fragment)
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

// DrawQuadXf (Layer2d) bindings — same single-cbuffer shape as DrawScreenQuad (VS reads no vertex
// buffer; 6 hardcoded object-space quad verts by vertex_id). The xf VS + the shared FS both bind
// DrawQuadXfParams at slot 0; texture(0)/sampler(0) mirror the HLSL t0/s0.
enum DrawQuadXfBinding {
  DRAWQUADXF_Params = 0,  // constant DrawQuadXfParams& (vertex + fragment)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DrawPoint2Params) == 32, "DrawPoint2Params 32 bytes");
static_assert(sizeof(DrawLineBuildupParams) == 32, "DrawLineBuildupParams 32 bytes");
static_assert(sizeof(DrawLineParams) == 32, "DrawLineParams 32 bytes");
static_assert(sizeof(DrawBillboardParams) == 32, "DrawBillboardParams 32 bytes");
static_assert(sizeof(DrawScreenQuadParams) == 48, "DrawScreenQuadParams 48 bytes");
static_assert(sizeof(DrawQuadXfParams) == 128, "DrawQuadXfParams 128 bytes");
#endif
