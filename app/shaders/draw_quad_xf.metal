// DrawQuadXf (Layer2d) VS — the TRANSFORMED textured quad (TiXL Layer2d → draw-Quad-vs.hlsl). The
// ONLY difference vs draw_screenquad_vs is the projection: the object-space quad vertex is multiplied
// by ObjectToClipSpace instead of drawn straight in clip space. PS is SHARED with DrawScreenQuad
// (draw_screenquad_fs, clamp(Color*tex, 0, clampMax)) — Layer2d's psMain is byte-identical to
// DrawScreenQuad's (draw-Quad-vs.hlsl:70-74).
//
// PARITY AUTHORITY: external/tixl/.../Assets/shaders/dx11/draw-Quad-vs.hlsl vsMain (LIVE, :48-58):
//   float2 quadVertex = Quad[vertexId].xy;
//   float2 quadVertexInObject = quadVertex * float2(Width, Height);
//   output.position = mul(float4(quadVertexInObject, 0, 1), ObjectToClipSpace);   // ROW-VECTOR v·M
//   output.texCoord = quadVertex * float2(0.5, -0.5) + 0.5;
// The Quad[] is IDENTICAL to draw_screenquad's kQuad (two tris, [-1,1] corners). Y-flip in texCoord
// is the same as Cut 88 (already proves clip→pixel for the no-transform path).
//
// MATRIX CONVENTION (locked, runtime/field_camera.h): ObjectToClipSpace is host-packed ROW-MAJOR
// (m[r*4+c]); the VS does the row-vector multiply BY HAND (mul4row), identical to
// field_raymarch_template's mul4 → `mul4row(M,v) == v·M_rowmajor`. NO transpose on the host, NO
// float4x4 column-major reinterpret here. This is the single conversion point, pinned by
// --selftest-field-camera (math) + --selftest-layer2d (the drop-mul render tooth).
#include <metal_stdlib>
#include "draw_params.h"  // DrawQuadXfParams, DRAWQUADXF_Params
using namespace metal;

// Same 6 quad corners as draw_screenquad's kQuad (verbatim from draw-Quad-vs.hlsl's Quad[] xy).
constant float2 kXfQuad[6] = {
  float2(-1, -1), float2( 1, -1), float2( 1,  1),
  float2( 1,  1), float2(-1,  1), float2(-1, -1),
};

struct QuadXfVSOut {
  float4 position [[position]];
  float2 texCoord;
};

// mul4row(M_rowmajor, v) = v·M for a ROW-MAJOR float[16]: (v·M)_j = Σ_i v_i · M[i*4+j].
// Byte-identical to field_raymarch_template.metal's mul4 (the convention the camera selftest pins).
static float4 mul4row(constant float M[16], float4 v) {
  float4 o;
  for (int j = 0; j < 4; ++j) {
    float s = 0.0;
    for (int i = 0; i < 4; ++i) s += v[i] * M[i * 4 + j];
    o[j] = s;
  }
  return o;
}

vertex QuadXfVSOut draw_quad_xf_vs(uint vid [[vertex_id]],
                                   constant DrawQuadXfParams& P [[buffer(DRAWQUADXF_Params)]]) {
  QuadXfVSOut o;
  float2 quadVertex = kXfQuad[vid];
  float2 quadVertexInObject = quadVertex * float2(P.width, P.height);
  float4 objPos = float4(quadVertexInObject, 0.0f, 1.0f);
  // The ONE new line vs draw_screenquad_vs: project the object-space quad through ObjectToClipSpace.
  // applyTransform==0 (the drop-mul golden tooth) falls back to the raw-clip ScreenQuad behavior so
  // the render golden can prove the mul is load-bearing.
  o.position = (P.applyTransform != 0u) ? mul4row(P.objectToClipSpace, objPos) : objPos;
  o.texCoord = quadVertex * float2(0.5f, -0.5f) + 0.5f;
  return o;
}

// FS reuses draw_screenquad_fs (draw_screenquad.metal): clamp(Color*tex, 0, clampMax). DrawQuadXfParams
// lays out color@0 / clampMax@32 identically to DrawScreenQuadParams, so the shared FS reads the right
// fields when this VS's pipeline binds a DrawQuadXfParams buffer at slot 0.
