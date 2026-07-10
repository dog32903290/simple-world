// reversefacevertexindexorder.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md
// §10, 2026-07-10 wave-2). NOT wired to any stage/owner yet (see runtime/reversefacevertexindexorder_
// params.h header note).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-ReverseFaceVertexIndexOrder.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM except:
//   (1) entry renamed main0 -> reversefacevertexindexorder
//   (2) [[buffer(N)]] literal indices replaced with the named RFVIO binding enum
//   (3) the empty HLSL cbuffer + GetDimensions/spvBufferSizeConstants bound-check replaced with a
//       host-ABI Count param (§10.2③/§10.5①, ReverseFaceVertexIndexOrderParams — see params.h)
//   (4) ★NEW TRANSPILER-GAP FIX (wave-2 finding, not yet in §10.5): `StructuredBuffer<int3>` /
//       `RWStructuredBuffer<int3>` compile to a bare `int3 _data[1]` array member in spirv-cross's raw
//       MSL output. Metal's array-of-3-component-vector ABI rounds EACH ELEMENT to 16-byte stride
//       (Metal Shading Language Spec, extended-vector alignment) — but HLSL's StructuredBuffer<int3>
//       (and TiXL's real host-side Int3, sw_mesh.h SwTriIndex) is TIGHTLY packed at 12 bytes/element.
//       Left as bare `int3`, the kernel reads/writes at the WRONG stride for every element past index
//       0 — silent corruption, no crash. EMPIRICALLY PROVEN 2026-07-10 (see mathv worktree scratch
//       stride-test: bare int3 mismatched at i=1..3, packed_int3 matched at all i, host buffer written
//       at the true 12-byte SwTriIndex stride both times). Fix: `int3 _data[1]` -> `packed_int3
//       _data[1]` in both buffer structs; reads cast `int3(...)` before swizzling (packed_int3 doesn't
//       support `.zyx` directly), writes rely on the implicit int3->packed_int3 conversion (proven).
//       This is the SAME class of trap as PbrVertex's float3 fields (see flipnormals.metal) — a bare
//       spirv-cross vector-array element needs a manual packed_ override whenever the source HLSL type
//       is a StructuredBuffer<vecN> the transpiler didn't recognize as needing tight packing.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/reversefacevertexindexorder_params.h"

using namespace metal;

struct SourceIndices { packed_int3 _data[1]; };
struct ResultIndices  { packed_int3 _data[1]; };

kernel void reversefacevertexindexorder(
    device SourceIndices& SourceIndices_1 [[buffer(RFVIO_SourceIndices)]],
    device ResultIndices& ResultIndices_1 [[buffer(RFVIO_ResultIndices)]],
    constant ReverseFaceVertexIndexOrderParams& P [[buffer(RFVIO_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x >= P.Count) {
        return;
    }
    ResultIndices_1._data[gl_GlobalInvocationID.x] = int3(SourceIndices_1._data[gl_GlobalInvocationID.x]).zyx;
}
