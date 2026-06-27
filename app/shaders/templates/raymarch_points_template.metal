// raymarch_points_template.metal — STRING TEMPLATE for the SDF point-modify + count-multiply op
// RaymarchPoints. NOT precompiled. Mirrors sdf_reflection_line_points_template.metal: it carries the SAME
// six /*{...}*/ hooks (GLOBALS/FLOAT_PARAMS/FIELD_CALL + TEXTURES/TEXTURE_PARAMS/TEXTURE_ARGS) that
// runtime/field_graph.cpp (assembleFieldMSL) fills at runtime; platform/metal_compile then compiles the
// result via newLibrary(source). It lives under shaders/templates/ so the app/CMakeLists.txt metallib glob
// (`shaders/*.metal`, NON-recursive) does NOT pick it up (the hooks are not valid MSL until filled).
//
// SELF-CONTAINED (no project #includes): the runtime newLibrary(source) path has NO include search path, so
// this INLINES SwPoint, the buffer-slot indices, and qRotateVec3 (byte-identical to the shared headers).
//
// PARITY (TiXL Assets/shaders/points/modify/MovePointsForwardToSDF.hlsl). TWO modes ported 1:1:
//   - forward axis n = qRotateVec3((0,0,-1), Rotation) (hlsl:96).
//   - PointMode==0 RAYMARCH (hlsl:98-154): outBase = sourceIndex*PointCountPerLineReflections; write the
//     SOURCE point at outBase[0] (hlsl:101). For reflectionIndex 0..MaxReflections-1 (hlsl:102) raymarch
//     stepIndex 0..MaxSteps-1 (hlsl:104) stepping pp -= n*d*StepDistanceFactor (hlsl:143); Write* slots
//     accumulate sumD / reflectionIndex (hlsl:109-125); on |d|<MinDistance write the hit at
//     outBase+reflectionIndex+1, reflect n about -GetNormal, push off by MinDistance*100, break
//     (hlsl:127-134); on sumD>MaxDistance write the hit at outBase+reflectionIndex+1, break (hlsl:136-141).
//     Then fill outBase+reflectionIndex .. outBase+MaxReflections with NaN-Scale separators (hlsl:148-153).
//   - PointMode!=0 KEEPSTEPS (hlsl:155-207): outBase same. For reflectionIndex 0..MaxReflections (hlsl:159)
//     raymarch stepIndex 0..MaxSteps-1 (hlsl:161); Write* slots = d / stepIndex (hlsl:165-181); WRITE EVERY
//     STEP at outBase + reflectionIndex*PointCountPerLine + stepIndex (hlsl:183); accumulate sumD; on
//     |d|<MinDistance || sumD>MaxDistance reflect n about -GetNormal, push off by MinDistance*10, break
//     (hlsl:187-193); else march pp -= n*d*StepDistanceFactor (hlsl:195). After the inner loop set Scale=NaN
//     and fill stepIndex..MaxSteps separators at the same stride (hlsl:198-204), then RESTORE Scale from the
//     source point (hlsl:205) for the next reflection segment.
// GetDistance == GetField(float4(p3,0)).w (hlsl:53-56); GetNormal is the 4-tap tetrahedral finite difference
// (hlsl:58-65). reflect() is a Metal builtin (matches HLSL reflect).
//
// SCOPE / PORTED DEFAULTS. RaymarchPoints.t3 defaults (GUID-keyed): MaxSteps=20, MaxReflectionCount=0,
// MinDistance=0.005, StepDistanceFactor=1.0, NormalSamplingDistance=0.01, MaxDistance=100, Mode=0 (Raymarch),
// WriteDistanceTo=1 (FX1), WriteStepCountTo=2 (FX2). BOTH Write* DEFAULT-ACTIVE, different FX slots, ported.
// FORK (named, faithful): the HLSL `p.Scale = float3(NAN,...)` separator uses the Point's Scale field — our
// SwPoint mirror's `Scale` (packed_float3 @48) is the same field (TiXL Point.Scale -> SwPoint.Scale).
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

// --- inlined buffer-slot indices (mirror runtime/raymarchpoints_params.h RaymarchPointsBinding) ----
constant int RMP_SourcePoints = 0;  // const device SwPoint* (t0)
constant int RMP_ResultPoints = 1;  // device SwPoint*       (u0)
constant int RMP_Params       = 2;  // constant RaymarchPointsParams& (b0/b2 flattened)
constant int RMP_FieldParams  = 3;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined RaymarchPointsParams (mirror runtime/raymarchpoints_params.h, 48B) ----
struct RaymarchPointsParams {
    float MinDistance;
    float StepDistanceFactor;
    float NormalSamplingDistance;
    float MaxDistance;
    uint  SourcePointCount;
    int   MaxSteps;
    int   MaxReflections;
    int   PointMode;
    int   WriteDistanceTo;
    int   WriteStepCountTo;
    int   PointCountPerLine;
    int   PointCountPerLineReflections;
};

// --- inlined qRotateVec3 (byte-identical to app/shaders/shared/quat.metal.h). xyz = imaginary, w = real. ----
static inline float3 rmpQRotateVec3(float3 v, float4 q) {
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a sample point. p.xyz = sample point, p.w = mode flag (0 = field eval,
// matching TiXL GetField's `float4(p3.xyz, 0)`). f.w = signed distance. SEED all-ones (TiXL `float4 f = 1;`).
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// GetDistance — sample the field distance .w at p3 (p.w=0 field-eval mode). MovePointsForwardToSDF.hlsl:53-56.
static inline float rmpGetDistance(float3 p3, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return evalField(float4(p3, 0.0f), P/*{TEXTURE_ARGS}*/).w;
}

// GetNormal — tetrahedral 4-tap finite difference of the SDF distance. MovePointsForwardToSDF.hlsl:58-65.
static inline float3 rmpGetNormal(float3 p, float h, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return normalize(
        rmpGetDistance(p + float3( h, -h, -h), P/*{TEXTURE_ARGS}*/) * float3( 1, -1, -1) +
        rmpGetDistance(p + float3(-h,  h, -h), P/*{TEXTURE_ARGS}*/) * float3(-1,  1, -1) +
        rmpGetDistance(p + float3(-h, -h,  h), P/*{TEXTURE_ARGS}*/) * float3(-1, -1,  1) +
        rmpGetDistance(p + float3( h,  h,  h), P/*{TEXTURE_ARGS}*/) * float3( 1,  1,  1));
}

kernel void raymarch_points(const device SwPoint*          SourcePoints [[buffer(0)]],
                            device SwPoint*                ResultPoints [[buffer(1)]],
                            constant RaymarchPointsParams& P            [[buffer(2)]],
                            constant FieldParams&          FP           [[buffer(3)]]/*{TEXTURES}*/,
                            uint3                          tid          [[thread_position_in_grid]]) {
  int sourceIndex = (int)tid.x;
  if ((uint)sourceIndex >= P.SourcePointCount) return;  // hlsl:78-82

  SwPoint p = SourcePoints[sourceIndex];

  float sumD = 0.0;                                                     // hlsl:93
  float3 n = rmpQRotateVec3(float3(0.0, 0.0, -1.0), p.Rotation);        // hlsl:96 forward axis

  if (P.PointMode == 0) {
    // ===== RAYMARCH mode (hlsl:98-154) — keep source + per-reflection surface hits =====
    int outBaseIndex = sourceIndex * P.PointCountPerLineReflections;    // hlsl:100
    ResultPoints[outBaseIndex] = p;                                     // hlsl:101 keep the source point

    int reflectionIndex = 0;
    for (reflectionIndex = 0; reflectionIndex < P.MaxReflections; reflectionIndex++) {  // hlsl:102
      for (int stepIndex = 0; stepIndex < P.MaxSteps; stepIndex++) {    // hlsl:104
        float d = rmpGetDistance(float3(p.Position), FP/*{TEXTURE_ARGS}*/);  // hlsl:106
        sumD += d;                                                      // hlsl:107

        if (P.WriteDistanceTo == 1)      { p.FX1 = sumD; }              // hlsl:109-116
        else if (P.WriteDistanceTo == 2) { p.FX2 = sumD; }
        if (P.WriteStepCountTo == 1)      { p.FX1 = (float)reflectionIndex; }  // hlsl:118-125
        else if (P.WriteStepCountTo == 2) { p.FX2 = (float)reflectionIndex; }

        if (abs(d) < P.MinDistance) {                                  // hlsl:127-134 surface hit -> reflect
          ResultPoints[outBaseIndex + reflectionIndex + 1] = p;        // hlsl:129
          float3 surfaceNormal = -rmpGetNormal(float3(p.Position), P.NormalSamplingDistance, FP/*{TEXTURE_ARGS}*/);
          n = reflect(n, surfaceNormal);                               // hlsl:131
          p.Position = packed_float3(float3(p.Position) - n * P.MinDistance * 100.0);  // hlsl:132 push off
          break;                                                       // hlsl:133
        }

        if (sumD > P.MaxDistance) {                                    // hlsl:136-141 escaped
          ResultPoints[outBaseIndex + reflectionIndex + 1] = p;        // hlsl:139
          break;                                                       // hlsl:140
        }

        p.Position = packed_float3(float3(p.Position) - n * d * P.StepDistanceFactor);  // hlsl:143 march
      }
    }

    // hlsl:148-152 — fill the rest of the line with NaN-Scale separators (reflectionIndex..MaxReflections).
    p.Scale = packed_float3(NAN, NAN, NAN);                            // hlsl:148
    for (; reflectionIndex <= P.MaxReflections; reflectionIndex++) {   // hlsl:149
      ResultPoints[outBaseIndex + reflectionIndex] = p;               // hlsl:151
    }
    return;                                                            // hlsl:153
  }
  else {
    // ===== KEEPSTEPS mode (hlsl:155-207) — keep every raymarch step of every reflection =====
    int outBaseIndex = sourceIndex * P.PointCountPerLineReflections;   // hlsl:158
    for (int reflectionIndex = 0; reflectionIndex <= P.MaxReflections; reflectionIndex++) {  // hlsl:159
      int stepIndex = 0;
      for (stepIndex = 0; stepIndex < P.MaxSteps; stepIndex++) {       // hlsl:161
        float d = rmpGetDistance(float3(p.Position), FP/*{TEXTURE_ARGS}*/);  // hlsl:163

        if (P.WriteDistanceTo == 1)      { p.FX1 = d; }                // hlsl:165-172
        else if (P.WriteDistanceTo == 2) { p.FX2 = d; }
        if (P.WriteStepCountTo == 1)      { p.FX1 = (float)stepIndex; } // hlsl:174-181
        else if (P.WriteStepCountTo == 2) { p.FX2 = (float)stepIndex; }

        ResultPoints[outBaseIndex + reflectionIndex * P.PointCountPerLine + stepIndex] = p;  // hlsl:183

        sumD += d;                                                     // hlsl:185

        if (abs(d) < P.MinDistance || sumD > P.MaxDistance) {          // hlsl:187-193 hit/escape -> reflect
          float3 surfaceNormal = -rmpGetNormal(float3(p.Position), P.NormalSamplingDistance, FP/*{TEXTURE_ARGS}*/);
          n = reflect(n, surfaceNormal);                               // hlsl:190
          p.Position = packed_float3(float3(p.Position) - n * P.MinDistance * 10.0);  // hlsl:191 push off
          break;                                                       // hlsl:192
        }

        p.Position = packed_float3(float3(p.Position) - n * d * P.StepDistanceFactor);  // hlsl:195 march
      }

      p.Scale = packed_float3(NAN, NAN, NAN);                          // hlsl:198

      // hlsl:201-204 — including MaxSteps for separator: fill stepIndex..MaxSteps at the segment stride.
      for (; stepIndex <= P.MaxSteps; stepIndex++) {                   // hlsl:201
        ResultPoints[outBaseIndex + reflectionIndex * P.PointCountPerLine + stepIndex] = p;  // hlsl:203
      }
      p.Scale = SourcePoints[sourceIndex].Scale;                       // hlsl:205 restore source Scale
    }
  }
}
