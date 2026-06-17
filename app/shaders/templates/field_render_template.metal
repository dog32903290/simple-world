// field_render_template.metal — STRING TEMPLATE for runtime shader-graph codegen. NOT precompiled.
//
// This file is a *template*, not a standalone shader: it contains /*{...}*/ hooks that
// runtime/field_graph.cpp (assembleFieldMSL) fills in at runtime, then platform/metal_compile
// compiles the result with newLibrary(source). It deliberately lives under shaders/templates/ so the
// app/CMakeLists.txt metallib glob — `shaders/*.metal` (NON-recursive) — does NOT pick it up and try
// to precompile it into shaders.metallib (it would fail: the hooks are not valid MSL until filled).
//
// PARITY (TiXL field render template): three injection hooks, same names as
//   external/tixl/Operators/Lib/field/render/_/GenerateShaderGraphCode.cs (GLOBALS / FLOAT_PARAMS /
//   FIELD_CALL). TiXL's HLSL cbuffer becomes an MSL `constant Params&` argument; HLSL register(b0)
//   becomes [[buffer(0)]]. The FIELD_CALL body (p<c>/f<c>/length()) is identical text in both.
//
// COORDINATE PARITY (backward-traced from FieldToImageTemplate.hlsl psMain, the authoritative TiXL
// 2D field->image path, default Center=(0,0)/Scale=1/Rotate=0/SliceDepth=0/square aspect):
//   uv = texCoord; uv.y = 1-uv.y; uv -= 0.5; uv *= 2;  ->  p.xy = (texCoord.x*2-1, (1-texCoord.y)*2-1)
//   p.z = SliceDepth = 0 ; GetField seeds p.w = 0  (NOT 1) so SphereSDF's `p.w < 0.5` branch matches.
// We render a fullscreen triangle whose interpolated texCoord drives this mapping. The fragment
// writes f.w (the signed distance) into the RED channel (orchestrator: golden reads f.w cleanly;
// the TiXL color-map / gradient lives downstream and is parity-deferred).
#include <metal_stdlib>
using namespace metal;

// --- node helper globals (de-duplicated reusable functions) ----
/*{GLOBALS}*/

// --- all node parameters, packed into a single 16-byte-aligned constant buffer (TiXL FloatParams) ----
struct FieldParams {
/*{FLOAT_PARAMS}*/
};

// Evaluate the assembled field at a local position. p.xyz = sample point, p.w = mode flag (0 = field
// eval, matching TiXL GetField's `float4(p3.xyz, 0)`). Returns f: f.w = signed distance, f.xyz =
// local space / color carry (TiXL convention).
static float4 evalField(float4 p, constant FieldParams& P) {
    // PARITY external/tixl/Operators/Lib/Assets/shaders/img/generate/FieldToImageTemplate.hlsl:99
    //   `float4 f = 1;` — the field SEED is all-ones (f.w=1, f.xyz=1), NOT zero. This is the value a
    // node reads before it writes (load-bearing for future combiners). A single SphereSDF leaf
    // unconditionally overwrites f.w and f.xyz (it branches on p.w, not f.w — see SphereSDF.cs:35-36),
    // so the seed is invisible for the current single-leaf golden; aligning it now keeps parity for
    // graphs whose leaves READ f before writing.
    float4 f = float4(1.0);
/*{FIELD_CALL}*/
    return f;
}

// Fullscreen triangle (3 verts cover the [-1,1] clip quad). texCoord in [0,1] is carried to the
// fragment exactly like a TiXL fullscreen pass (quadPos.xy * float2(0.5,-0.5) + 0.5).
struct VsOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VsOut sw_field_vertex(uint vid [[vertex_id]]) {
    // Oversized triangle: clip-space verts (-1,-1)(3,-1)(-1,3) cover the whole [-1,1] viewport.
    float2 clip = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? 3.0 : -1.0);
    VsOut o;
    o.position = float4(clip, 0.0, 1.0);
    // TiXL fullscreen texCoord rule: quadPos.xy * (0.5,-0.5) + 0.5 (so y is flipped here already).
    o.texCoord = clip * float2(0.5, -0.5) + 0.5;
    return o;
}

fragment float4 sw_field_fragment(VsOut in [[stage_in]],
                                  constant FieldParams& P [[buffer(0)]]) {
    // Map texCoord -> field space (FieldToImageTemplate default regula). texCoord.y is already the
    // flipped TiXL convention from the vertex; the psMain `uv.y = 1-uv.y` then `uv-=0.5; uv*=2` net
    // maps to p.xy below. p.z = SliceDepth = 0, p.w = 0 (field-eval mode).
    float2 uv = in.texCoord;
    uv.y = 1.0 - uv.y;
    uv -= 0.5;
    uv *= 2.0;
    // fork: aspect=1 假設；非方形輸出未對齊 TiXL uv.x*=aspectRatio。方形 golden 範圍內 byte-parity，非方形 follow-up。
    // (TiXL FieldToImageTemplate.hlsl:124,128 `aspectRatio = TargetWidth/TargetHeight; uv.x *= aspectRatio;`)
    float4 p = float4(uv, 0.0, 0.0);
    float4 f = evalField(p, P);
    // Distance -> RED channel (golden reads R). Keep g/b/a defined for a stable readback layout.
    return float4(f.w, 0.0, 0.0, 1.0);
}
