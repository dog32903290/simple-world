// computeshaderstage_getimagebrightness — the BUFFER-track compute-stage kernel for GetImageBrightness
// (TEXTURE_COMPUTE_SEAM_SPEC.md stage 3: 1SRV-tex + 1UAV-buf + b0 CB — the first texture-SRV-read,
// BUFFER-write kernel through the generic ComputeShaderStage seam). This is the kernel that PROVES the
// buffer-rail stage's stage-3 mechanism: it reads a Texture2D SRV (bound at Metal texture(0) =
// CS_TEX_SRV_BASE) and accumulates a scaled luminance sum into a RWStructuredBuffer<uint> UAV (bound at
// buffer(12) = CS_UAV_BASE) — so a dropped setTexture (the cross-currency gather off) yields the wrong sum.
//
// TiXL authority (P5-safe — ONLY the HLSL, never sw's own output):
//   external/tixl/Operators/Lib/Assets/shaders/img/analyze/cs-GetImageBrightness.hlsl (main, :26-49)
//     Texture2D<float4> InputTexture : register(t0);   RWStructuredBuffer<uint> ResultBuffer : register(u0);
//     cbuffer ParamConstants : b0 { uint scaleFactor; uint textureWidth; uint textureHeight; }
//     color = InputTexture.Load(DTid); lum = (r+g+b)/3*a; scaled = (uint)(lum*scaleFactor);
//     InterlockedAdd(localSum, scaled) [groupshared]; then GI==0 → InterlockedAdd(ResultBuffer[0], localSum).
//   The compound's `clear` pass (cs-GetImageBrightness.hlsl:19-23) zeroes ResultBuffer[0] — folded away in
//   sw to "the UAV producer supplies a zero-initialized buffer" (fork getimagebrightness-clear-folds-to-
//   buffer-zeroinit): the stage dispatches only `main`; the reduction's correctness needs ResultBuffer[0]==0
//   on entry, which the IntsToBuffer/buffer producer provides (TiXL's clear pass is a GPU-side memset).
//
// ── sw COLLAPSE (named forks, TEXTURE_COMPUTE_SEAM §3) ───────────────────────────────────────────────
// • getimagebrightness-flatten-groupshared-reduction: TiXL's per-threadgroup `localSum` partial sum is a
//   PERFORMANCE optimization — the final ResultBuffer[0] = Σ scaled over all in-bounds texels is IDENTICAL
//   whether accumulated via threadgroup-local partials then one device atomic per group, or a direct device
//   atomic per texel (integer add is associative + commutative). sw ports it as ONE direct per-texel device
//   atomic → same bit-exact total, no groupshared / barriers. Verified bit-for-bit by --selftest-mathv-
//   getimagebrightness (逐-texel CPU sum oracle).
// • getimagebrightness-texsize-from-texture-query: TiXL feeds textureWidth/Height into b0 from a separate
//   GetTextureSize op; Metal exposes get_width()/get_height() in-kernel, so the edge-guard reads them
//   directly (= depth-to-linear kernel's idiom) and the b0 CB carries ONLY scaleFactor.
// • edge-guard (HLSL Load-OOB-returns-0): DX11 InputTexture.Load past the extent returns 0 → contributes 0;
//   Metal texture.read OOB is undefined, so guard `dtid < extent` and skip (skip == add 0 == identical).
//
// PROVENANCE: hand-authored from the .hlsl (the kernel is short + has no transcendentals; the transpiler
// recipe in TEXTURE_COMPUTE_SEAM_SPEC §4.1 is unnecessary for this arithmetic-only body). Binding indices
// match buffer_ops_computeshaderstage.cpp's stage cook: CB(b0)→buffer(CS_CB_BASE=0), texture-SRV(t0)→
// texture(CS_TEX_SRV_BASE=0), UAV-buf(u0)→buffer(CS_UAV_BASE=12). The stage dispatches this kernel 2D over
// the SRV texture's W×H (fork computestage-buffer-stage-2d-from-srvtex-dispatch), tg 8×8 = HLSL numthreads.
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

kernel void computeshaderstage_getimagebrightness(
    constant uint& scaleFactor [[buffer(0)]],            // b0 ParamConstants.scaleFactor (texW/H via query)
    device atomic_uint* ResultBuffer [[buffer(12)]],     // u0 RWStructuredBuffer<uint> (CS_UAV_BASE)
    texture2d<float> InputTexture [[texture(0)]],        // t0 SRV (CS_TEX_SRV_BASE)
    uint3 DTid [[thread_position_in_grid]])
{
    // HLSL Load-OOB-returns-0 semantics: a thread past the texture extent contributes 0 → skip (== add 0).
    if (DTid.x >= InputTexture.get_width() || DTid.y >= InputTexture.get_height())
        return;
    float4 color = InputTexture.read(uint2(DTid.xy), 0);
    float luminance = (color.r + color.g + color.b) / 3.0f * color.a;   // .hlsl:34 verbatim
    uint scaledLuminance = (uint)(luminance * (float)scaleFactor);      // .hlsl:37 (float→uint truncates)
    atomic_fetch_add_explicit(ResultBuffer, scaledLuminance, memory_order_relaxed);  // .hlsl:39+48 folded
}
