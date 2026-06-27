// pointcolorwithfield_template.metal — STRING TEMPLATE for the direct-Field gather seam LEAF
// PointColorWithField (cloned from move_points_to_sdf_template.metal). NOT precompiled. Carries the SAME
// six /*{...}*/ hooks (GLOBALS/FLOAT_PARAMS/FIELD_CALL + TEXTURES/TEXTURE_PARAMS/TEXTURE_ARGS) that
// runtime/field_graph.cpp (assembleFieldMSL) fills at runtime, then platform/metal_compile compiles the
// result via newLibrary(source). It lives under shaders/templates/ so the app/CMakeLists.txt metallib glob
// (`shaders/*.metal`, NON-recursive) does NOT pick it up (the hooks are not valid MSL until filled).
//
// SELF-CONTAINED (no project #includes): the runtime newLibrary(source) path has NO include search path,
// so — exactly like the force/MoveToSDF templates — this INLINES the SwPoint layout + the buffer-slot
// indices it needs instead of #include "tixl_point.h". The inlined SwPoint is byte-identical to
// tixl_point.h (64B packed). This is a MODIFIER over a Points BAG (SourcePoints t0 -> ResultPoints u0).
//
// PARITY (TiXL Assets/shaders/points/_research/ColorPointsWithField.hlsl:60-78): kernel body ported 1:1.
//   - isnan(p.Scale.x) early-passthrough (hlsl:62-66) kept verbatim.
//   - strength = Strength * (StrengthFactor==0 ? 1 : StrengthFactor==1 ? p.FX1 : p.FX2)  (hlsl:69-72).
//   - field = GetField(float4(pos,1))  (hlsl:75); p.Color = lerp(p.Color, field, strength)  (hlsl:76).
// GetField == evalField here; float4(pos,1) (w=1) selects the assembled SDF's COLOR branch (f.xyz),
// IDENTICAL to MoveToSDF's SetColor evalField(float4(pp,1),...).rgb. p.Color is float4, so the lerp blends
// ALL FOUR channels toward `field` (TiXL `p.Color = lerp(p.Color, field, strength)` — field.w from the
// all-ones seed is 1, so a single SphereSDF leaf overwrites .xyz and leaves f.w=1).
// SCOPE: no raymarch / normal / quaternion machinery (this op does not move points) — those are dropped
// vs the MoveToSDF clone. No FORKs: the whole .hlsl body is ported.
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

// --- inlined buffer-slot indices (mirror runtime/pointcolorwithfield_params.h PcwfBinding) ----
constant int PCWF_SourcePoints = 0;  // const device SwPoint* (t0)
constant int PCWF_ResultPoints = 1;  // device SwPoint*       (u0)
constant int PCWF_Params       = 2;  // constant PcwfParams& (b0)
constant int PCWF_FieldParams  = 3;  // constant FieldParams& (the assembled field's packed buffer)

// --- inlined PcwfParams (mirror runtime/pointcolorwithfield_params.h) ----
struct PcwfParams {
    float Strength;        // lerp toward the field color (TiXL Strength, default 1)
    float Range;           // b0 slot, DEAD in the body (kept for cbuffer-layout fidelity)
    uint  Count;           // bag size
    int   StrengthFactor;  // FModes: 0=None -> x1, 1=F1 -> p.FX1, 2=F2 -> p.FX2
};

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a sample point. p.xyz = sample point, p.w = mode flag (0 = distance
// eval, 1 = color branch, matching TiXL GetField). f.w = signed distance; f.xyz = field color. SEED
// all-ones (TiXL `float4 f = 1;`): a single SDF leaf unconditionally overwrites f.w (and f.xyz on w>=0.5).
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

kernel void pointcolorwithfield(const device SwPoint*    SourcePoints [[buffer(0)]],
                                device SwPoint*          ResultPoints [[buffer(1)]],
                                constant PcwfParams&     P            [[buffer(2)]],
                                constant FieldParams&    FP           [[buffer(3)]]/*{TEXTURES}*/,
                                uint3                    tid          [[thread_position_in_grid]]) {
  if (tid.x >= P.Count) return;

  SwPoint p = SourcePoints[tid.x];

  // hlsl:62-66 — dead/unborn slot (NaN Scale.x) passes through verbatim.
  if (isnan(p.Scale.x)) { ResultPoints[tid.x] = p; return; }

  // hlsl:69-72 — strength = Strength * factor(StrengthFactor: None/F1/F2).
  float strength = P.Strength * (P.StrengthFactor == 0 ? 1.0
                               : (P.StrengthFactor == 1) ? p.FX1
                                                         : p.FX2);

  // hlsl:75-76 — sample the field COLOR branch (w=1) at the point position, lerp the point Color toward it.
  float3 pos = float3(p.Position);
  float4 field = evalField(float4(pos, 1.0), FP/*{TEXTURE_ARGS}*/);
  p.Color = mix(p.Color, field, strength);

  ResultPoints[tid.x] = p;
}
