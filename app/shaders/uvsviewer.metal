// uvsviewer.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10, 2026-07-10
// wave-3). NOT wired to any stage/owner yet (see runtime/uvsviewer_params.h header note, incl. the
// off-by-one NAMED FORK).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/fx/mesh-UVs.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM (including the `>` not `>=` boundary guard — the off-by-one
// NAMED FORK documented in params.h, faithfully NOT "fixed") except:
//   (1) entry renamed main0 -> uvsviewer; [[buffer(N)]] literals replaced with the named UVSVIEWER
//       binding enum (SAME numeric values the transpiler actually emitted — §10.5③: ResultVertices=0/
//       VerticesA=1/Params=2, NOT declaration order); the dead VerticesA.GetDimensions call was already
//       eliminated by spirv-cross itself (only ONE spvBufferSizeConstants slot appeared in the raw
//       output), replaced here with the host-ABI Count field (§10.2③/§10.5①)
//   (2) ★PbrVertex packed_float3 struct fix (SAME transpiler gap as flipnormals.metal/
//       combinevertexbuffers.metal's header notes, MATH_VERIFY_WORKFLOW.md §10.5⑦): spirv-cross's raw
//       PbrVertex left Position/Normal/Tangent/Bitangent as plain float3 (only ColorRGB got
//       packed_float3), landing on the wrong 112-byte apparent stride vs the real 80-byte PbrVertex.cs
//       layout (runtime/sw_mesh.h SwVertex). Fixed here by declaring all four vec3 fields
//       packed_float3 — no spvPaddedArrayElement wrapper needed once the struct's true size is correct.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/uvsviewer_params.h"

using namespace metal;

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

struct ResultVertices { PbrVertex _data[1]; };
struct VerticesA       { PbrVertex _data[1]; };

kernel void uvsviewer(
    device ResultVertices& ResultVertices_1 [[buffer(UVSVIEWER_ResultVertices)]],
    device VerticesA& VerticesA_1 [[buffer(UVSVIEWER_VerticesA)]],
    constant UvsViewerParams& P [[buffer(UVSVIEWER_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    do
    {
        if (gl_GlobalInvocationID.x > P.Count)   // ★VERBATIM `>` not `>=` — see header NAMED FORK
        {
            break;
        }
        float3 _317 = float3(VerticesA_1._data[gl_GlobalInvocationID.x].Position);
        float3 _320 = float3(VerticesA_1._data[gl_GlobalInvocationID.x].Normal);
        float3 _323 = float3(VerticesA_1._data[gl_GlobalInvocationID.x].Tangent);
        float3 _326 = float3(VerticesA_1._data[gl_GlobalInvocationID.x].Bitangent);
        float2 _329 = VerticesA_1._data[gl_GlobalInvocationID.x].TexCoord;
        float _335 = VerticesA_1._data[gl_GlobalInvocationID.x].Selected;
        if (P.SwitchUV > 0.5)
        {
            ResultVertices_1._data[gl_GlobalInvocationID.x].Position = packed_float3(mix(_317, float3(VerticesA_1._data[gl_GlobalInvocationID.x].TexCoord2, 0.0), float3(P.BlendFactor)));
        }
        else
        {
            ResultVertices_1._data[gl_GlobalInvocationID.x].Position = packed_float3(mix(_317, float3(_329, 0.0), float3(P.BlendFactor)));
        }
        ResultVertices_1._data[gl_GlobalInvocationID.x].Normal = packed_float3(mix(_320, float3(0.0, 0.0, 1.0), float3(P.BlendFactor)));
        ResultVertices_1._data[gl_GlobalInvocationID.x].Tangent = packed_float3(mix(_323, float3(1.0, 0.0, 0.0), float3(P.BlendFactor)));
        ResultVertices_1._data[gl_GlobalInvocationID.x].Bitangent = packed_float3(mix(_326, float3(0.0, 1.0, 0.0), float3(P.BlendFactor)));
        ResultVertices_1._data[gl_GlobalInvocationID.x].TexCoord = _329;
        ResultVertices_1._data[gl_GlobalInvocationID.x].Selected = _335;
        break;
    } while(false);
}
