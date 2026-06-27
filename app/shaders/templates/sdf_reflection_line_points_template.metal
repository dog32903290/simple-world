// sdf_reflection_line_points_template.metal — STRING TEMPLATE for the SDF point-modify + count-multiply
// op SdfReflectionLinePoints. NOT precompiled. Mirrors move_points_to_sdf_template.metal: it carries the
// SAME six /*{...}*/ hooks (GLOBALS/FLOAT_PARAMS/FIELD_CALL + TEXTURES/TEXTURE_PARAMS/TEXTURE_ARGS) that
// runtime/field_graph.cpp (assembleFieldMSL) fills at runtime, then platform/metal_compile compiles the
// result via newLibrary(source). It lives under shaders/templates/ so the app/CMakeLists.txt metallib glob
// (`shaders/*.metal`, NON-recursive) does NOT pick it up (the hooks are not valid MSL until filled).
//
// SELF-CONTAINED (no project #includes): the runtime newLibrary(source) path has NO include search path,
// so — like move_points_to_sdf_template.metal — this INLINES SwPoint, the buffer-slot indices, and the
// quaternion helper it needs (qRotateVec3) instead of #include'ing the shared headers. The inlined SwPoint
// is byte-identical to tixl_point.h (64B packed). This is a COUNT-MULTIPLYING op: one thread per SOURCE
// point writes a whole polyline of `pointsPerLine = MaxReflections + 3` output points, so it dispatches
// over SourcePointCount (NOT the output count) and the output buffer is sized SourceCount*pointsPerLine.
//
// PARITY (TiXL Assets/shaders/points/modify/SdfReflectionLinePoints.hlsl): kernel body ported 1:1.
//   - forward axis n = qRotateVec3((0,0,-1), Rotation) (hlsl:80).
//   - pointsPerLine = MaxReflections + 3; outIndex = sourceIndex*pointsPerLine; maxIndexForLine = end
//     (hlsl:84-86).
//   - keep the SOURCE point first (hlsl:90-110): WriteDistanceTo/WriteStepCountTo zeroed on it.
//   - reflectionIndex 0..MaxReflections (hlsl:114): inner raymarch 0..MaxSteps stepping
//     pp -= n*d*StepDistanceFactor (hlsl:153); on |d|<MinDistance reflect n about -GetNormal and push off
//     the surface by MinDistance (hlsl:135-145); on sumD>MaxDistance write the point and CANCEL the line
//     (reflectionIndex = MaxReflections+1) (hlsl:147-152). If the inner loop runs out (stepIndex==MaxSteps)
//     write FX2=outIndex + the Write* slots then emit the point (hlsl:158-179).
//   - fill the rest of the line with NaN-Scale SEPARATORS (hlsl:183-188).
// GetDistance == GetField(float4(p3,0)).w (hlsl:50-53); GetNormal is the 4-tap tetrahedral finite
// difference (hlsl:55-62). reflect() is a Metal builtin (matches HLSL reflect).
//
// SCOPE / PORTED DEFAULTS. SdfReflectionLinePoints.t3 defaults (GUID-keyed): MaxReflectionCount=2,
// MaxSteps=40, MinDistance=0.005, StepDistanceFactor=1.0, NormalSamplingDistance=0.01, MaxDistance=100,
// WriteDistanceTo=1 (FX1), WriteStepCountTo=2 (FX2). BOTH Write* are DEFAULT-ACTIVE and target DIFFERENT
// FX slots (FX1 vs FX2), so both branches are ported 1:1 and run at the op's .t3 defaults.
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

// --- inlined buffer-slot indices (mirror runtime/sdfreflectionlinepoints_params.h SdfReflectionLineBinding) ----
constant int SRL_SourcePoints = 0;  // const device SwPoint* (t0)
constant int SRL_ResultPoints = 1;  // device SwPoint*       (u0)
constant int SRL_Params       = 2;  // constant SdfReflectionLineParams& (b0)
constant int SRL_FieldParams  = 3;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined SdfReflectionLineParams (mirror runtime/sdfreflectionlinepoints_params.h, 36B) ----
struct SdfReflectionLineParams {
    float MinDistance;             // raymarch stop threshold (default 0.005)
    float StepDistanceFactor;      // march step scale (default 1.0)
    float NormalSamplingDistance;  // finite-diff h (default 0.01)
    float MaxDistance;             // accumulated-distance cutoff (default 100)
    uint  SourcePointCount;        // input bag count (thread guard)
    int   MaxSteps;                // raymarch iteration cap (default 40)
    int   MaxReflections;          // reflection cap (default 2, clamped 0..10 by .t3)
    int   WriteDistanceTo;         // .t3 default 1=FX1 (None=0/FX1=1/FX2=2)
    int   WriteStepCountTo;        // .t3 default 2=FX2 (None=0/FX1=1/FX2=2)
};

// --- inlined qRotateVec3 (byte-identical to app/shaders/shared/quat.metal.h; no include search path on
//     the newLibrary source path). xyz = imaginary, w = real. Fast Rodrigues form. ----
static inline float3 srlQRotateVec3(float3 v, float4 q) {
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
// matching TiXL GetField's `float4(p3.xyz, 0)`). f.w = signed distance. SEED all-ones (TiXL `float4 f = 1;`):
// a single SDF leaf unconditionally overwrites f.w.
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// GetDistance — sample the field distance .w at p3 (p.w=0 field-eval mode). SdfReflectionLinePoints.hlsl:50-53.
static inline float srlGetDistance(float3 p3, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return evalField(float4(p3, 0.0f), P/*{TEXTURE_ARGS}*/).w;
}

// GetNormal — tetrahedral 4-tap finite difference of the SDF distance. SdfReflectionLinePoints.hlsl:55-62.
static inline float3 srlGetNormal(float3 p, float h, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return normalize(
        srlGetDistance(p + float3( h, -h, -h), P/*{TEXTURE_ARGS}*/) * float3( 1, -1, -1) +
        srlGetDistance(p + float3(-h,  h, -h), P/*{TEXTURE_ARGS}*/) * float3(-1,  1, -1) +
        srlGetDistance(p + float3(-h, -h,  h), P/*{TEXTURE_ARGS}*/) * float3(-1, -1,  1) +
        srlGetDistance(p + float3( h,  h,  h), P/*{TEXTURE_ARGS}*/) * float3( 1,  1,  1));
}

kernel void sdf_reflection_line_points(const device SwPoint*           SourcePoints [[buffer(0)]],
                                       device SwPoint*                 ResultPoints [[buffer(1)]],
                                       constant SdfReflectionLineParams& P          [[buffer(2)]],
                                       constant FieldParams&           FP           [[buffer(3)]]/*{TEXTURES}*/,
                                       uint3                           tid          [[thread_position_in_grid]]) {
  int sourceIndex = (int)tid.x;
  if ((uint)sourceIndex >= P.SourcePointCount) return;  // hlsl:75-77

  SwPoint p = SourcePoints[sourceIndex];

  float sumD = 0.0;
  float3 n = srlQRotateVec3(float3(0.0, 0.0, -1.0), p.Rotation);  // hlsl:80 forward axis

  int pointsPerLine = (P.MaxReflections + 3);                    // hlsl:84
  int outIndex = sourceIndex * pointsPerLine;                    // hlsl:85
  int maxIndexForLine = outIndex + pointsPerLine - 1;            // hlsl:86

  // hlsl:90-110 — keep the SOURCE point first; Write* slots zeroed on it.
  if (P.WriteDistanceTo == 1)      { p.FX1 = 0.0; }
  else if (P.WriteDistanceTo == 2) { p.FX2 = 0.0; }
  if (P.WriteStepCountTo == 1)      { p.FX1 = 0.0; }
  else if (P.WriteStepCountTo == 2) { p.FX2 = 0.0; }
  ResultPoints[outIndex++] = p;                                  // hlsl:110

  // hlsl:114 — raymarch + keep reflections. reflectionIndex 0..MaxReflections inclusive.
  int reflectionIndex = 0;
  int stepIndex = 0;
  for (reflectionIndex = 0; reflectionIndex <= P.MaxReflections; reflectionIndex++) {
    for (stepIndex = 0; stepIndex < P.MaxSteps; stepIndex++) {
      float d = srlGetDistance(float3(p.Position), FP/*{TEXTURE_ARGS}*/);
      sumD += d;

      if (P.WriteDistanceTo == 1)      { p.FX1 = sumD; }          // hlsl:124-131
      else if (P.WriteDistanceTo == 2) { p.FX2 = sumD; }
      if (P.WriteStepCountTo == 1)      { p.FX1 = (float)reflectionIndex; }
      else if (P.WriteStepCountTo == 2) { p.FX2 = (float)reflectionIndex; }

      if (abs(d) < P.MinDistance) {                              // hlsl:135-144 — surface hit -> reflect
        float3 surfaceNormal = -srlGetNormal(float3(p.Position), P.NormalSamplingDistance, FP/*{TEXTURE_ARGS}*/);
        n = reflect(n, surfaceNormal);
        ResultPoints[outIndex++] = p;                            // write the surface step
        p.Position = packed_float3(float3(p.Position) - n * P.MinDistance * 1.0);  // push off the surface
        break;
      }

      if (sumD > P.MaxDistance) {                                // hlsl:146-151 — escaped -> cancel line
        ResultPoints[outIndex++] = p;
        reflectionIndex = P.MaxReflections + 1;
        break;
      }

      p.Position = packed_float3(float3(p.Position) - n * d * P.StepDistanceFactor);  // hlsl:153 march
    }

    if (stepIndex == P.MaxSteps) {                               // hlsl:158-179 — ran out of steps
      p.FX2 = (float)outIndex;
      if (P.WriteDistanceTo == 1)      { p.FX1 = sumD; }
      else if (P.WriteDistanceTo == 2) { p.FX2 = sumD; }
      if (P.WriteStepCountTo == 1)      { p.FX1 = (float)reflectionIndex; }
      else if (P.WriteStepCountTo == 2) { p.FX2 = (float)reflectionIndex; }
      ResultPoints[outIndex++] = p;
    }
  }

  // hlsl:183-188 — fill the rest of the line with NaN-Scale SEPARATORS.
  p.Scale = packed_float3(NAN, NAN, NAN);
  for (; outIndex <= maxIndexForLine; outIndex++) {
    ResultPoints[outIndex] = p;
  }
}
