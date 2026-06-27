// selectpointswithsdf_template.metal — STRING TEMPLATE for the direct-Field gather seam LEAF
// SelectPointsWithSDF (cloned from move_points_to_sdf_template.metal). NOT precompiled. Carries the SAME
// six /*{...}*/ hooks (GLOBALS/FLOAT_PARAMS/FIELD_CALL + TEXTURES/TEXTURE_PARAMS/TEXTURE_ARGS) that
// runtime/field_graph.cpp (assembleFieldMSL) fills at runtime, then platform/metal_compile compiles the
// result via newLibrary(source). It lives under shaders/templates/ so the app/CMakeLists.txt metallib glob
// (`shaders/*.metal`, NON-recursive) does NOT pick it up (the hooks are not valid MSL until filled).
//
// SELF-CONTAINED (no project #includes): INLINES the SwPoint layout + buffer-slot indices (byte-identical
// to tixl_point.h, 64B packed), plus the TiXL shared helpers the body uses: ApplyGainAndBias /
// GetBias / GetSchlickBias (bias-functions.hlsl) and hash11u (hash-functions.hlsl), ported 1:1.
//
// PARITY (TiXL Assets/shaders/points/modify/SelectPointsWithField.hlsl:86-182): kernel body ported 1:1.
//   - isnan(p.Scale.x) passthrough (hlsl:95-99) kept verbatim.
//   - scatter = Scatter*(hash11u(i.x)-0.5);  f0 = GetDistance(pos)+scatter*Range  (hlsl:104-106).
//   - GetDistance == GetField(float4(p3,0)).w (hlsl:56-59) == MoveToSDF's mtsGetDistance (w=0 distance branch).
//   - the MappingMode switch (hlsl:109-128), f = 1-ApplyGainAndBias(f, GainAndBias) (hlsl:132).
//   - org = WriteTo==0?1:WriteTo==1?p.FX1:p.FX2 (hlsl:134-137); the SelectMode switch (hlsl:139-158).
//   - DiscardNonSelected NaN-discard (hlsl:160) — DEAD at the .t3 default (DiscardNonSelected=false).
//   - strength factor (hlsl:162-165); result = lerp(org, f*abs(strength+1), strength) (hlsl:167);
//     result = ClampResult?max(0,result):result (hlsl:169); write to FX1/FX2 (hlsl:171-179).
// The dead `float3 d = mod(pos,1)` (hlsl:102, `d` never read) is OMITTED. No FORKs otherwise.
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

// --- inlined buffer-slot indices (mirror runtime/selectpointswithsdf_params.h SpwsdfBinding) ----
constant int SPWSDF_SourcePoints = 0;  // const device SwPoint* (t0)
constant int SPWSDF_ResultPoints = 1;  // device SwPoint*       (u0)
constant int SPWSDF_Params       = 2;  // constant SpwsdfParams& (b0)
constant int SPWSDF_FieldParams  = 3;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined SpwsdfParams (mirror runtime/selectpointswithsdf_params.h) ----
struct SpwsdfParams {
    float Strength;
    float GainAndBiasX;
    float GainAndBiasY;
    float Scatter;
    float Center;
    float Range;
    int   SelectMode;
    int   ClampResult;
    int   DiscardNonSelected;
    int   StrengthFactor;
    int   WriteTo;
    int   MappingMode;
    uint  Count;
};

// --- inlined TiXL bias helpers (byte-identical to shared/bias-functions.hlsl) ----
static inline float spwsdfGetBias(float bias, float x) {
    return x / ((1.0 / bias - 2.0) * (1.0 - x) + 1.0);
}
static inline float spwsdfGetSchlickBias(float g, float x) {
    if (x < 0.5) { x *= 2.0; x = 0.5 * spwsdfGetBias(g, x); }
    else { x = 2.0 * x - 1.0; x = 0.5 * spwsdfGetBias(1.0 - g, x) + 0.5; }
    return x;
}
static inline float spwsdfApplyGainAndBias(float value, float2 gainBias) {
    float g = saturate(gainBias.x);
    float b = saturate(gainBias.y);
    if (value > 0.9999) return 1.0;
    if (value < 0.00001) return 0.0;
    if (g < 0.5) { value = spwsdfGetBias(b, value); value = spwsdfGetSchlickBias(g, value); }
    else { value = spwsdfGetSchlickBias(g, value); value = spwsdfGetBias(b, value); }
    return value;
}

// --- inlined TiXL hash11u (byte-identical to shared/hash-functions.hlsl; _PRIME0 = 13331U) ----
static inline float spwsdfHash11u(uint x) {
    const uint k = 1103515245U;  // GLIB C
    x *= 13331U;                 // _PRIME0
    x = ((x >> 8U) ^ x) * k;
    x = ((x >> 8U) ^ x) * k;
    return float(x) * (1.0 / float(0xffffffffU));
}

// --- TiXL fmod helper (hlsl:81-84): fmod(x,y) = x - y*floor(x/y) ----
static inline float spwsdfFmod(float x, float y) { return x - y * floor(x / y); }

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a sample point. p.xyz = sample point, p.w = mode flag (0 = distance
// eval, matching TiXL GetField's `float4(p3.xyz, 0)`). f.w = signed distance. SEED all-ones (TiXL
// `float4 f = 1;`): a single SDF leaf unconditionally overwrites f.w.
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// GetDistance — sample the field distance .w at p3 (p.w=0 distance-eval mode). SelectPointsWithField.hlsl:56-59.
static inline float spwsdfGetDistance(float3 p3, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    return evalField(float4(p3, 0.0f), P/*{TEXTURE_ARGS}*/).w;
}

kernel void selectpointswithsdf(const device SwPoint*    SourcePoints [[buffer(0)]],
                                device SwPoint*          ResultPoints [[buffer(1)]],
                                constant SpwsdfParams&   P            [[buffer(2)]],
                                constant FieldParams&    FP           [[buffer(3)]]/*{TEXTURES}*/,
                                uint3                    tid          [[thread_position_in_grid]]) {
  if (tid.x >= P.Count) return;

  SwPoint p = SourcePoints[tid.x];

  // hlsl:95-99 — dead/unborn slot (NaN Scale.x) passes through verbatim.
  if (isnan(p.Scale.x)) { ResultPoints[tid.x] = p; return; }

  float3 pos = float3(p.Position);

  // hlsl:104-106 — scatter the sample, read the SDF distance.
  float scatter = P.Scatter * (spwsdfHash11u(tid.x) - 0.5);
  float f0 = spwsdfGetDistance(pos, FP/*{TEXTURE_ARGS}*/) + scatter * P.Range;

  // hlsl:109-128 — map the distance through the selected mapping mode.
  float f = 0.0;
  float Range = P.Range;
  float Center = P.Center;
  switch (P.MappingMode) {
    case 0:  // MAPPING_CENTERED
      f = (f0 + Range / 2.0) / Range - Center / (Range * 0.5);
      break;
    case 1:  // MAPPING_FORSTART
      f = f0 / Range - Center;
      break;
    case 2:  // MAPPING_PINGPONG
      f = spwsdfFmod((2.0 * f0 - 2.0 * Center * Range - 1.0) / Range, 2.0);
      f += -1.0;
      f = abs(f);
      break;
    case 3:  // MAPPING_REPEAT
      f = f0 / Range - 0.5 - Center;
      f = spwsdfFmod(f, 1.0);
      break;
  }

  // hlsl:132 — invert through gain/bias.
  f = 1.0 - spwsdfApplyGainAndBias(f, float2(P.GainAndBiasX, P.GainAndBiasY));

  // hlsl:134-137 — pick the existing weight from the WriteTo slot.
  float org = P.WriteTo == 0 ? 1.0 : (P.WriteTo == 1) ? p.FX1 : p.FX2;

  // hlsl:139-158 — combine with the existing weight by SelectMode.
  if (P.SelectMode == 0) {            // Override — s already == f
  } else if (P.SelectMode == 1) {     // Add
    f += org;
  } else if (P.SelectMode == 2) {     // Sub
    f = org - f;
  } else if (P.SelectMode == 3) {     // Multiply
    f *= org;
  } else if (P.SelectMode == 4) {     // Invert
    f = 1.0 - f * org;
  }

  // hlsl:160 — DiscardNonSelected NaN-discard (DEAD at .t3 default DiscardNonSelected=false).
  p.Scale *= (P.DiscardNonSelected != 0 && f <= 0.0) ? NAN : 1.0;

  // hlsl:162-165 — strength = Strength * factor(StrengthFactor: None/F1/F2).
  float strength = P.Strength * (P.StrengthFactor == 0 ? 1.0
                               : (P.StrengthFactor == 1) ? p.FX1
                                                         : p.FX2);

  // hlsl:167-169 — blend toward the selection, optionally clamp negatives away.
  float result = mix(org, f * abs(strength + 1.0), strength);
  result = P.ClampResult != 0 ? max(0.0, result) : result;

  // hlsl:171-179 — write the selection scalar into the chosen FX slot.
  switch (P.WriteTo) {
    case 1: p.FX1 = result; break;
    case 2: p.FX2 = result; break;
  }

  ResultPoints[tid.x] = p;
}
