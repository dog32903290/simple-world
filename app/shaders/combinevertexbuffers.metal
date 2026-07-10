// combinevertexbuffers.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-2). NOT wired to any stage/owner yet (see runtime/combinevertexbuffers_params.h).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/_/mesh-CombineVertexBuffers.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output's OPERATOR LOGIC verbatim, but the raw output needed TWO structural
// adapter fixes (not just literal renames) documented here rather than silently applied:
//   (1) ★TRANSPILER GAP — PbrVertex struct-layout corruption: spirv-cross emitted `PbrVertex` with
//       PLAIN `float3 Position/Normal/Tangent/Bitangent` (only the LAST field, ColorRGB, got
//       `packed_float3`) and wrapped every buffer element in a `spvPaddedArrayElement<PbrVertex,112>`
//       padding template. 112 != PbrVertex's real 80-byte stride (proven in runtime/sw_mesh.h against
//       TiXL's PbrVertex.cs FieldOffset layout) — every float3 field left plain gets Metal's 16-byte
//       array-of-vec3 rounding, inflating the apparent stride. Fix: ALL FOUR float3 fields become
//       packed_float3 (matching sw_mesh.h's SwVertex byte-for-byte), which restores the true 80-byte
//       stride and lets the padding-template wrapper (and its `.data` indirection / pointer-cast
//       component-write hack) be dropped entirely — same class of bug as int3's packed_int3 fix
//       (reversefacevertexindexorder.metal's header note), just on a NAMED struct instead of a bare
//       vector array.
//   (2) ★TRANSPILER GAP — b0/u0 binding COLLISION: the raw output aliased the (b0) Params cbuffer
//       onto the SAME buffer slot as the (u0) ResultVertices UAV (`spvBufferAliasSet0Binding0`,
//       Vulkan descriptor-aliasing hack) because glslang's --hlsl-iomap shift scheme collided the two
//       register classes for this specific 2-cbuffer+1-SRV+1-UAV combination. Fix: assign fresh,
//       non-colliding [[buffer(N)]] slots (CVB_* enum) instead of reusing the raw numbers.
// Narrow adapter edits within the (fixed) body: entry renamed main0 -> combinevertexbuffers; the two
// HLSL cbuffers flattened into one CombineVertexBuffersParams (§10.2③, same shape as AddNoise's dual-
// cbuffer reconstruction); GetDimensions replaced with a host-ABI Count field (§10.5①). No arithmetic
// was changed: `ResultVertices[targetIndex] = Vertices[i.x]; ResultVertices[targetIndex].Position.y +=
// DebugValue;` (HLSL:25-27) maps 1:1 to the two statements below.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/combinevertexbuffers_params.h"

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

struct Vertices       { PbrVertex _data[1]; };
struct ResultVertices { PbrVertex _data[1]; };

kernel void combinevertexbuffers(
    device Vertices& Vertices_1 [[buffer(CVB_Vertices)]],
    device ResultVertices& ResultVertices_1 [[buffer(CVB_ResultVertices)]],
    constant CombineVertexBuffersParams& P [[buffer(CVB_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x >= P.Count) {
        return;
    }
    uint targetIndex = gl_GlobalInvocationID.x + uint(P.StartVertexIndex);
    ResultVertices_1._data[targetIndex] = Vertices_1._data[gl_GlobalInvocationID.x];
    ResultVertices_1._data[targetIndex].Position.y += P.DebugValue;
}
