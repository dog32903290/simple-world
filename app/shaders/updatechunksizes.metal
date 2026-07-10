// updatechunksizes.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-3). NOT wired to any stage/owner yet (see runtime/updatechunksizes_params.h header
// note — this is ONE kernel of the much larger DrawMeshChunksAtPoints.t3 compound).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e UpdateChunkSizes -S comp
//     -V --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/chunks/MeshChunks-UpdateChunkSizes.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM (including the `spvSMod` helper spirv-cross injected for
// HLSL's int `%` operator — kept unmodified, see params.h header for the floored-vs-truncated note)
// except:
//   (1) entry renamed UpdateChunkSizes -> updatechunksizes (sw lowercase-kernel-name convention;
//       §10.5② entry name ITSELF was already `UpdateChunkSizes`, not `main` — confirmed via
//       `grep numthreads -A2` before transpiling, -e flag set accordingly)
//   (2) [[buffer(N)]] literal indices replaced with the named UCS binding enum (SAME numeric values
//       the transpiler actually emitted — §10.5③: Params=0/ChunkIndicesForPoints=1/ChunkSizes=2/
//       ChunkDefs=3, NOT declaration order, read off the raw output)
//   (3) Params struct type -> UpdateChunkSizesParams (all three HLSL cbuffer fields present verbatim,
//       no GetDimensions/host-ABI substitution needed — §10.5① N/A for this op)
// No struct-packing fix needed (§10.5⑥⑦ N/A): every buffer element here is a scalar int/uint or an
// all-int struct (ChunkDef), never a vec3 — the packed_ trap only applies to vector-typed members.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/updatechunksizes_params.h"

using namespace metal;

// Implementation of signed integer mod accurate to SPIR-V specification (spirv-cross-injected helper,
// kept verbatim — see params.h header for why this never forks vs HLSL's truncated `%` in THIS
// kernel's realistic non-negative-operand domain).
template<typename Tx, typename Ty>
inline Tx spvSMod(Tx x, Ty y)
{
    Tx remainder = x - y * (x / y);
    return select(Tx(remainder + y), remainder, remainder == 0 || (x >= 0) == (y >= 0));
}

struct ChunkIndicesForPoints
{
    int _data[1];
};

struct ChunkSizes
{
    uint _data[1];
};

struct ChunkDef
{
    int StartFaceIndex;
    int FaceCount;
    int StartVertexIndex;
    int VertexCount;
};

struct ChunkDefs
{
    ChunkDef _data[1];
};

kernel void updatechunksizes(constant UpdateChunkSizesParams& P [[buffer(UCS_Params)]],
                              device ChunkIndicesForPoints& ChunkIndicesForPoints_1 [[buffer(UCS_ChunkIndicesForPoints)]],
                              device ChunkSizes& ChunkSizes_1 [[buffer(UCS_ChunkSizes)]],
                              device ChunkDefs& ChunkDefs_1 [[buffer(UCS_ChunkDefs)]],
                              uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    do
    {
        int _91 = int(gl_GlobalInvocationID.x);
        if (_91 >= P.Count)
        {
            break;
        }
        ChunkSizes_1._data[_91] = uint(ChunkDefs_1._data[spvSMod(ChunkIndicesForPoints_1._data[spvSMod(_91, P.ChunkIndexForPointsCounts)], P.ChunkDefCount)].FaceCount);
        break;
    } while(false);
}
