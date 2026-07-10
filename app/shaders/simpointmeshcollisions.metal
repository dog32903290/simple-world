// simpointmeshcollisions.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-3). NOT wired to any stage/owner yet (see runtime/simpointmeshcollisions_params.h
// header note, incl. the mesh-CollapseVertices.hlsl candidate-swap record).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/sim/SimPointMeshCollisions.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM (including the full closestPointOnTriangle branchy geometry
// and the findClosestPointAndDistance linear scan loop, exactly as glslang unrolled them into nested
// ternANY-branch ifs) except:
//   (1) entry renamed main0 -> simpointmeshcollisions; [[buffer(N)]] literals replaced with the named
//       SPMC binding enum (SAME numeric values the transpiler actually emitted — §10.5③: Indices=0/
//       Vertices=1/Particles=2/Params=3); the TWO live GetDimensions calls (spvBufferSizeConstants[0]
//       for Indices, [2] for Particles) replaced with host-ABI FaceCount/PointCount fields (§10.2③/
//       §10.5①) — Vertices' GetDimensions call was already dead-code-eliminated by spirv-cross itself.
//   (2) ★packed_int3 stride fix (§10.5⑥, SAME transpiler gap as reversefacevertexindexorder.metal/
//       combineindexbuffers.metal): Indices' bare `int3 _data[1]` -> `packed_int3 _data[1]` (HLSL
//       StructuredBuffer<int3> is tightly 12-byte packed; Metal's bare int3 array rounds to 16-byte
//       stride, silently corrupting every element past index 0).
//   (3) ★PbrVertex packed_float3 struct fix (§10.5⑦, SAME transpiler gap as flipnormals.metal etc.):
//       Vertices' PbrVertex Position/Normal/Tangent/Bitangent declared packed_float3 — matches the true
//       80-byte SwVertex stride, no spvPaddedArrayElement wrapper. NOTE: the `Particle` struct did NOT
//       need this fix — spirv-cross correctly packed BOTH its float3 fields (Position/Velocity) because
//       neither is immediately followed by another float3 field (Radius/BirthTime scalars interleave) —
//       confirms §10.5⑦'s "consecutive float3 RUN" framing: the bug only bites when 2+ float3 fields
//       sit back-to-back, not every float3 field in general.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/simpointmeshcollisions_params.h"
#include "../src/runtime/tixl_point.h"  // Particle (TiXL-faithful 64B layout, already packed_float3-correct)

using namespace metal;

struct Indices { packed_int3 _data[1]; };

struct PbrVertex {
    packed_float3 Position;
    packed_float3 Normal;
    packed_float3 Tangent;
    packed_float3 Bitangent;
    float2 TexCoord;
    float2 TexCoord2;
    float Selected;
    packed_float3 ColorRGB;
};

struct Vertices { PbrVertex _data[1]; };
struct Particles { Particle _data[1]; };

constant float3 _862 = {};

kernel void simpointmeshcollisions(
    device Indices& Indices_1 [[buffer(SPMC_Indices)]],
    device Vertices& Vertices_1 [[buffer(SPMC_Vertices)]],
    device Particles& Particles_1 [[buffer(SPMC_Particles)]],
    constant SimPointMeshCollisionsParams& P [[buffer(SPMC_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    do
    {
        if (gl_GlobalInvocationID.x >= P.PointCount)
        {
            break;
        }
        uint _496 = P.FaceCount;
        float3 _861;
        _861 = _862;
        float _912;
        float3 _915;
        uint _860 = 0u;
        float _885 = 99999.0;
        for (; _860 < _496; _885 = _912, _861 = _915, _860++)
        {
            int3 face = int3(Indices_1._data[_860]);
            float3 _629 = float3(Vertices_1._data[face.y].Position) - float3(Vertices_1._data[face.x].Position);
            float3 _632 = float3(Vertices_1._data[face.z].Position) - float3(Vertices_1._data[face.x].Position);
            float3 _635 = float3(Vertices_1._data[face.x].Position) - float3(Particles_1._data[gl_GlobalInvocationID.x].Position);
            float _638 = dot(_629, _629);
            float _641 = dot(_629, _632);
            float _644 = dot(_632, _632);
            float _647 = dot(_629, _635);
            float _650 = dot(_632, _635);
            float _657 = (_638 * _644) - (_641 * _641);
            float _664 = (_641 * _650) - (_644 * _647);
            float _671 = (_641 * _647) - (_638 * _650);
            float _863;
            float _872;
            if ((_664 + _671) < _657)
            {
                float _864;
                float _873;
                if (_664 < 0.0)
                {
                    float _865;
                    float _874;
                    if (_671 < 0.0)
                    {
                        float _866;
                        float _875;
                        if (_647 < 0.0)
                        {
                            _875 = 0.0;
                            _866 = fast::clamp((-_647) / _638, 0.0, 1.0);
                        }
                        else
                        {
                            _875 = fast::clamp((-_650) / _644, 0.0, 1.0);
                            _866 = 0.0;
                        }
                        _874 = _875;
                        _865 = _866;
                    }
                    else
                    {
                        _874 = fast::clamp((-_650) / _644, 0.0, 1.0);
                        _865 = 0.0;
                    }
                    _873 = _874;
                    _864 = _865;
                }
                else
                {
                    float _867;
                    float _876;
                    if (_671 < 0.0)
                    {
                        _876 = 0.0;
                        _867 = fast::clamp((-_647) / _638, 0.0, 1.0);
                    }
                    else
                    {
                        float _717 = 1.0 / _657;
                        _876 = _671 * _717;
                        _867 = _664 * _717;
                    }
                    _873 = _876;
                    _864 = _867;
                }
                _872 = _873;
                _863 = _864;
            }
            else
            {
                float _868;
                float _877;
                if (_664 < 0.0)
                {
                    float _732 = _641 + _647;
                    float _735 = _644 + _650;
                    float _869;
                    float _878;
                    if (_735 > _732)
                    {
                        float _752 = fast::clamp((_735 - _732) / ((_638 - (2.0 * _641)) + _644), 0.0, 1.0);
                        _878 = 1.0 - _752;
                        _869 = _752;
                    }
                    else
                    {
                        _878 = fast::clamp((-_650) / _644, 0.0, 1.0);
                        _869 = 0.0;
                    }
                    _877 = _878;
                    _868 = _869;
                }
                else
                {
                    float _870;
                    float _879;
                    if (_671 < 0.0)
                    {
                        float _871;
                        float _880;
                        if ((_638 + _647) > (_641 + _650))
                        {
                            float _790 = fast::clamp((((_644 + _650) - _641) - _647) / ((_638 - (2.0 * _641)) + _644), 0.0, 1.0);
                            _880 = 1.0 - _790;
                            _871 = _790;
                        }
                        else
                        {
                            _880 = 0.0;
                            _871 = fast::clamp((-_650) / _644, 0.0, 1.0);
                        }
                        _879 = _880;
                        _870 = _871;
                    }
                    else
                    {
                        float _817 = fast::clamp((((_644 + _650) - _641) - _647) / ((_638 - (2.0 * _641)) + _644), 0.0, 1.0);
                        _879 = 1.0 - _817;
                        _870 = _817;
                    }
                    _877 = _879;
                    _868 = _870;
                }
                _872 = _877;
                _863 = _868;
            }
            float3 _831 = (float3(Vertices_1._data[face.x].Position) + (_629 * _863)) + (_632 * _872);
            float _592 = length(_831 - float3(Particles_1._data[gl_GlobalInvocationID.x].Position));
            bool _595 = _592 < _885;
            _912 = _595 ? _592 : _885;
            _915 = select(_861, _831, bool3(_595));
        }
        float3 _529 = float3(Particles_1._data[gl_GlobalInvocationID.x].Position) - _861;
        float _531 = length(_529);
        if (isnan(_531) || (_531 < 0.001000000047497451305389404296875))
        {
            break;
        }
        if (_531 > Particles_1._data[gl_GlobalInvocationID.x].Radius)
        {
            break;
        }
        Particles_1._data[gl_GlobalInvocationID.x].Velocity = packed_float3(float3(Particles_1._data[gl_GlobalInvocationID.x].Velocity) + (fast::normalize(_529) * P.Bounciness));
        break;
    } while(false);
}
