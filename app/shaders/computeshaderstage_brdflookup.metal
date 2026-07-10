// computeshaderstage_brdflookup — the TEX-track compute-stage kernel for _ComputeBRDFLookup.
//
// TiXL authority (P5-safe source — ONLY the HLSL, never sw's own output):
//   external/tixl/Operators/Lib/Assets/shaders/3d/rendering/ComputeBrdfLookupTexture-cs.hlsl
//   Pre-integrates the Cook-Torrance specular BRDF into a split-sum (DFG1,DFG2) 2D LUT.
//   RWTexture2D<float4> LUT : register(u0); [numthreads(32,32,1)]; NO SRV / NO CB / NO sampler.
//   Output = a DETERMINISTIC function of (ThreadID.x/W, ThreadID.y/H) — 1024-sample Hammersley GGX.
//
// PROVENANCE (transpiler-first, per TEXTURE_COMPUTE_SEAM_SPEC §4.1): produced by the glslang(16.3.0)+
// spirv-cross(1.4.350.1) recipe — `glslang -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24
// -e main -S comp -V --target-env vulkan1.0 -I<shaders-root>` then `spirv-cross --msl
// --msl-version 20000`. The body below is the transpiler output VERBATIM; the ONLY adapter edit is the
// entry-point rename main0 → computeshaderstage_brdflookup (this kernel has no CB/SRV/sampler, so no
// cbuffer/signature rebuild was needed). Verified line-by-line against the HLSL (radicalInverse_VdC
// bit-reverse, sampleGGX alpha=roughness², Schlick-GGX IBL k=roughness²/2, split-sum accumulate).
//
// SEAM: bound by point_ops_computeshaderstagetex.cpp — the allocated shaderWrite output texture is
// bound at texture(0) (== u0 / the LUT UAV), 2D dispatch over the output W×H (fork
// computestage-per-kernel-threadgroup: numthreads(32,32,1) in the kernel-metadata table).
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

kernel void computeshaderstage_brdflookup(texture2d<float, access::write> LUT [[texture(0)]],
                                          uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint2 _355 = uint2(LUT.get_width(), LUT.get_height());
    float _371 = float(gl_GlobalInvocationID.y) / float(_355.y);
    float _373 = fast::max(float(gl_GlobalInvocationID.x) / float(_355.x), 0.001000000047497451305389404296875);
    float3 _380 = float3(sqrt(1.0 - (_373 * _373)), 0.0, _373);
    float _588;
    float _589;
    _589 = 0.0;
    _588 = 0.0;
    float _591;
    float _592;
    for (uint _587 = 0u; _587 < 1024u; _589 = _592, _588 = _591, _587++)
    {
        uint _468 = (_587 << 16u) | (_587 >> 16u);
        uint _475 = ((_468 & 1431655765u) << 1u) | ((_468 & 2863311530u) >> 1u);
        uint _482 = ((_475 & 858993459u) << 2u) | ((_475 & 3435973836u) >> 2u);
        uint _489 = ((_482 & 252645135u) << 4u) | ((_482 & 4042322160u) >> 4u);
        float _499 = float(((_489 & 16711935u) << 8u) | ((_489 & 4278255360u) >> 8u)) * 2.3283064365386962890625e-10;
        float _508 = _371 * _371;
        float _519 = sqrt((1.0 - _499) / (1.0 + (((_508 * _508) - 1.0) * _499)));
        float _524 = sqrt(1.0 - (_519 * _519));
        float _526 = float(_587) * 0.0061359219253063201904296875;
        float3 _536 = float3(_524 * cos(_526), _524 * sin(_526), _519);
        float3 _401 = (_536 * (2.0 * dot(_380, _536))) - _380;
        float _403 = _401.z;
        float _409 = fast::max(dot(_380, _536), 0.0);
        if (_403 > 0.0)
        {
            float _549 = (_371 * _371) * 0.5;
            float _423 = (((_403 / ((_403 * (1.0 - _549)) + _549)) * (_373 / ((_373 * (1.0 - _549)) + _549))) * _409) / (_519 * _373);
            float _426 = powr(1.0 - _409, 5.0);
            _592 = _589 + (_426 * _423);
            _591 = _588 + ((1.0 - _426) * _423);
        }
        else
        {
            _592 = _589;
            _591 = _588;
        }
    }
    LUT.write(float4(float2(_588, _589) * 0.0009765625, 0.0, 1.0), uint2(uint2(gl_GlobalInvocationID.xy)));
}
