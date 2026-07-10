// repeatindicesatpoints.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-2). NOT wired to any stage/owner yet (see runtime/repeatindicesatpoints_params.h).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-RepeatIndicesAtPoints.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM except:
//   (1) entry renamed main0 -> repeatindicesatpoints; [[buffer(N)]] replaced with the RIATP enum (SAME
//       numeric values the transpiler actually emitted — §10.5③)
//   (2) GetDimensions/spvBufferSizeConstants replaced with a host-ABI Count param (§10.2③/§10.5①)
//   (3) ★packed_int3 stride fix (SAME transpiler gap as reversefacevertexindexorder.metal /
//       combineindexbuffers.metal's header notes — StructuredBuffer<int3> needs packed_int3, not
//       spirv-cross's bare int3)
// 2D dispatch: gid.x = source face index (bound-checked against Count), gid.y = point/instance index
// (NOT bound-checked in the HLSL — the caller's dispatch grid height IS the point count, matching the
// original `[numthreads(16,16,1)]` shape exactly).
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/repeatindicesatpoints_params.h"

using namespace metal;

struct SourceFaces { packed_int3 _data[1]; };
struct ResultFaces  { packed_int3 _data[1]; };

kernel void repeatindicesatpoints(
    device SourceFaces& SourceFaces_1 [[buffer(RIATP_SourceFaces)]],
    device ResultFaces& ResultFaces_1 [[buffer(RIATP_ResultFaces)]],
    constant RepeatIndicesAtPointsParams& P [[buffer(RIATP_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x >= P.Count) {
        return;
    }
    int targetFaceIndex = int((gl_GlobalInvocationID.y * P.Count) + gl_GlobalInvocationID.x);
    ResultFaces_1._data[targetFaceIndex] = int3(uint3(int3(SourceFaces_1._data[gl_GlobalInvocationID.x])) + uint3(uint(P.VertexCount) * gl_GlobalInvocationID.y));
}
