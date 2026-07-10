// deformmesh.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10, 2026-07-10
// wave-3). NOT wired to any stage/owner yet (see runtime/deformmesh_params.h header note, incl. the
// TwistAxis-out-of-range AMBIGUITY-PINNED UB resolution).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Deform.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM (including the `radians()` helper spirv-cross injected, the
// TaperAxis/TwistAxis switch statements exactly as lowered, and the `constant float3 _808 = {}` zero
// fill for TwistAxis's undefined default case — see params.h header) except:
//   (1) entry renamed main0 -> deformmesh; [[buffer(N)]] literals replaced with the named DEFORMMESH
//       binding enum (SAME numeric values the transpiler actually emitted — §10.5③: Params=0/
//       SourceVerts=1/ResultVerts=2); the GetDimensions call (spvBufferSizeConstants[1], NOT [0] —
//       the size-constant array index tracks the AFFECTED buffer's own binding number, confirmed by
//       comparing against uvsviewer.metal's [0]/transformmeshuvs' analogous case) replaced with the
//       host-ABI Count field appended to Params (§10.2③/§10.5①)
//   (2) Params struct fields kept VERBATIM including spirv-cross's own correct cbuffer padding
//       (params.h header note — this is NOT the §10.5⑥⑦ storage-buffer packing bug; the constant-
//       buffer packing here was already right)
//   (3) ★PbrVertex packed_float3 struct fix (SAME transpiler gap as flipnormals.metal/uvsviewer.metal
//       §10.5⑦): all four vec3 fields (Position/Normal/Tangent/Bitangent) declared packed_float3,
//       matching the true 80-byte SwVertex stride — no spvPaddedArrayElement wrapper needed.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/deformmesh_params.h"

using namespace metal;

// Implementation of the GLSL radians() function (spirv-cross-injected helper, kept verbatim).
template<typename T>
inline T radians(T d)
{
    return d * T(0.01745329251);
}

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

struct SourceVerts { PbrVertex _data[1]; };
struct ResultVerts { PbrVertex _data[1]; };

constant float3 _808 = {};

kernel void deformmesh(
    constant DeformMeshParams& P [[buffer(DEFORMMESH_Params)]],
    device SourceVerts& SourceVerts_1 [[buffer(DEFORMMESH_SourceVerts)]],
    device ResultVerts& ResultVerts_1 [[buffer(DEFORMMESH_ResultVerts)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    do
    {
        if (gl_GlobalInvocationID.x >= P.Count)
        {
            break;
        }
        float _439 = (P.UseVertexSelection > 0.5) ? SourceVerts_1._data[gl_GlobalInvocationID.x].Selected : 1.0;
        float3 _590 = float3(SourceVerts_1._data[gl_GlobalInvocationID.x].Position) - float3(P.Pivot);
        float3 _461 = mix(float3(SourceVerts_1._data[gl_GlobalInvocationID.x].Position), mix(float3(SourceVerts_1._data[gl_GlobalInvocationID.x].Position), _590 * (P.Radius / length(_590)), float3(P.Spherize)), float3(_439));
        float3 _805;
        switch (int(P.TaperAxis))
        {
            case 0:
            {
                float2 _471 = P.Taper2 * P.TaperAmount;
                float _473 = _461.x;
                float2 _477 = _461.yz * float2(1.0 - (_471.x * _473), 1.0 - (_471.y * _473));
                float3 _754 = _461;
                _754.y = _477.x;
                _754.z = _477.y;
                _805 = _754;
                break;
            }
            case 1:
            {
                float2 _487 = P.Taper2 * P.TaperAmount;
                float _489 = _461.y;
                float2 _493 = _461.xz * float2(1.0 - (_487.x * _489), 1.0 - (_487.y * _489));
                float3 _761 = _461;
                _761.x = _493.x;
                _761.z = _493.y;
                _805 = _761;
                break;
            }
            case 2:
            {
                float2 _503 = P.Taper2 * P.TaperAmount;
                float _505 = _461.z;
                float2 _509 = _461.xy * float2(1.0 - (_503.x * _505), 1.0 - (_503.y * _505));
                float3 _768 = _461;
                _768.x = _509.x;
                _768.y = _509.y;
                _805 = _768;
                break;
            }
            default:
            {
                _805 = _461;
                break;
            }
        }
        float3 _519 = mix(_461, _805, float3(_439));
        float _522 = radians(P.TwistAmount);
        float3 _647 = _519 - float3(P.TwistPivot);
        float3 _806;
        switch (int(P.TwistAxis))
        {
            case 0:
            {
                float _655 = _647.x * _522;
                float _657 = cos(_655);
                float _659 = sin(_655);
                _806 = float3(_647.x, (_647.y * _657) - (_647.z * _659), (_647.y * _659) + (_647.z * _657));
                break;
            }
            case 1:
            {
                float _685 = _647.y * _522;
                float _687 = cos(_685);
                float _689 = sin(_685);
                _806 = float3((_647.x * _687) - (_647.z * _689), _647.y, (_647.x * _689) + (_647.z * _687));
                break;
            }
            case 2:
            {
                float _715 = _647.z * _522;
                float _717 = cos(_715);
                float _719 = sin(_715);
                _806 = float3((_647.x * _717) - (_647.y * _719), (_647.x * _719) + (_647.y * _717), _647.z);
                break;
            }
            default:
            {
                _806 = _808;
                break;
            }
        }
        ResultVerts_1._data[gl_GlobalInvocationID.x].Position = packed_float3(mix(_519, _806 + float3(P.TwistPivot), float3(_439)));
        ResultVerts_1._data[gl_GlobalInvocationID.x].Normal = SourceVerts_1._data[gl_GlobalInvocationID.x].Normal;
        ResultVerts_1._data[gl_GlobalInvocationID.x].Tangent = SourceVerts_1._data[gl_GlobalInvocationID.x].Tangent;
        ResultVerts_1._data[gl_GlobalInvocationID.x].Bitangent = SourceVerts_1._data[gl_GlobalInvocationID.x].Bitangent;
        ResultVerts_1._data[gl_GlobalInvocationID.x].TexCoord = SourceVerts_1._data[gl_GlobalInvocationID.x].TexCoord;
        ResultVerts_1._data[gl_GlobalInvocationID.x].TexCoord2 = SourceVerts_1._data[gl_GlobalInvocationID.x].TexCoord2;
        ResultVerts_1._data[gl_GlobalInvocationID.x].Selected = SourceVerts_1._data[gl_GlobalInvocationID.x].Selected;
        ResultVerts_1._data[gl_GlobalInvocationID.x].ColorRGB = packed_float3(float3(SourceVerts_1._data[gl_GlobalInvocationID.x].ColorRGB));
        break;
    } while(false);
}
