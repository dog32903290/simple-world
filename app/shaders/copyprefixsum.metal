// copyprefixsum.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-4). NOT wired to any stage/owner yet (see runtime/copyprefixsum_params.h header
// note — pass 4 of the DrawPointsDOF.t3 compound's six-pass sort-N family).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e CopyPrefixSum -S comp
//     -V --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/draw-sorted/sort-4-CopyPrefixSum.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM except:
//   (1) entry renamed CopyPrefixSum -> copyprefixsum (§10.5② entry name ITSELF was already
//       `CopyPrefixSum`, not `main` — confirmed via `grep numthreads -A2` before transpiling)
//   (2) [[buffer(N)]] literal indices replaced with the named COPYPREFIXSUM binding enum (SAME
//       numeric values the transpiler actually emitted — §10.5③: Params=0/BucketOffsetSum=1/
//       BucketPrefixSum=2, read off the raw output verbatim, NOT declaration order)
// No struct-packing fix needed (§10.5⑥⑦ N/A): only scalar uint buffer elements. No GetDimensions
// substitution needed (§10.5① N/A): no GetDimensions call in this kernel.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/copyprefixsum_params.h"

using namespace metal;

struct BucketOffsetSum { uint _data[1]; };

kernel void copyprefixsum(
    constant CopyPrefixSumParams& P                [[buffer(COPYPREFIXSUM_Params)]],
    device BucketOffsetSum&       BucketOffsetSum_1 [[buffer(COPYPREFIXSUM_BucketOffsetSum)]],
    device BucketOffsetSum&       BucketPrefixSum   [[buffer(COPYPREFIXSUM_BucketPrefixSum)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x < uint(P.BucketCount))
    {
        BucketOffsetSum_1._data[gl_GlobalInvocationID.x] = BucketPrefixSum._data[gl_GlobalInvocationID.x];
    }
}
