// simulatepoints.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-4). NOT wired to any stage/owner yet (see runtime/simulatepoints_params.h header
// note — a DIFFERENT, simpler op than sw's existing `particle_sim` kernel; not a duplicate).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/sim/simulate-points.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM except:
//   (1) entry renamed main0 -> simulatepoints; [[buffer(N)]] literals replaced with the named
//       SIMULATEPOINTS binding enum (SAME numeric values the transpiler actually emitted — §10.5③:
//       Particles=0/Params=1)
//   (2) GetDimensions (spvBufferSizeConstants[0] / 64) replaced with the host-ABI P.Count field
//       (§10.2③/§10.5①) — the `spvBufferSizeConstants [[buffer(25)]]` parameter is dropped entirely
//   (3) ★Particle packed_float3 struct fix (§10.5⑦, reused playbook): Position/Velocity declared
//       packed_float3 via `tixl_point.h`'s shared Particle type — matches the true 64-byte stride, no
//       raw-struct duplication.
#include <metal_stdlib>
#include <simd/simd.h>
#include "tixl_point.h"
#include "../src/runtime/simulatepoints_params.h"

using namespace metal;

struct Particles { Particle _data[1]; };

kernel void simulatepoints(
    device Particles&               Particles_1 [[buffer(SIMULATEPOINTS_Particles)]],
    constant SimulatePointsParams&  P           [[buffer(SIMULATEPOINTS_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x >= uint(P.Count))
    {
        return;
    }
    float _205 = Particles_1._data[gl_GlobalInvocationID.x].Radius;
    float4 _208 = Particles_1._data[gl_GlobalInvocationID.x].Rotation;
    float4 _211 = Particles_1._data[gl_GlobalInvocationID.x].Color;
    float3 _214 = float3(Particles_1._data[gl_GlobalInvocationID.x].Velocity);
    float _217 = Particles_1._data[gl_GlobalInvocationID.x].BirthTime;
    Particles_1._data[gl_GlobalInvocationID.x].Position = float3(Particles_1._data[gl_GlobalInvocationID.x].Position) + ((_214 * 0.00999999977648258209228515625) * P.Speed);
    Particles_1._data[gl_GlobalInvocationID.x].Radius = _205;
    Particles_1._data[gl_GlobalInvocationID.x].Rotation = _208;
    Particles_1._data[gl_GlobalInvocationID.x].Color = _211;
    Particles_1._data[gl_GlobalInvocationID.x].Velocity = _214 * (1.0 - P.Drag);
    Particles_1._data[gl_GlobalInvocationID.x].BirthTime = _217;
}
