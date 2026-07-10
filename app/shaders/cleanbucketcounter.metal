// cleanbucketcounter.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-4). NOT wired to any stage/owner yet (see runtime/cleanbucketcounter_params.h
// header note — this is ONE of six numbered sort-N passes the DrawPointsDOF.t3 compound dispatches).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e ClearBucketCounter -S
//     comp -V --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/draw-sorted/sort-1-CleanBucketCounter.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM except:
//   (1) entry renamed ClearBucketCounter -> cleanbucketcounter (sw lowercase-kernel-name convention;
//       §10.5② entry name ITSELF was already `ClearBucketCounter`, not `main` — confirmed via
//       `grep numthreads -A2` before transpiling, -e flag set accordingly)
//   (2) [[buffer(N)]] literal indices replaced with the named CLEANBUCKETCOUNTER binding enum (SAME
//       numeric values the transpiler actually emitted — §10.5③: Params=0/BucketCounter=1)
// No struct-packing fix needed (§10.5⑥⑦ N/A): the only buffer element is a scalar uint, never a
// vec3. No GetDimensions substitution needed (§10.5① N/A): this kernel takes no SRV and never calls
// GetDimensions.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/cleanbucketcounter_params.h"

using namespace metal;

struct BucketCounter { uint _data[1]; };

kernel void cleanbucketcounter(
    constant CleanBucketCounterParams& P             [[buffer(CLEANBUCKETCOUNTER_Params)]],
    device BucketCounter&              BucketCounter_1 [[buffer(CLEANBUCKETCOUNTER_BucketCounter)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x < uint(P.BucketCount))
    {
        BucketCounter_1._data[gl_GlobalInvocationID.x] = 0u;
    }
}
