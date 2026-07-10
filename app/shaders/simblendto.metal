// simblendto.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10, 2026-07-10).
// NOT wired to any stage/owner yet (see runtime/simblendto_params.h header note).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/combine/SimBlendTo.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body below is the transpiler output VERBATIM. The ONLY adapter edits (§10.2③): (1) entry renamed
// main0 -> simblendto, (2) [[buffer(N)]] literal indices replaced with the named SimBlendToBinding
// enum (same numeric values), (3) #include of the shared params header, (4) Params struct type ->
// SimBlendToParams (ABI header, same 4-float layout as the transpiler's local `Params` struct). No
// operator/expression inside main() was touched — including the wA/wB-both-from-ResultPoints bug
// (params.h header note): the transpiler reproduces it because it's literally what SimBlendTo.hlsl
// says, and that fidelity is the entire point of this pipeline.
//
// LegacyPoint below == SwPoint's exact 64-byte layout (external/tixl shared/point.hlsl; see
// runtime/tixl_point.h) — field names differ (W/Stretch/Selected vs FX1/Scale/FX2) but stride/offsets
// are identical, so the host fills/reads this buffer AS a SwPoint buffer with zero repacking.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/simblendto_params.h"

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

struct PointsB
{
    LegacyPoint _data[1];
};

kernel void simblendto(device ResultPoints& ResultPoints_1 [[buffer(SIMBLENDTO_ResultPoints)]],
                       device PointsB& PointsB_1 [[buffer(SIMBLENDTO_PointsB)]],
                       constant SimBlendToParams& _63 [[buffer(SIMBLENDTO_Params)]],
                       uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    float _100 = ResultPoints_1._data[gl_GlobalInvocationID.x].W;
    float _104 = ResultPoints_1._data[gl_GlobalInvocationID.x].W;
    ResultPoints_1._data[gl_GlobalInvocationID.x].Position = mix(float3(ResultPoints_1._data[gl_GlobalInvocationID.x].Position), float3(PointsB_1._data[gl_GlobalInvocationID.x].Position), float3(_63.BlendFactor));
    ResultPoints_1._data[gl_GlobalInvocationID.x].W = mix(_100, _104, _63.BlendFactor);
}
