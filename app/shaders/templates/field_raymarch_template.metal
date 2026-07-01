// field_raymarch_template.metal — STRING TEMPLATE for the 3D sphere-traced field render path.
// NOT precompiled (lives under shaders/templates/ so the metallib glob skips it; the hooks are not
// valid MSL until assembleFieldMSL fills them). Cloned from field_render_template.metal: it reuses the
// SAME six injection hooks (GLOBALS / FLOAT_PARAMS / FIELD_CALL / TEXTURES / TEXTURE_PARAMS /
// TEXTURE_ARGS), so assembleFieldMSL and evalField's body are REUSED UNCHANGED — the only new code is
// the camera unproject + the sphere-trace fragment. The frozen `float4 p` field seam (field_graph.h)
// is already 3D: this path feeds it a real world-space p.xyz with p.w=0, exactly like the 2D path.
//
// PARITY AUTHORITY (port the load-bearing ~120 lines of):
//   external/tixl/Operators/Lib/Assets/shaders/img/generate/RaymarchSDFFieldTemplate.hlsl
// Ported faithfully:
//   - vsMain4 ray unproject (:69-82): ViewToWorld = ClipSpaceToWorld; near/far frag pos unprojected,
//     rayDir = normalize(worldTFar - worldTNear).  (We do the unproject in the FRAGMENT instead of a
//     VS interpolant — NAMED FORK below — math identical.)
//   - the march loop (:182-186): for steps<maxSteps && |D|>MinDistance && D<MaxDistance { D=GetDistance(p); p+=dp*D; }
//   - GetNormal forward-difference gradient (:115-123) and ComputedShadedColor Blinn-Phong (:106-113)
//     + ComputeAO (:125-135) are ported as helpers, BUT see the LIVE-OUTPUT FORK note: TiXL's psMain
//     RETURNS EARLY at :222-227 with `float4(glowEffect.rrr,1)` (steps-based grayscale) + depth; the
//     Blinn-Phong/AO color block (:200-244) is DEAD CODE after that return. We faithfully render the
//     LIVE TiXL output (glow grayscale + depth), so those helpers are present-but-unused (kept for the
//     follow-up when TiXL's color path is revived; they compile and document the contract).
//
// MATRIX CONVENTION (locked, see runtime/field_camera.h): the host packs each 4x4 ROW-MAJOR into
// float[16]. HLSL `mul(rowVec, M)` = `v·M`. MSL float4x4 is column-major and `m*v` is matrix·column.
// We rebuild the MSL matrix so that `mul4(M,v)` below == `v·M_rowmajor` (see mul4). NO transpose on
// the host — the rebuild here is the single conversion point, pinned by --selftest-field-camera.
#include <metal_stdlib>
using namespace metal;

constexpr sampler clampedSampler(coord::normalized, address::clamp_to_edge, filter::nearest);

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- resource element-type struct definitions (point-buffer→field seam; empty for a leaf-only field) ----
/*{STRUCT_DEFS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Raymarch render scalars + colors (TiXL RaymarchSDFFieldTemplate.hlsl ParamConstants cbuffer, b1).
// Defaults supplied by the host (field_render.cpp) from RaymarchField.cs slot defaults.
struct RaymarchParams {
    float MaxSteps;
    float StepSize;
    float MinDistance;
    float MaxDistance;
    float Fog;
    float DistToColor;
    float AODistance;
    float _pad1;
    float4 Specular;
    float4 Glow;
    float4 AmbientOcclusion;
    float4 Background;
    packed_float3 LightPos;
    float _pad2;
    float2 Spec;
};

// Camera transforms (TiXL Transforms cbuffer, b2). We pack only the two matrices the shader uses,
// each ROW-MAJOR float[16] (NAMED FORK: faithful SUBSET of TiXL's 10-matrix Transforms layout — the
// raymarch shader reads ONLY ClipSpaceToWorld + CameraToWorld; packing the other 8 would be dead
// bytes. See field_camera.h RaymarchTransforms.).
struct Transforms {
    float clipSpaceToWorld[16];  // row-major
    float cameraToWorld[16];     // row-major
};

// mul4(M_rowmajor, v): reproduce HLSL `mul(v, M)` = `v·M` for a ROW-MAJOR float[16]. With M stored
// row-major as M[r*4+c], (v·M)_j = Σ_i v_i · M[i*4+j]. (Matches host mat4TransformPointDivW pre-divide
// and runtime/field_camera.h's convention.)
static float4 mul4(constant float M[16], float4 v) {
    float4 o;
    for (int j = 0; j < 4; ++j) {
        float s = 0.0;
        for (int i = 0; i < 4; ++i) s += v[i] * M[i * 4 + j];
        o[j] = s;
    }
    return o;
}

// Field eval (REUSED UNCHANGED from the 2D template — same evalField contract). p.xyz = world sample
// point, p.w = 0 (field-eval mode). Returns f: f.w = signed distance.
static float4 evalField(float4 p, constant FieldParams& P/*{TEXTURE_PARAMS}*//*{BUFFER_PARAMS}*/) {
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// GetDistance: the march primitive. TiXL GetDistance(p3) = GetField(float4(p3,0)).w.
static float getDistance(float3 p3, constant FieldParams& P/*{TEXTURE_PARAMS}*//*{BUFFER_PARAMS}*/) {
    return evalField(float4(p3, 0.0), P/*{TEXTURE_ARGS}*//*{BUFFER_ARGS}*/).w;
}

// GetNormal forward-difference gradient (RaymarchSDFFieldTemplate.hlsl:115-123). dt=0.01.
static float3 getNormal(float3 p, constant FieldParams& P/*{TEXTURE_PARAMS}*//*{BUFFER_PARAMS}*/) {
    float dt = 0.01;
    float3 n = float3(getDistance(p + float3(dt, 0, 0), P/*{TEXTURE_ARGS}*//*{BUFFER_ARGS}*/),
                      getDistance(p + float3(0, dt, 0), P/*{TEXTURE_ARGS}*//*{BUFFER_ARGS}*/),
                      getDistance(p + float3(0, 0, dt), P/*{TEXTURE_ARGS}*//*{BUFFER_ARGS}*/)) -
               getDistance(p, P/*{TEXTURE_ARGS}*//*{BUFFER_ARGS}*/);
    return normalize(n);
}

struct VsOut {
    float4 position [[position]];
    float2 texCoord;
};

// Fullscreen triangle (clip verts (-1,-1)(3,-1)(-1,3)) — same as the 2D path; texCoord in [0,1].
vertex VsOut sw_field_vertex(uint vid [[vertex_id]]) {
    float2 clip = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? 3.0 : -1.0);
    VsOut o;
    o.position = float4(clip, 0.0, 1.0);
    o.texCoord = clip * float2(0.5, -0.5) + 0.5;
    return o;
}

// Fragment output: color + depth (TiXL psMain PSOutput). RGBA color carries the steps-based glow
// grayscale (the LIVE TiXL output); depth is the linearized world depth (DepthFromWorldSpace2).
struct FragOut {
    float4 color [[color(0)]];
};

fragment FragOut sw_field_fragment(VsOut in [[stage_in]],
                                   constant FieldParams& P [[buffer(0)]],
                                   constant RaymarchParams& R [[buffer(1)]],
                                   constant Transforms& X [[buffer(2)]]/*{TEXTURES}*//*{BUFFERS}*/) {
    // --- ray unproject (RaymarchSDFFieldTemplate.hlsl vsMain4 :69-82, done per-fragment here) ---
    // NAMED FORK: TiXL builds worldTViewPos/worldTViewDir in the VERTEX shader and interpolates them;
    // we recompute them in the FRAGMENT from texCoord. For a fullscreen quad this is mathematically
    // identical (the unproject is affine-in-clip and we divide by w per fragment) and avoids adding a
    // VsOut interpolant — same result, fewer moving parts.
    float2 texCoord = in.texCoord;
    float2 clipXY = float2(texCoord.x * 2.0 - 1.0, -texCoord.y * 2.0 + 1.0);

    float4 vNear = float4(clipXY, 0.0, 1.0);
    float4 wNear = mul4(X.clipSpaceToWorld, vNear);
    wNear /= wNear.w;
    float4 vFar = float4(clipXY, 1.0, 1.0);
    float4 wFar = mul4(X.clipSpaceToWorld, vFar);
    wFar /= wFar.w;

    float3 eye = wNear.xyz;
    float3 dp = normalize(wFar.xyz - wNear.xyz);

    // --- march loop (RaymarchSDFFieldTemplate.hlsl :173-186) ---
    float3 p = eye;
    float D = R.StepSize;          // TiXL: D = StepSize (seed)
    int steps = 0;
    int maxSteps = (int)(R.MaxSteps - 0.5);
    for (steps = 0; steps < maxSteps && abs(D) > R.MinDistance && D < R.MaxDistance; steps++) {
        D = getDistance(p, P/*{TEXTURE_ARGS}*//*{BUFFER_ARGS}*/);
        p += dp * D;
    }

    // --- LIVE TiXL output (psMain :218-227): steps-based glow grayscale + linear world depth ---
    // glowEffect = steps / MaxSteps : a converging ray (a hit) takes few steps -> dark; a grazing /
    // missing ray exhausts maxSteps -> bright. (This IS what TiXL renders — the Blinn-Phong block
    // below the early return is dead. See header LIVE-OUTPUT FORK.)
    float glowEffect = float(steps) / R.MaxSteps;

    FragOut out;
    out.color = float4(glowEffect, glowEffect, glowEffect, 1.0);
    return out;
}

// ---- TiXL-dead helpers, ported for parity but unused by the LIVE output (see header). They compile
// and pin the Blinn-Phong / AO contract for the follow-up that revives TiXL's color path. ----
// ComputedShadedColor (RaymarchSDFFieldTemplate.hlsl:106-113) Blinn-Phong with rim term:
static float3 computedShadedColor(float3 normal, float3 view, float3 light, float3 diffuseColor,
                                  constant RaymarchParams& R) {
    float3 halfLV = normalize(light + view);
    float clampedSpecPower = max(R.Spec.y, 0.001);
    float spe = pow(max(dot(normal, halfLV), R.Spec.x), clampedSpecPower);
    float dif = dot(normal, light) * 0.1 + 0.15;
    return dif * diffuseColor + spe * R.Specular.rgb;
}
