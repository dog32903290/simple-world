// appendpoints.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10, 2026-07-10).
// NOT wired to any stage/owner yet (see runtime/appendpoints_params.h header note).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/combine/AppendPoints.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body below is the transpiler output VERBATIM. The ONLY adapter edits (§10.2③): (1) entry renamed
// main0 -> appendpoints, (2) [[buffer(N)]] literal indices replaced with the named AppendPointsBinding
// enum (same numeric values: 0=Params,1=ResultPoints,2=Points1,3=Points2 — this op's actual transpiled
// order, per §10.5 限制③ "binding 順序逐顆讀"), (3) #include of the shared params header, (4) Params
// struct type -> AppendPointsParams. No operator/expression inside main() was touched — including the
// `i.x > CountA+CountB` OOB-write-then-return branch and the two mid-copy NaN-boundary writes (params.h
// header note): the transpiler reproduces them because that's literally what AppendPoints.hlsl says.
//
// LegacyPoint below == SwPoint's exact 64-byte layout (external/tixl shared/point.hlsl; see
// runtime/tixl_point.h) — field names differ (W/Stretch/Selected vs FX1/Scale/FX2) but stride/offsets
// are identical, so the host fills/reads this buffer AS a SwPoint buffer with zero repacking. Note
// the second SRV variable (`Points2`) reuses the SAME transpiler-emitted struct TYPE name `Points1`
// (spirv-cross names the type after the first buffer of that shape) — this is a type-name coincidence,
// not an aliasing bug; the two are distinct buffer bindings (2 and 3).
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/appendpoints_params.h"

using namespace metal;

struct LegacyPoint
{
    packed_float3 Position;
    float W;
    float4 Rotation;
    float4 Color;
    packed_float3 Stretch;
    float Selected;
};

struct ResultPoints
{
    LegacyPoint _data[1];
};

struct Points1
{
    LegacyPoint _data[1];
};

kernel void appendpoints(constant AppendPointsParams& _21 [[buffer(APPENDPOINTS_Params)]],
                         device ResultPoints& ResultPoints_1 [[buffer(APPENDPOINTS_ResultPoints)]],
                         device Points1& Points1_1 [[buffer(APPENDPOINTS_Points1)]],
                         device Points1& Points2 [[buffer(APPENDPOINTS_Points2)]],
                         uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    float NAN0 = as_type<float>(0x7fc00000u /* nan */);
    do
    {
        uint _136 = uint(_21.CountA + 1.5);
        if (float(gl_GlobalInvocationID.x) > (_21.CountA + _21.CountB))
        {
            ResultPoints_1._data[gl_GlobalInvocationID.x].W = NAN0;
            break;
        }
        if (gl_GlobalInvocationID.x < _136)
        {
            ResultPoints_1._data[gl_GlobalInvocationID.x] = Points1_1._data[gl_GlobalInvocationID.x];
            if (gl_GlobalInvocationID.x == (_136 - 1u))
            {
                ResultPoints_1._data[gl_GlobalInvocationID.x].W = NAN0;
            }
        }
        else
        {
            ResultPoints_1._data[gl_GlobalInvocationID.x] = Points2._data[gl_GlobalInvocationID.x - _136];
            if (gl_GlobalInvocationID.x == (_136 + uint(_21.CountB + 0.5)))
            {
                ResultPoints_1._data[gl_GlobalInvocationID.x].W = NAN0;
            }
        }
        break;
    } while(false);
}
