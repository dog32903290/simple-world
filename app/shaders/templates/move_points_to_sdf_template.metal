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
// SCOPE / NAMED FORKS (faithful to TiXL defaults — every forked feature degenerates to its TiXL=default-off
// behavior, so output is bit-faithful at the op's .t3 defaults):
//   - WriteDistanceMode (hlsl:118-129): TiXL default None(0) → no FX write. NOT ported (no port). At the
//     default this branch is dead, so omitting it is faithful. FORK-WriteDistance-omitted.
//   - SetOrientation (hlsl:133-136): TiXL default false → Rotation untouched. NOT ported. FORK-SetOrient-omitted.
//   - SetColor (hlsl:138-142): TiXL default false → Color untouched. NOT ported. FORK-SetColor-omitted.
//   - AmountFactor (hlsl:111-114): TiXL default None(0) → amount = Amount*1. We bind plain Amount (factor==0
//     branch). NOT ported (no FX1/FX2 multiply). FORK-AmountFactor-none.
//   These are the faithful-at-default subset; the raymarch-to-surface core (the op's whole point) is 1:1.
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
    float _pad0;
    float _pad1;
};

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

  ResultPoints[tid.x] = p;
}
