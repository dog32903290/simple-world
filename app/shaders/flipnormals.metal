// flipnormals.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10, 2026-07-10
// wave-2). NOT wired to any stage/owner yet (see runtime/flipnormals_params.h header note, incl. the
// TexCoord2 NOTED-QUIRK).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-FlipNormals.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output's OPERATOR LOGIC verbatim (including the TexCoord2 omission — the raw
// output's 7-of-8-field assignment list matches the HLSL exactly, faithfully NOT touching TexCoord2).
// Adapter fixes applied (documented, not silent — same two classes as reversefacevertexindexorder.
// metal / combinevertexbuffers.metal's header notes):
//   (1) entry renamed main0 -> flipnormals; [[buffer(N)]] replaced with the named FLIPNORMALS enum;
//       empty HLSL cbuffer + GetDimensions/spvBufferSizeConstants replaced with a host-ABI Count param
//   (2) ★PbrVertex packed_float3 struct fix (SAME transpiler gap as combinevertexbuffers.metal):
//       spirv-cross's raw PbrVertex left Position/Normal/Tangent/Bitangent as plain float3 (only
//       ColorRGB got packed_float3), landing on the wrong 112-byte apparent stride vs the real 80-byte
//       PbrVertex.cs layout (runtime/sw_mesh.h SwVertex). Fixed here by declaring all four vec3 fields
//       packed_float3, matching sw_mesh.h byte-for-byte — no spvPaddedArrayElement wrapper needed once
//       the struct's true size is correct.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/flipnormals_params.h"

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

struct SourceVerts { PbrVertex _data[1]; };
struct ResultVerts { PbrVertex _data[1]; };

kernel void flipnormals(
    device SourceVerts& SourceVerts_1 [[buffer(FLIPNORMALS_SourceVerts)]],
    device ResultVerts& ResultVerts_1 [[buffer(FLIPNORMALS_ResultVerts)]],
    constant FlipNormalsParams& P [[buffer(FLIPNORMALS_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x >= P.Count) {
        return;
    }
    ResultVerts_1._data[gl_GlobalInvocationID.x].Position = SourceVerts_1._data[gl_GlobalInvocationID.x].Position;
    ResultVerts_1._data[gl_GlobalInvocationID.x].Normal = -float3(SourceVerts_1._data[gl_GlobalInvocationID.x].Normal);
    ResultVerts_1._data[gl_GlobalInvocationID.x].Tangent = -float3(SourceVerts_1._data[gl_GlobalInvocationID.x].Tangent);
    ResultVerts_1._data[gl_GlobalInvocationID.x].Bitangent = SourceVerts_1._data[gl_GlobalInvocationID.x].Bitangent;
    ResultVerts_1._data[gl_GlobalInvocationID.x].TexCoord = SourceVerts_1._data[gl_GlobalInvocationID.x].TexCoord;
    ResultVerts_1._data[gl_GlobalInvocationID.x].Selected = SourceVerts_1._data[gl_GlobalInvocationID.x].Selected;
    ResultVerts_1._data[gl_GlobalInvocationID.x].ColorRGB = SourceVerts_1._data[gl_GlobalInvocationID.x].ColorRGB;
    // TexCoord2: intentionally NOT assigned — matches HLSL:21-27 verbatim (NOTED-QUIRK, see params.h).
}
