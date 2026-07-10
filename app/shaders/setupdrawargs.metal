// setupdrawargs.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-4). NOT wired to any stage/owner yet (see runtime/setupdrawargs_params.h header
// note — pass 6 (last) of the DrawPointsDOF.t3 compound's six-pass sort-N family).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e SetupDrawArgs -S comp
//     -V --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/draw-sorted/sort-6-SetupDrawArgs.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM except:
//   (1) entry renamed SetupDrawArgs -> setupdrawargs (§10.5② entry name ITSELF was already
//       `SetupDrawArgs`, not `main` — confirmed via `grep numthreads -A2` before transpiling)
//   (2) [[buffer(N)]] literal indices replaced with the named SETUPDRAWARGS binding enum (SAME
//       numeric values the transpiler actually emitted — §10.5③: BucketPrefixSum=0/Params=1/
//       BucketCounter=2/DrawArgsBuffer=3, declaration order)
// No struct-packing fix needed (§10.5⑥⑦ N/A): only scalar uint buffer elements. No GetDimensions
// substitution needed (§10.5① N/A): no GetDimensions call. numthreads(1,1,1) in the source HLSL —
// this kernel is dispatched with EXACTLY ONE thread; there is no per-element loop or bounds guard.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/setupdrawargs_params.h"

using namespace metal;

struct DrawArgsLikeBuffer { uint _data[1]; };

kernel void setupdrawargs(
    device DrawArgsLikeBuffer&    BucketPrefixSum_1 [[buffer(SETUPDRAWARGS_BucketPrefixSum)]],
    constant SetupDrawArgsParams& P                 [[buffer(SETUPDRAWARGS_Params)]],
    device DrawArgsLikeBuffer&    BucketCounter     [[buffer(SETUPDRAWARGS_BucketCounter)]],
    device DrawArgsLikeBuffer&    DrawArgsBuffer    [[buffer(SETUPDRAWARGS_DrawArgsBuffer)]])
{
    DrawArgsBuffer._data[0] = (BucketPrefixSum_1._data[P.BucketCount - 1] + BucketCounter._data[P.BucketCount - 1]) * 6u;
    DrawArgsBuffer._data[1] = 1u;
    DrawArgsBuffer._data[2] = 0u;
    DrawArgsBuffer._data[3] = 0u;
    DrawArgsBuffer._data[4] = 0u;
}
