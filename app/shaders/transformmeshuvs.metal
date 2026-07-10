// transformmeshuvs.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-3). NOT wired to any stage/owner yet (see runtime/transformmeshuvs_params.h header
// note, incl. the MATRIX CONVENTION derivation — this op's #1 risk per §5's TransformFromClipSpace
// teaching).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-TransformUVs.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM (including the native `float4x4`/`v * TransformMatrix`
// expression — NOT rewritten to a flat-array multiply; see params.h ALGEBRA note for why the row-major
// host upload convention makes this produce the SAME result as the hand-written production kernel's
// `M·v` form) except:
//   (1) entry renamed main0 -> transformmeshuvs; [[buffer(N)]] literals replaced with the named TMUV
//       binding enum (SAME numeric values the transpiler actually emitted — §10.5③: SourceVerts=0/
//       Params=1/ResultVerts=2); GetDimensions (spvBufferSizeConstants[0]) replaced with the host-ABI
//       Count field appended to Params (§10.2③/§10.5①)
//   (2) ★PbrVertex packed_float3 struct fix (§10.5⑦): Position/Normal/Tangent/Bitangent declared
//       packed_float3 — matches the true 80-byte SwVertex stride, no spvPaddedArrayElement wrapper.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/transformmeshuvs_params.h"

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

kernel void transformmeshuvs(
    device SourceVerts& SourceVerts_1 [[buffer(TMUV_SourceVerts)]],
    constant TransformMeshUvsParams& P [[buffer(TMUV_Params)]],
    device ResultVerts& ResultVerts_1 [[buffer(TMUV_ResultVerts)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x >= P.Count) {
        return;
    }
    float s = (P.UseVertexSelection > 0.5) ? SourceVerts_1._data[gl_GlobalInvocationID.x].Selected : 1.0;
    float3 pos = float3(SourceVerts_1._data[gl_GlobalInvocationID.x].TexCoord, 0.0);
    float3 pos2 = float3(SourceVerts_1._data[gl_GlobalInvocationID.x].TexCoord2, 0.0);
    ResultVerts_1._data[gl_GlobalInvocationID.x] = SourceVerts_1._data[gl_GlobalInvocationID.x];
    if (P.ToTexCoord2 != 0.0) {
        ResultVerts_1._data[gl_GlobalInvocationID.x].TexCoord2 = mix(pos2, (float4(pos2, 1.0) * P.TransformMatrix).xyz, float3(s)).xy;
    } else {
        ResultVerts_1._data[gl_GlobalInvocationID.x].TexCoord = mix(pos, (float4(pos, 1.0) * P.TransformMatrix).xyz, float3(s)).xy;
    }
}
