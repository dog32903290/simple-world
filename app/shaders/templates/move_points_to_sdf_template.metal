// move_points_to_sdf_template.metal — STRING TEMPLATE for the SDF point-modify seam, MovePointsToSDF.
// NOT precompiled. Mirrors field_distance_force_template.metal: it carries the SAME six /*{...}*/ hooks
// (GLOBALS/FLOAT_PARAMS/FIELD_CALL + TEXTURES/TEXTURE_PARAMS/TEXTURE_ARGS) that runtime/field_graph.cpp
// (assembleFieldMSL) fills at runtime, then platform/metal_compile compiles the result via
// newLibrary(source). It lives under shaders/templates/ so the app/CMakeLists.txt metallib glob
// (`shaders/*.metal`, NON-recursive) does NOT pick it up (the hooks are not valid MSL until filled).
//
// SELF-CONTAINED (no project #includes): the runtime newLibrary(source) path has NO include search path,
// so — exactly like the force templates — this INLINES the SwPoint layout + the buffer-slot indices it
// needs instead of #include "tixl_point.h". The inlined SwPoint is byte-identical to tixl_point.h:
// (packed_float3 Position @0, float FX1 @12, float4 Rotation @16, float4 Color @32, packed_float3 Scale
// @48, float FX2 @60 = 64 bytes). This is a MODIFIER over a Points BAG (SourcePoints t0 → ResultPoints u0),
// NOT the in-place particle pool the force templates mutate — hence two buffer slots (src + dst).
//
// PARITY (TiXL Assets/shaders/points/modify/MovePointsToSDF.hlsl:77-145): kernel body ported 1:1. The
// raymarch loop (hlsl:98-109) walks `pp -= GetNormal(pp) * d * StepDistanceFactor` for up to MaxSteps,
// stopping when |GetDistance(pp)| < MinDistance; then `p.Position = lerp(p.Position, pp, amount)` (hlsl:131).
// GetDistance == GetField(float4(p3,0)).w (hlsl:54-57); GetNormal is the 4-tap tetrahedral finite difference
// (hlsl:59-66). The isnan(Scale.x) early-passthrough (hlsl:86-90) is kept verbatim.
//
// SCOPE / PORTED DEFAULTS + NAMED FORKS. MoveToSDF.t3 defaults (GUID-keyed): SetOrientation=TRUE,
// SetColor=TRUE, WriteDistanceMode=None(0), AmountFactor=None(0). So at the op's .t3 defaults TiXL ALSO
// reorients each point to the surface normal AND recolors it from the field — both are now ported 1:1:
//   - SetOrientation (hlsl:133-136): TiXL default TRUE → p.Rotation = qSlerp(p.Rotation,
//     normalize(qLookAt(n, float3(0,1,0))), amount). PORTED (gated int, NodeSpec default true). The normal
//     `n` is the last raymarch-loop normal (same `n` TiXL uses). qSlerp/qLookAt inlined below (no project
//     #includes on the newLibrary path) byte-identical to shared/quat.metal.h.
//   - SetColor (hlsl:138-142): TiXL default TRUE → p.Color.rgb = lerp(p.Color.rgb, GetField(float4(pp,1)).rgb,
//     amount). PORTED (gated int, NodeSpec default true). evalField returns the full float4; the assembled
//     SDF MSL emits f.xyz (color) alongside f.w (distance) — e.g. SphereSDF emits
//     `f.xyz = p.w < 0.5 ? p.xyz : float3(1.0)`, so GetField(...,p.w=1).rgb is the field color, faithful.
//   - WriteDistanceMode (hlsl:118-129): TiXL default None(0) → no FX write. NOT ported (no port). At the
//     default this branch is dead, so omitting it is faithful. FORK-WriteDistance-omitted.
//   - AmountFactor (hlsl:111-114): TiXL default None(0) → amount = Amount*1. We bind plain Amount (factor==0
//     branch). NOT ported (no FX1/FX2 multiply). FORK-AmountFactor-none.
//   The raymarch-to-surface core + the two default-true orient/color branches are 1:1; only the two
//   non-default-driver extras (WriteDistanceMode / AmountFactor) remain forked.
#include <metal_stdlib>
using namespace metal;

// --- inlined SwPoint (byte-identical to runtime/tixl_point.h SwPoint, 64B packed) ----
struct SwPoint {
    packed_float3 Position;  // @0
    float         FX1;       // @12
    float4        Rotation;  // @16
    float4        Color;     // @32
    packed_float3 Scale;     // @48
    float         FX2;       // @60
};                           // 64

// --- inlined buffer-slot indices (mirror runtime/movepointstosdf_params.h MoveToSdfBinding) ----
constant int MTS_SourcePoints = 0;  // const device SwPoint* (t0)
constant int MTS_ResultPoints = 1;  // device SwPoint*       (u0)
constant int MTS_Params       = 2;  // constant MoveToSdfParams& (b0)
constant int MTS_FieldParams  = 3;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined MoveToSdfParams (mirror runtime/movepointstosdf_params.h) ----
struct MoveToSdfParams {
    float Amount;               // lerp toward the surface point (TiXL Amount, default 1)
    float MinDistance;          // raymarch stop threshold (default 0.005)
    float StepDistanceFactor;   // march step scale (default 0.5)
    float NormalSamplingDistance;  // finite-diff h (default 0.1)
    uint  Count;                // bag size
    int   MaxSteps;             // raymarch iteration cap (default 20)
    int   SetOrientation;       // .t3 default TRUE → reorient to surface normal (hlsl:133-136)
    int   SetColor;             // .t3 default TRUE → recolor from field (hlsl:138-142)
};

// --- inlined quaternion helpers (byte-identical to app/shaders/shared/quat.metal.h; the newLibrary
//     source path has NO include search path so the template cannot #include the shared header). Used
//     ONLY by the SetOrientation branch (hlsl:133-136). xyz = imaginary, w = real. ----
#define MTS_QUAT_IDENTITY float4(0, 0, 0, 1)

static inline float4 mtsQSlerp(float4 a, float4 b, float t) {
  if (length(a) == 0.0) {
    if (length(b) == 0.0) return MTS_QUAT_IDENTITY;
    return b;
  } else if (length(b) == 0.0) {
    return a;
  }
  float cosHalfAngle = a.w * b.w + dot(a.xyz, b.xyz);
  if (cosHalfAngle >= 1.0 || cosHalfAngle <= -1.0) {
    return a;
  } else if (cosHalfAngle < 0.0) {
    b.xyz = -b.xyz;
    b.w = -b.w;
    cosHalfAngle = -cosHalfAngle;
  }
  float blendA;
  float blendB;
  if (cosHalfAngle < 0.99) {
    float halfAngle = acos(cosHalfAngle);
    float sinHalfAngle = sin(halfAngle);
    float oneOverSinHalfAngle = 1.0 / sinHalfAngle;
    blendA = sin(halfAngle * (1.0 - t)) * oneOverSinHalfAngle;
    blendB = sin(halfAngle * t) * oneOverSinHalfAngle;
  } else {
    blendA = 1.0 - t;
    blendB = t;
  }
  float4 result = float4(blendA * a.xyz + blendB * b.xyz, blendA * a.w + blendB * b.w);
  if (length(result) > 0.0) return normalize(result);
  return MTS_QUAT_IDENTITY;
}

static inline float4 mtsQLookAt(float3 forward, float3 up) {
  float3 right = normalize(cross(forward, up));
  up = normalize(cross(forward, right));
  float m00 = right.x, m01 = right.y, m02 = right.z;
  float m10 = up.x,    m11 = up.y,    m12 = up.z;
  float m20 = forward.x, m21 = forward.y, m22 = forward.z;
  float num8 = (m00 + m11) + m22;
  float4 q = MTS_QUAT_IDENTITY;
  if (num8 > 0.0) {
    float num = sqrt(num8 + 1.0);
    q.w = num * 0.5;
    num = 0.5 / num;
    q.x = (m12 - m21) * num;
    q.y = (m20 - m02) * num;
    q.z = (m01 - m10) * num;
    return q;
  }
  if ((m00 >= m11) && (m00 >= m22)) {
    float num7 = sqrt(((1.0 + m00) - m11) - m22);
    float num4 = 0.5 / num7;
    q.x = 0.5 * num7;
    q.y = (m01 + m10) * num4;
    q.z = (m02 + m20) * num4;
    q.w = (m12 - m21) * num4;
    return q;
  }
  if (m11 > m22) {
    float num6 = sqrt(((1.0 + m11) - m00) - m22);
    float num3 = 0.5 / num6;
    q.x = (m10 + m01) * num3;
    q.y = 0.5 * num6;
    q.z = (m21 + m12) * num3;
    q.w = (m20 - m02) * num3;
    return q;
  }
  float num5 = sqrt(((1.0 + m22) - m00) - m11);
  float num2 = 0.5 / num5;
  q.x = (m20 + m02) * num2;
  q.y = (m21 + m12) * num2;
  q.z = 0.5 * num5;
  q.w = (m01 - m10) * num2;
  return q;
}

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a sample point. p.xyz = sample point, p.w = mode flag (0 = field eval,
// matching TiXL GetField's `float4(p3.xyz, 0)`). f.w = signed distance (field_ops_spheresdf.cpp:56). SEED
// all-ones (TiXL `float4 f = 1;`): a single SDF leaf unconditionally overwrites f.w.
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// GetDistance — sample the field distance .w at p3 (p.w=0 field-eval mode). MovePointsToSDF.hlsl:54-57.
static inline float mtsGetDistance(float3 p3, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return evalField(float4(p3, 0.0f), P/*{TEXTURE_ARGS}*/).w;
}

// GetNormal — tetrahedral 4-tap finite difference of the SDF distance. MovePointsToSDF.hlsl:59-66.
static inline float3 mtsGetNormal(float3 p, float h, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return normalize(
        mtsGetDistance(p + float3( h, -h, -h), P/*{TEXTURE_ARGS}*/) * float3( 1, -1, -1) +
        mtsGetDistance(p + float3(-h,  h, -h), P/*{TEXTURE_ARGS}*/) * float3(-1,  1, -1) +
        mtsGetDistance(p + float3(-h, -h,  h), P/*{TEXTURE_ARGS}*/) * float3(-1, -1,  1) +
        mtsGetDistance(p + float3( h,  h,  h), P/*{TEXTURE_ARGS}*/) * float3( 1,  1,  1));
}

kernel void move_points_to_sdf(const device SwPoint*       SourcePoints [[buffer(0)]],
                               device SwPoint*             ResultPoints [[buffer(1)]],
                               constant MoveToSdfParams&   P            [[buffer(2)]],
                               constant FieldParams&       FP           [[buffer(3)]]/*{TEXTURES}*/,
                               uint3                       tid          [[thread_position_in_grid]]) {
  if (tid.x >= P.Count) return;

  SwPoint p = SourcePoints[tid.x];

  // hlsl:86-90 — dead/unborn slot (NaN Scale.x) passes through verbatim.
  if (isnan(p.Scale.x)) { ResultPoints[tid.x] = p; return; }

  float3 pos = float3(p.Position);

  // hlsl:94-109 — raymarch to the surface: step along -normal scaled by distance, stop near the surface.
  float3 pp = pos;
  float3 n  = float3(0.0);
  for (int stepIndex = 0; stepIndex < P.MaxSteps; ++stepIndex) {
    float d = mtsGetDistance(pp, FP/*{TEXTURE_ARGS}*/);
    if (abs(d) < P.MinDistance) break;
    n = mtsGetNormal(pp, P.NormalSamplingDistance, FP/*{TEXTURE_ARGS}*/);
    pp -= n * d * P.StepDistanceFactor;
  }

  // hlsl:111-114 — AmountFactor None(0): amount = Amount * 1 (FORK-AmountFactor-none, faithful at default).
  float amount = P.Amount;

  // hlsl:131 — blend the original position toward the converged surface point.
  p.Position = packed_float3(mix(float3(p.Position), pp, amount));

  // hlsl:133-136 — SetOrientation (TiXL .t3 default TRUE): re-aim Rotation toward the surface normal `n`
  // (the last normal from the raymarch loop, exactly as TiXL uses it), slerped by amount.
  if (P.SetOrientation != 0) {
    p.Rotation = mtsQSlerp(p.Rotation, normalize(mtsQLookAt(n, float3(0.0, 1.0, 0.0))), amount);
  }

  // hlsl:138-142 — SetColor (TiXL .t3 default TRUE): recolor from the field at the surface point. p.w=1
  // selects the assembled SDF's COLOR branch (f.xyz), matching TiXL GetField(float4(pp,1)).rgb.
  if (P.SetColor != 0) {
    float3 color = evalField(float4(pp, 1.0), FP/*{TEXTURE_ARGS}*/).rgb;
    p.Color.rgb = mix(p.Color.rgb, color, amount);
  }

  ResultPoints[tid.x] = p;
}
