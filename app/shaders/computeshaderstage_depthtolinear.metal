// computeshaderstage_depthtolinear — the TEX-track compute-stage kernel for _ComputeDepthToLinear
// (texture-compute seam STAGE 2: 1SRV-tex + 1UAV-tex + b0 CB, the first SRV-tex-read tex-out kernel).
//
// TiXL authority (P5-safe source — ONLY the HLSL, never sw's own output):
//   external/tixl/Operators/Lib/Assets/shaders/img/post-fx/depth-to-linear.hlsl
//   Linearizes a depth buffer: reads Texture2D<float> InputTexture : register(t0), writes
//   RWTexture2D<float> OutputTexture : register(u0), driven by a b0 CB (Near/Far/OutrangeMin/
//   OutrangeMax/ClampRange/Mode). [numthreads(16,16,1)]; NO sampler (direct integer InputTexture[i.xy]).
//
// PROVENANCE (transpiler-first, per TEXTURE_COMPUTE_SEAM_SPEC §4.1 / MASTER_PLAN transpiler recipe):
// produced by the glslang(16.3.0)+spirv-cross(2026-04-30) recipe — `glslang -D --hlsl-iomap --amb
// --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V --target-env vulkan1.0 -I<shaders-root>` then
// `spirv-cross --msl --msl-version 20000`. The body below is the transpiler output VERBATIM. TWO adapter
// edits, both P5-safe:
//   (1) entry-point rename main0 → computeshaderstage_depthtolinear.
//   (2) HLSL PARSE-TRAP fix on the source before transpile: the .hlsl ternary false-branch writes
//       `: (c = (2.0*n)/(...))` — a redundant SELF-ASSIGNMENT to the very variable `c` being declared.
//       FXC/DXC accept it (lenient), glslang rejects `c` as unknown mid-declaration. The ternary result
//       is assigned to `c` regardless, so `(c = EXPR)` is SEMANTICALLY IDENTICAL to `(EXPR)` — the edit
//       removes the inner assignment only. (The CPU oracle mathv_ref_depthtolinear.h transcribes the
//       ORIGINAL .hlsl text including the no-op self-assign, so ref and kernel share ONE authority.)
// The transpiler assigns bindings: CB(b0)→buffer(0), InputTexture(t0,SRV)→texture(0),
// OutputTexture(u0,UAV,write)→texture(1). point_ops_computeshaderstagetex.cpp binds to those indices
// (kernel-metadata table: srvTexIndex=0, uavTexIndex=1, numthreads 16×16, R32Float out, 6-float CB).
//
// depth<0 SPECIAL CASE (verbatim from the .hlsl): writes a per-texel checker ((x+y)%16>0 ? 0 : 1) — a
// deterministic function of the thread coords, exercised by the mathv sweep's negative-depth band.
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct ParamConstants
{
    float Near;
    float Far;
    float OutrangeMin;
    float OutrangeMax;
    float ClampRange;
    float Mode;
};

kernel void computeshaderstage_depthtolinear(constant ParamConstants& _50 [[buffer(0)]], texture2d<float> InputTexture [[texture(0)]], texture2d<float, access::write> OutputTexture [[texture(1)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    do
    {
        uint2 _182 = uint2(InputTexture.get_width(), InputTexture.get_height());
        if ((gl_GlobalInvocationID.x >= _182.x) || (gl_GlobalInvocationID.y >= _182.y))
        {
            break;
        }
        float4 _205 = InputTexture.read(uint2(gl_GlobalInvocationID.xy), 0);
        float _206 = _205.x;
        if (_206 < 0.0)
        {
            OutputTexture.write(float4(float((((gl_GlobalInvocationID.x + gl_GlobalInvocationID.y) % 16u) > 0u) ? 0 : 1)), uint2(gl_GlobalInvocationID.xy));
            break;
        }
        float _251 = (_50.Mode < 0.5) ? (((-_50.Far) * _50.Near) / ((_206 * (_50.Far - _50.Near)) - _50.Far)) : ((2.0 * _50.Near) / ((_50.Far + _50.Near) - (_206 * (_50.Far - _50.Near))));
        float _289;
        if ((_50.OutrangeMin != 0.0) || (_50.OutrangeMax != 0.0))
        {
            _289 = (_251 - _50.OutrangeMin) / (_50.OutrangeMax - _50.OutrangeMin);
        }
        else
        {
            _289 = _251;
        }
        OutputTexture.write(float4((_50.ClampRange > 0.5) ? fast::clamp(_289, 0.0, 1.0) : _289), uint2(gl_GlobalInvocationID.xy));
        break;
    } while(false);
}
