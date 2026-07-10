// gridwalkpoints.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-4). NOT wired to any stage/owner yet (see runtime/gridwalkpoints_params.h header
// note — owner UNRESOLVED, an orphaned compute shader with no owning .t3 found in the Lib tree).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/sim/grid-walk-points.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM (including the fully-inlined qRotateVec3 cross/dot
// expansion, the `mod()` float3 helper spirv-cross injected — floored, matches GLSL.std.450 Mod —
// and the DEAD-STORE elimination of the source's `axis`/`r`/`newLocalPosition=clamp(...)` locals,
// which never appear because they are never read downstream, see mathv_ref_gridwalkpoints.h header)
// except:
//   (1) entry renamed main0 -> gridwalkpoints; [[buffer(N)]] literals replaced with the named
//       GRIDWALKPOINTS binding enum (SAME numeric values the transpiler actually emitted — §10.5③:
//       ResultPoints=0/Params=1); GetDimensions (spvBufferSizeConstants[0]/64) replaced with the
//       host-ABI P.Count field (§10.2③/§10.5①) — the `spvBufferSizeConstants [[buffer(25)]]`
//       parameter is dropped entirely
//   (2) ★SwPoint struct reuse (§10.5⑦ playbook, no NEW packing fix needed — spirv-cross already
//       emitted packed_float3 for the LegacyPoint struct's Position/Stretch): the locally-declared
//       raw `LegacyPoint` struct is replaced with `tixl_point.h`'s shared `SwPoint` type; field
//       accesses renamed to the SwPoint equivalents at the SAME byte offset (LegacyPoint.W ->
//       SwPoint.FX1 @12; this kernel never touches .Stretch/.Selected, so no rename needed there).
//       Params' packed_float3 GridSize/GridOffset flattened to scalar X/Y/Z host-ABI fields (matches
//       wrappointposition_params.h's CenterX/Y/Z precedent).
#include <metal_stdlib>
#include <simd/simd.h>
#include "tixl_point.h"
#include "../src/runtime/gridwalkpoints_params.h"

using namespace metal;

// Implementation of the GLSL mod() function (spirv-cross-injected helper, kept verbatim — matches
// GLSL.std.450 Mod, FLOORED, see mathv_ref_gridwalkpoints.h header AMBIGUITY note).
template<typename Tx, typename Ty>
inline Tx gwp_mod(Tx x, Ty y)
{
    return x - y * floor(x / y);
}

// Implementation of signed integer mod accurate to SPIR-V specification (spirv-cross-injected
// helper, kept verbatim — used on `int(fract(...)*6.0) % 6`, always non-negative by construction
// since fract() is always in [0,1), so floored/truncated agree trivially here; kept as the raw
// spvSMod form anyway for byte-fidelity to the transpiler output, not a manual `%` substitution).
template<typename Tx, typename Ty>
inline Tx gwp_spvSMod(Tx x, Ty y)
{
    Tx remainder = x - y * (x / y);
    return select(Tx(remainder + y), remainder, remainder == 0 || (x >= 0) == (y >= 0));
}

struct ResultPoints { SwPoint _data[1]; };

// axisAngles[] — grid-walk-points.hlsl :20-28, verbatim static table.
constant float4 gwp_axisAngles[6] = { float4(0.0, 1.0, 0.0, 0.0), float4(0.0, 1.0, 0.0, 1.0), float4(0.0, 1.0, 0.0, 2.0), float4(0.0, 1.0, 0.0, 3.0), float4(1.0, 0.0, 0.0, -1.0), float4(1.0, 0.0, 0.0, 1.0) };

kernel void gridwalkpoints(
    device ResultPoints&           ResultPoints_1 [[buffer(GRIDWALKPOINTS_ResultPoints)]],
    constant GridWalkPointsParams& P              [[buffer(GRIDWALKPOINTS_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    do
    {
        if (gl_GlobalInvocationID.x >= uint(P.Count))
        {
            ResultPoints_1._data[gl_GlobalInvocationID.x].FX1 = 0.0;   // LegacyPoint.W
            break;
        }
        float4 _586 = ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation;
        float4 _379 = fast::normalize(_586);
        float3 _509 = cross(_379.xyz, float3(0.0, 0.0, -1.0)) * 2.0;
        float3 _384 = ((float3(0.0, 0.0, -1.0) + (_509 * _379.w)) + cross(_379.xyz, _509)) * P.Speed;
        float3 gridOffset = float3(P.GridOffsetX, P.GridOffsetY, P.GridOffsetZ);
        float3 gridSize = float3(P.GridSizeX, P.GridSizeY, P.GridSizeZ);
        float3 _406 = (gwp_mod(float3(ResultPoints_1._data[gl_GlobalInvocationID.x].Position) - gridOffset, gridSize)) + _384;
        float _525 = fract(gwp_mod(((((P.Seed + float(gl_GlobalInvocationID.x)) + ResultPoints_1._data[gl_GlobalInvocationID.x].Position[0]) + ResultPoints_1._data[gl_GlobalInvocationID.x].Position[1]) + ResultPoints_1._data[gl_GlobalInvocationID.x].Position[2]) * 421.0, 1231.0) * 0.103100001811981201171875);
        float _529 = _525 * (_525 + 33.3300018310546875);
        if (((((((_406.x <= 0.0) || (_406.x >= gridSize[0u])) || (_406.y <= 0.0)) || (_406.y >= gridSize[1u])) || (_406.z <= 0.0)) || (_406.z >= gridSize[2u])) || (P.TriggerTurn > 0.5))
        {
            int _480 = gwp_spvSMod(int(fract(_529 * (_529 + _529)) * 6.0), 6);
            ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation = float4(gwp_axisAngles[_480].xyz * sin(gwp_axisAngles[_480].w * 1.0471975803375244140625), cos(gwp_axisAngles[_480].w * 1.0471975803375244140625));
        }
        ResultPoints_1._data[gl_GlobalInvocationID.x].Position = float3(ResultPoints_1._data[gl_GlobalInvocationID.x].Position) + _384;
        break;
    } while(false);
}
