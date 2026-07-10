// pointsimulation.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10). NOT wired to any stage/owner yet (see runtime/pointsimulation_params.h header note).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/combine/PointSimulation.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body below is the transpiler output VERBATIM except the §10.5 限制① GetDimensions substitution and
// the standard §10.2③ adapter edits: (1) entry renamed main0 -> pointsimulation, (2) [[buffer(N)]]
// literal indices replaced with the named PointSimulationBinding enum (same numeric values: 0=Params,
// 1=ResultPoints, 2=SourcePoints), (3) #include of the shared params header, (4) the local `Params`
// struct replaced with the ABI header's PointSimulationParams (same MixOriginal/Reset field names —
// zero call-site edits below `_166.MixOriginal`/`_166.Reset`), (5) GetDimensions substitution: the
// transpiler's `constant uint* spvBufferSizeConstants [[buffer(25)]]` parameter + `constant uint&
// SourcePoints_1BufferSize = spvBufferSizeConstants[2];` is replaced with a ONE-LINE host-ABI
// computation (`_166.Count * 64u`, SwPoint's 64-byte stride) — AddNoise's precedent for this exact
// limitation (MATH_VERIFY_WORKFLOW.md §10.5①). No other operator/expression inside main() was
// touched — including the isnan-triple-OR reset branch and the inlined qSlerp (all math verbatim).
//
// LegacyPoint below == SwPoint's exact 64-byte layout (external/tixl shared/point.hlsl; see
// runtime/tixl_point.h) — field names differ (W/Stretch/Selected vs FX1/Scale/FX2) but stride/offsets
// are identical, so the host fills/reads this buffer AS a SwPoint buffer with zero repacking.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/pointsimulation_params.h"

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

struct SourcePoints
{
    LegacyPoint _data[1];
};

kernel void pointsimulation(constant PointSimulationParams& _166 [[buffer(POINTSIMULATION_Params)]],
                            device ResultPoints& ResultPoints_1 [[buffer(POINTSIMULATION_ResultPoints)]],
                            device SourcePoints& SourcePoints_1 [[buffer(POINTSIMULATION_SourcePoints)]],
                            uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint SourcePoints_1BufferSize = _166.Count * 64u;  // §10.5① GetDimensions substitution, see header
    do
    {
        if (_166.Reset > 0.5)
        {
            ResultPoints_1._data[gl_GlobalInvocationID.x] = SourcePoints_1._data[gl_GlobalInvocationID.x];
            break;
        }
        if (gl_GlobalInvocationID.x >= ((SourcePoints_1BufferSize - 0) / 64))
        {
            break;
        }
        float _378 = ResultPoints_1._data[gl_GlobalInvocationID.x].W;
        float _382 = SourcePoints_1._data[gl_GlobalInvocationID.x].W;
        if ((isnan(_382) || isnan(_378)) || isnan(((device float*)&ResultPoints_1._data[gl_GlobalInvocationID.x].Position)[0u]))
        {
            ResultPoints_1._data[gl_GlobalInvocationID.x] = SourcePoints_1._data[gl_GlobalInvocationID.x];
            break;
        }
        ResultPoints_1._data[gl_GlobalInvocationID.x].W = mix(_378, _382, _166.MixOriginal);
        ResultPoints_1._data[gl_GlobalInvocationID.x].Position = mix(float3(ResultPoints_1._data[gl_GlobalInvocationID.x].Position), float3(SourcePoints_1._data[gl_GlobalInvocationID.x].Position), float3(_166.MixOriginal));
        ResultPoints_1._data[gl_GlobalInvocationID.x].Color = mix(ResultPoints_1._data[gl_GlobalInvocationID.x].Color, SourcePoints_1._data[gl_GlobalInvocationID.x].Color, float4(_166.MixOriginal));
        ResultPoints_1._data[gl_GlobalInvocationID.x].Stretch = mix(float3(ResultPoints_1._data[gl_GlobalInvocationID.x].Stretch), float3(SourcePoints_1._data[gl_GlobalInvocationID.x].Stretch), float3(_166.MixOriginal));
        ResultPoints_1._data[gl_GlobalInvocationID.x].Selected = mix(ResultPoints_1._data[gl_GlobalInvocationID.x].Selected, SourcePoints_1._data[gl_GlobalInvocationID.x].Selected, _166.MixOriginal);
        float4 _655;
        do
        {
            if (length(ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation) == 0.0)
            {
                if (length(SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation) == 0.0)
                {
                    _655 = float4(0.0, 0.0, 0.0, 1.0);
                    break;
                }
                _655 = SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation;
                break;
            }
            else
            {
                if (length(SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation) == 0.0)
                {
                    _655 = ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation;
                    break;
                }
            }
            float _525 = (ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation.w * SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.w) + dot(ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz, SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz);
            float _650;
            float4 _654;
            if ((_525 >= 1.0) || (_525 <= (-1.0)))
            {
                _655 = ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation;
                break;
            }
            else
            {
                if (_525 < 0.0)
                {
                    _654 = float4(-SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz, -SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.w);
                    _650 = -_525;
                }
                else
                {
                    _654 = SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation;
                    _650 = _525;
                }
            }
            float _651;
            float _652;
            if (_650 < 0.9900000095367431640625)
            {
                float _558 = acos(_650);
                float _562 = 1.0 / sin(_558);
                _652 = sin(_558 * _166.MixOriginal) * _562;
                _651 = sin(_558 * (1.0 - _166.MixOriginal)) * _562;
            }
            else
            {
                _652 = _166.MixOriginal;
                _651 = 1.0 - _166.MixOriginal;
            }
            float4 _602 = float4((ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz * _651) + (_654.xyz * _652), (_651 * ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation.w) + (_652 * _654.w));
            if (length(_602) > 0.0)
            {
                _655 = fast::normalize(_602);
                break;
            }
            _655 = float4(0.0, 0.0, 0.0, 1.0);
            break;
        } while(false);
        ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation = _655;
        break;
    } while(false);
}
