// combineindexbuffers.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-2). NOT wired to any stage/owner yet (see runtime/combineindexbuffers_params.h).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/_/mesh-CombineIndexBuffers.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM except:
//   (1) entry renamed main0 -> combineindexbuffers
//   (2) [[buffer(N)]] literal indices replaced with the named CIB binding enum (SAME numeric values
//       the transpiler actually emitted: Indices=0, Params=1, ResultIndices=2 — §10.5③, not
//       declaration order)
//   (3) Params struct type -> CombineIndexBuffersParams (adds a host-ABI Count field, §10.2③/§10.5①,
//       replacing GetDimensions/spvBufferSizeConstants)
//   (4) packed_int3 stride fix (SAME transpiler gap as reversefacevertexindexorder.metal's header
//       note — StructuredBuffer<int3> must be packed_int3, not spirv-cross's bare int3, or writes
//       past element 0 land at the wrong byte offset)
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/combineindexbuffers_params.h"

using namespace metal;

struct Indices       { packed_int3 _data[1]; };
struct ResultIndices { packed_int3 _data[1]; };

kernel void combineindexbuffers(
    device Indices& Indices_1 [[buffer(CIB_Indices)]],
    constant CombineIndexBuffersParams& P [[buffer(CIB_Params)]],
    device ResultIndices& ResultIndices_1 [[buffer(CIB_ResultIndices)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x >= P.Count) {
        return;
    }
    uint targetIndex = gl_GlobalInvocationID.x + uint(P.StartIndex);
    int3 faceIndices = int3(Indices_1._data[gl_GlobalInvocationID.x]) + P.StartVertex;
    ResultIndices_1._data[targetIndex] = faceIndices;
}
