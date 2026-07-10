// GridPlane VS+FS — TiXL Operators/Lib/render/gizmo/GridPlane (.cs Guid 935e6597) →
// Lib/Assets/shaders/dx11/draw-GridPlane.hlsl (vsMain/psMain). A single object-space quad (Size
// half-extent, ObjectToClipSpace-projected — same shape as draw_quad_xf's VS) with a PROCEDURAL
// grid pattern computed per-fragment (three nested line-frequency octaves + a distance-based fade +
// a red/blue axis-highlight overlay). No texture input — pure math, unlike ScreenQuad/Layer2d.
//
// HOST-HOISTED (byte-identical, zero behavior change): the HLSL VS also computes
// `output.scale = mul(float4(1,1,1,1), WorldToObject).xy / Size` (hlsl:57) — a value with NO
// vertex_id dependency (every one of the 6 vertices computes the identical constant). The executor
// (gridplane_fill.cpp) computes it ONCE on the CPU via mat4Inverse instead of shipping WorldToObject
// and inverting it redundantly 6× on the GPU; DrawGridPlaneParams::scaleUV IS that value, read
// directly by the FS (it never varies across the quad, so no VS-out interpolation is needed).
//
// ★HLSL TRAP (draw-GridPlane.hlsl:80-81, PRESERVED VERBATIM — do NOT "fix"): `input.scale` in the
// HLSL source is a float2, but BOTH `angleX` and `angleY` divide by it in a SCALAR-assignment
// context — `float angleX = fwidth(...) / input.scale;`. HLSL silently narrows the float2 division
// RESULT to its first component on assignment (X3206, implicit truncation) — so both angleX and
// angleY end up dividing by scale.x; angleY NEVER reads scale.y. This is TiXL's actual shipped
// observable behavior (a real HLSL footgun baked into the reference image, not a porting artifact).
// MSL has no implicit vector truncation, so this port writes `.x` on BOTH lines explicitly to
// reproduce it bit-for-bit — "fixing" it would change the rendered grid and violate parity.
// `p` (below) is NOT affected — that division is genuinely component-wise (float2/float2 assigned
// to a float2), so p.x correctly reads scale.x and p.y correctly reads scale.y (hlsl:83).
#include <metal_stdlib>
#include "draw_params.h"  // DrawGridPlaneParams, DRAWGRIDPLANE_Params
using namespace metal;

// Same 6 quad corners as draw_screenquad/draw_quad_xf's Quad[] (two tris, [-1,1] corners).
constant float2 kGridQuad[6] = {
  float2(-1, -1), float2( 1, -1), float2( 1,  1),
  float2( 1,  1), float2(-1,  1), float2(-1, -1),
};

struct GridPlaneVSOut {
  float4 position [[position]];
  float2 texCoord;
};

// mul4row(M_rowmajor, v) = v·M — byte-identical copy of draw_quad_xf.metal's helper (field_camera
// convention, NO transpose; the row-major host packing this codebase standardizes on).
static float4 mul4row(constant float M[16], float4 v) {
  float4 o;
  for (int j = 0; j < 4; ++j) {
    float s = 0.0f;
    for (int i = 0; i < 4; ++i) s += v[i] * M[i * 4 + j];
    o[j] = s;
  }
  return o;
}

vertex GridPlaneVSOut gridplane_vs(uint vid [[vertex_id]],
                                   constant DrawGridPlaneParams& P [[buffer(DRAWGRIDPLANE_Params)]]) {
  GridPlaneVSOut o;
  float2 quadVertex = kGridQuad[vid];
  float2 quadVertexInObject = quadVertex * P.size * 0.5f;                       // hlsl:51
  o.position = mul4row(P.objectToClipSpace, float4(quadVertexInObject, 0.0f, 1.0f));  // hlsl:53
  o.texCoord = quadVertex * float2(0.5f, -0.5f) + 0.5f;                          // hlsl:56
  return o;
}

// TiXL mod(x,y) = x - y*floor(x/y) (hlsl:45 #define) — ALWAYS non-negative for y>0, unlike MSL's
// fmod (which keeps the sign of x). Must use this exact formula, not metal::fmod.
static float tixlMod(float x, float y) { return x - y * floor(x / y); }

// lines() — hlsl:68-76, verbatim. divisions/lineWidth/feather are file-scope HLSL constants there
// (divisions=1, lineWidth=0.3, feather=0.1) — inlined here as literals (never node-exposed in TiXL).
static float gridLines(float d, float angle, float spacing) {
  float pInCel = tixlMod(d, spacing) / spacing;
  float distanceToEdge = abs(pInCel - 0.5f);
  float feather2 = 0.1f * angle * 15.0f / spacing;
  float lineWidth2 = angle / spacing * 0.3f;
  return 1.0f - smoothstep(lineWidth2 - feather2, lineWidth2 + feather2, 0.5f - distanceToEdge);
}

fragment float4 gridplane_fs(GridPlaneVSOut in [[stage_in]],
                             constant DrawGridPlaneParams& P [[buffer(DRAWGRIDPLANE_Params)]]) {
  // ★TRAP (see file header): both angleX and angleY divide by P.scaleUV.x — hlsl:80-81.
  float angleX = fwidth(in.texCoord.x) / P.scaleUV.x;
  float angleY = fwidth(in.texCoord.y) / P.scaleUV.x;

  // p IS genuinely component-wise (float2/float2 assigned to float2) — hlsl:83.
  float2 p = (in.texCoord - 0.5f) * 1.0f /*divisions*/ / P.scaleUV;
  float combinedAngle = max(angleX, angleY);
  float fadeOutInDistance = 1.0f - pow(saturate(combinedAngle), 0.5f);

  float smallGrid = smoothstep(0.4f, 0.9f, fadeOutInDistance);
  float grid10 = smoothstep(-0.6f, 1.1f, fadeOutInDistance);
  // `axis` (hlsl:89, smoothstep(-3,0.3,...)) is computed in the source but its ONLY consumer (the
  // linesX/Y axis-gate multiplier) is commented out there (hlsl:91-92) — genuinely dead, dropped.

  float linesX = gridLines(p.x, angleX, 1.0f * P.scale) * smallGrid +
                gridLines(p.x, angleX, 10.0f * P.scale) * grid10 +
                gridLines(p.x, angleX, 100.0f * P.scale);                        // hlsl:91
  float linesY = gridLines(p.y, angleY, 1.0f * P.scale) * smallGrid +
                gridLines(p.y, angleY, 10.0f * P.scale) * grid10 +
                gridLines(p.y, angleY, 100.0f * P.scale);                        // hlsl:92
  float lineIntensity = max(linesX, linesY);                                     // hlsl:94

  float axisX = smoothstep(-0.1f, 0.1f, saturate(abs(p.y) * (0.3f + angleY * 100.0f)));  // hlsl:96
  float axisZ = smoothstep(-0.1f, 0.1f,
                           saturate(abs(p.x) * ((p.x > 0.0f ? 2.0f : 1.0f) * 0.3f + angleX * 100.0f)));  // hlsl:97

  float redOrBlue = (axisX < axisZ) ? 0.0f : 1.0f;                               // hlsl:99
  float3 axisColor = mix(p.x > 0.0f ? float3(0.8f, 0.0f, 0.0f) : float3(0.8f, 0.6f, 0.6f),
                         p.y < 0.0f ? float3(0.2f, 0.2f, 0.9f) : float3(0.5f, 0.5f, 0.7f),
                         redOrBlue);                                             // hlsl:100-103
  float isAxis = (min(axisZ, axisX) < 0.75f) ? 1.0f : 0.0f;                      // hlsl:104
  float3 color = mix(float3(1.0f, 1.0f, 1.0f), axisColor, isAxis);               // hlsl:105
  return float4(color, lineIntensity * (isAxis != 0.0f ? 2.0f : 1.0f)) * P.color;  // hlsl:106
}
