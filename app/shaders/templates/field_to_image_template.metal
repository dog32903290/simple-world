// field_to_image_template.metal — STRING TEMPLATE for the FieldToImage tex op (field_ops_fieldtoimage.cpp).
// NOT precompiled — see field_render_template.metal's header for the general string-asset rule (same
// CMakeLists non-recursive glob dodge). assembleFieldMSL fills the SAME hooks (GLOBALS/STRUCT_DEFS/
// FLOAT_PARAMS/FIELD_CALL/TEXTURES/TEXTURE_PARAMS/TEXTURE_ARGS/BUFFERS/BUFFER_PARAMS/BUFFER_ARGS) as the
// 2D + raymarch templates — the SAME 16+ SDF leaves cook byte-identically into this template too.
//
// TiXL authority: external/tixl/Operators/Lib/Assets/shaders/img/generate/FieldToImageTemplate.hlsl
// psMain (the FULL Center/Scale/Rotate/SliceDepth/Mode/Range/GainAndBias/PingPong/Repeat/Gradient path
// — field_render_template.metal only ports the IDENTITY-param slice of this same file for the raw-
// distance test harness renderField2d uses). Line numbers below cite that file.
//
// OWN-PARAM BUFFER (buffer(1)): FieldToImage's op-level (non-field) params do NOT ride the assembled
// FieldParams buffer(0) (that buffer is the SDF TREE's params, keyed by node prefix) — they are a
// SEPARATE fixed struct at buffer(1), exactly like field_raymarch_template.metal reserves buffer(1)/
// buffer(2) for its own RaymarchParams/Transforms. The dynamic point-buffer seam's BUFFERS hook still
// starts at buffer(field_graph.cpp's kBufferBaseSlot=3) — buffer(2) is simply an unused gap here (Metal
// permits gaps), identical to how the raymarch template leaves 2D's buffer(1)/(2) unused.
//
// GRADIENT TEXTURE (texture(30)): a FIXED high slot, OUT OF BAND from Seam A's dynamic field textures
// (which occupy texture(0..N-1), N = the assembled field's OWN texture-leaf count — 0 for every current
// SDF leaf, 1 for Image2dSDF). Metal fragment texture bind indices need not be contiguous; slot 30 gives
// headroom far past any realistic field texture count (mirrors the buffer-base-slot reservation-gap
// discipline field_graph.cpp already uses for the point-buffer seam).
#include <metal_stdlib>
using namespace metal;

// Field-eval sampler (Seam A texture-into-field leaves, e.g. Image2dSDF) — verbatim from
// field_render_template.metal (nearest + clamp, for golden texel-exact determinism).
constexpr sampler clampedSampler(coord::normalized, address::clamp_to_edge, filter::nearest);
// Gradient-row sampler: LINEAR + clamp (matches TiXL's ClampedSampler on the Gradient Texture2D SRV —
// BoxGradient/LinearGradient's "clammpedSampler" precedent, boxgradient.metal:136).
constexpr sampler gradientSampler(coord::normalized, address::clamp_to_edge, filter::linear);

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- resource element-type struct definitions (point-buffer→field seam; empty for a leaf-only field) ----
/*{STRUCT_DEFS}*/

// --- all FIELD-TREE node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// FieldToImage's OWN op-level params (buffer(1)) — host mirror = FieldToImageParams (field_render.h).
// Deliberately ALL PLAIN FLOATS (no float2/float3 members): avoids MSL vector-alignment padding rules
// entirely so the host struct (field_render.cpp) can memcpy a flat float array with zero drift risk —
// same discipline as field_render.cpp's RaymarchParamsGpu.
struct FieldToImageParams {
    float CenterX, CenterY;   // FieldToImageTemplate.hlsl ParamConstants.Center (:12)
    float Scale;               // :13
    float Rotate;               // :14
    float SliceDepth;            // :17
    float Mode;                   // 2nd cbuffer's Mode (:25) — 0 = MapDistanceToColor, 1 = UseColor
    float RangeX, RangeY;          // ParamConstants.Range (:16)
    float GainX, BiasY;             // ParamConstants.GainAndBias (:15)
    float PingPong, Repeat;          // 2nd cbuffer's PingPong/.Repeat (:23-24)
    float Aspect;                     // host-precomputed TargetWidth/TargetHeight (ResolutionConstBuffer :31-32)
    float __pad0, __pad1, __pad2;      // pad to 64B (not load-bearing; headroom/clarity only)
};

// Evaluate the assembled field at a local position (IDENTICAL contract to field_render_template.metal /
// field_raymarch_template.metal — the seed float4(1.0), the FIELD_CALL hook). p.xyz = sample point,
// p.w = mode flag (0 = field eval). Returns f: f.w = signed distance, f.xyz = local space / color carry.
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*//*{BUFFER_PARAMS}*/) {
    // PARITY external/tixl/Operators/Lib/Assets/shaders/img/generate/FieldToImageTemplate.hlsl:99
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// Fullscreen triangle — SAME convention as field_render_template.metal's sw_field_vertex (3 verts cover
// the [-1,1] clip quad; texCoord carried exactly like TiXL's fullscreen quadPos.xy*(0.5,-0.5)+0.5).
struct VsOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VsOut sw_fieldtoimage_vertex(uint vid [[vertex_id]]) {
    float2 clip = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? 3.0 : -1.0);
    VsOut o;
    o.position = float4(clip, 0.0, 1.0);
    o.texCoord = clip * float2(0.5, -0.5) + 0.5;
    return o;
}

// --- shared/bias-functions.hlsl :: ApplyGainAndBias (scalar, lines 6-49) — VERBATIM, same port as
// boxgradient.metal / lineargradient.metal / ngongradient.metal (all trace to the SAME file). ---
static inline float fi_GetBias(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
static inline float fi_GetSchlickBias(float g, float x) {
    if (x < 0.5f) {
        x *= 2.0f;
        x = 0.5f * fi_GetBias(g, x);
    } else {
        x = 2.0f * x - 1.0f;
        x = 0.5f * fi_GetBias(1.0f - g, x) + 0.5f;
    }
    return x;
}
static inline float fi_ApplyGainAndBias(float value, float2 gainBias) {
    float g = saturate(gainBias.x);
    float b = saturate(gainBias.y);
    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;
    if (g < 0.5f) {
        value = fi_GetBias(b, value);
        value = fi_GetSchlickBias(g, value);
    } else {
        value = fi_GetSchlickBias(g, value);
        value = fi_GetBias(b, value);
    }
    return value;
}

// PingPongRepeat — VERBATIM FieldToImageTemplate.hlsl lines 66-84 (baseValue = x, NOT x+0.5 — the SAME
// variant as BoxGradient/NGonGradient, not LinearGradient's +0.5 form).
static inline float fi_PingPongRepeat(float x, float pingPong, float repeat) {
    float baseValue = x;                                                  // :68
    float repeatValue = fract(baseValue);                                // :69
    float pingPongValue = 1.0f - abs(fract(x * 0.5f) * 2.0f - 1.0f);    // :70
    float singlePingPong = abs(x);                                        // :71

    float pingPongOutput = mix(singlePingPong, pingPongValue, step(0.5f, repeat));  // :74

    float value = mix(baseValue, repeatValue, step(0.5f, repeat));       // :77
    value = mix(value, pingPongOutput, step(0.5f, pingPong));             // :78
    value = mix(saturate(value), value, step(0.5f, repeat));             // :81
    return value;
}

// Mirror of FieldToImageTemplate.hlsl psMain (lines 121-151), line for line.
fragment float4 sw_fieldtoimage_fragment(VsOut in [[stage_in]],
                                         constant FieldParams& P [[buffer(0)]],
                                         constant FieldToImageParams& OP [[buffer(1)]],
                                         texture2d<float> Gradient [[texture(30)]]
                                         /*{TEXTURES}*//*{BUFFERS}*/) {
    float2 uv = in.texCoord;                                              // :123
    uv.y = 1.0f - uv.y;   // Flip Y for correct orientation                // :125
    uv -= 0.5f;                                                           // :126
    uv -= float2(OP.CenterX, OP.CenterY);                                 // :127
    uv.x *= OP.Aspect;                                                    // :128
    uv *= 2.0f;                                                           // :129
    float a = OP.Rotate * (3.14159265358979323846f / 180.0f);  // radians(Rotate)  // :130
    uv = cos(a) * uv + sin(a) * float2(uv.y, -uv.x);                      // :131
    uv /= OP.Scale;                                                       // :133

    float4 samplePos = float4(uv, OP.SliceDepth, 0.0f);                  // :135
    float4 f = evalField(samplePos, P/*{TEXTURE_ARGS}*//*{BUFFER_ARGS}*/); // GetField(samplePos)  :136

    float d = f.w;                                                       // :138

    if (OP.Mode > 0.5f)                                                  // :140
        return float4(f.rgb, 1.0f);                                      // :141

    d = (d - OP.RangeX) / (OP.RangeY - OP.RangeX);                       // :143

    d = fi_PingPongRepeat(d, OP.PingPong, OP.Repeat);                    // :145
    d = fi_ApplyGainAndBias(d, float2(OP.GainX, OP.BiasY));              // :146

    float4 color = Gradient.sample(gradientSampler, float2(d, 0.5f));    // :148
    return color;                                                        // :151
}
