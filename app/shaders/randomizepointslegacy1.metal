// randomizepointslegacy1.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md
// §10, 2026-07-10 wave-4). NOT wired to any stage/owner yet (see
// runtime/randomizepointslegacy1_params.h header note — a legacy op, distinct from sw's existing
// `randomizepoints` kernel).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/points/modify/RandomizePoints_Legacy1.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM (including the fully-inlined hash41u rounds, GetSchlickBias
// ternary-as-select lowering, and the two qMul+normalize-as-cross/dot expansions the compiler
// produced instead of naming a qMul function) except:
//   (1) entry renamed main0 -> randomizepointslegacy1; [[buffer(N)]] literals replaced with the
//       named RANDOMIZEPOINTSLEGACY1 binding enum (SAME numeric values the transpiler actually
//       emitted — §10.5③: SourcePoints=0/Params=1/ResultPoints=2); GetDimensions
//       (spvBufferSizeConstants[0]/64) replaced with the host-ABI P.Count field (§10.2③/§10.5①)
//   (2) ★SwPoint struct reuse (§10.5⑦ playbook, no NEW packing fix needed here — spirv-cross already
//       emitted packed_float3 for both Position and Stretch, since they are non-consecutive fields
//       in LegacyPoint): the locally-declared raw `LegacyPoint` struct is replaced with
//       `tixl_point.h`'s shared `SwPoint` type; every LegacyPoint-only field name is renamed to its
//       SwPoint equivalent at the SAME byte offset (LegacyPoint.W->SwPoint.FX1 @12,
//       LegacyPoint.Stretch->SwPoint.Scale @48, LegacyPoint.Selected->SwPoint.FX2 @60 — see
//       mathv_ref_randomizepointslegacy1.h header / tixl_point.h for the layout proof). Params'
//       packed_float3 RandomizePosition/RandomizeRotation are likewise flattened to scalar X/Y/Z
//       fields (host-ABI convention, matching wrappointposition_params.h's CenterX/Y/Z precedent).
//   (3) the transpiler's `constant uint _1124 = {};` zero-filler (standing in for a dead-code-
//       eliminated 4th hash lane that is computed then NEVER READ, since only hashRot.xyz is used
//       downstream — see the .hlsl :52-55 `hashRot.xyz` extraction) is kept as a local zero constant.
#include <metal_stdlib>
#include <simd/simd.h>
#include "tixl_point.h"
#include "../src/runtime/randomizepointslegacy1_params.h"

using namespace metal;

struct SourcePoints { SwPoint _data[1]; };
struct ResultPoints { SwPoint _data[1]; };

// dead-code-eliminated 4th hash lane, never read (see header note (3)) -- file-scope constant,
// matching the raw transpiler output's own placement (NOT a local inside the kernel body: MSL
// `constant` address space is file/global scope, same as the raw output emitted it).
constant uint _1124 = {};

kernel void randomizepointslegacy1(
    device SourcePoints&                    SourcePoints_1 [[buffer(RANDOMIZEPOINTSLEGACY1_SourcePoints)]],
    constant RandomizePointsLegacy1Params&  P              [[buffer(RANDOMIZEPOINTSLEGACY1_Params)]],
    device ResultPoints&                    ResultPoints_1 [[buffer(RANDOMIZEPOINTSLEGACY1_ResultPoints)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    float _1107 = SourcePoints_1._data[gl_GlobalInvocationID.x].FX1;      // LegacyPoint.W
    float4 _1111 = SourcePoints_1._data[gl_GlobalInvocationID.x].Color;
    float3 _1114 = float3(SourcePoints_1._data[gl_GlobalInvocationID.x].Scale);   // LegacyPoint.Stretch
    float _1117 = SourcePoints_1._data[gl_GlobalInvocationID.x].FX2;      // LegacyPoint.Selected
    int _534 = int(gl_GlobalInvocationID.x);
    float _545 = (P.Seed + (133.1123046875 * (float(_534) / float(P.Count)))) + 999.0;
    int _547 = int(_545);
    uint _707 = uint((_534 * 12341) + _547) * 13331u;
    uint _712 = ((_707 >> 8u) ^ _707) * 1103515245u;
    uint _717 = ((_712 >> 8u) ^ _712) * 1103515245u;
    uint _722 = ((_717 >> 8u) ^ _712) * 1103515245u;
    uint _743 = uint(((_534 * 12341) + _547) + 1) * 13331u;
    uint _748 = ((_743 >> 8u) ^ _743) * 1103515245u;
    uint _753 = ((_748 >> 8u) ^ _748) * 1103515245u;
    uint _758 = ((_753 >> 8u) ^ _748) * 1103515245u;
    float4 _567 = mix(float4(uint4(_712, _717, _722, ((_722 >> 8u) ^ _717) * 1103515245u)) * 2.3283064365386962890625e-10, float4(uint4(_748, _753, _758, ((_758 >> 8u) ^ _753) * 1103515245u)) * 2.3283064365386962890625e-10, float4(smoothstep(0.0, 1.0, _545 - float(_547))));
    uint _779 = uint((_534 * 2723) + _547) * 13331u;
    uint _784 = ((_779 >> 8u) ^ _779) * 1103515245u;
    uint _789 = ((_784 >> 8u) ^ _784) * 1103515245u;
    uint _815 = uint(((_534 * 2723) + _547) + 1) * 13331u;
    uint _820 = ((_815 >> 8u) ^ _815) * 1103515245u;
    uint _825 = ((_820 >> 8u) ^ _820) * 1103515245u;
    float4 _853 = _567 * 2.0;
    float4 _862 = _567 * 2.0;
    float4 _597 = (select((((_853 - float4(1.0)) / (((float4(2.0) - _853) * ((1.0 / (1.0 - P.Bias)) - 2.0)) + float4(1.0))) * float4(0.5)) + float4(0.5), (_862 / (((float4(1.0) - _862) * ((1.0 / P.Bias) - 2.0)) + float4(1.0))) * float4(0.5), _567 < float4(0.5)) * 2.0) - float4(1.0);
    float _608 = P.Amount * ((P.UseWAsSelection > 0.5) ? _1107 : 1.0);
    float3 _615 = (_597.xyz * float3(P.RandomizePositionX, P.RandomizePositionY, P.RandomizePositionZ)) * _608;
    float3 _1125;
    if (P.UseLocalSpace < 0.5)
    {
        float3 _903 = cross(SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz, _615) * 2.0;
        _1125 = (_615 + (_903 * SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.w)) + cross(SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz, _903);
    }
    else
    {
        _1125 = _615;
    }
    float3 _643 = (((((mix(float4(uint4(_784, _789, ((_789 >> 8u) ^ _784) * 1103515245u, _1124)) * 2.3283064365386962890625e-10, float4(uint4(_820, _825, ((_825 >> 8u) ^ _820) * 1103515245u, _1124)) * 2.3283064365386962890625e-10, float4(smoothstep(0.0, 1.0, _545 - float(_547)))) * 2.0) - float4(1.0)).xyz - float3(0.5)) * ((float3(P.RandomizeRotationX, P.RandomizeRotationY, P.RandomizeRotationZ) * float3(0.0055555556900799274444580078125)) * 3.1415927410125732421875)) * _608) * _597.xyz;
    float _648 = _643.x * P.Offset;
    float _924 = cos(_648 * 0.5);
    float4 _932 = float4(float3(1.0, 0.0, 0.0) * sin(_648 * 0.5), _924);
    float4 _652 = fast::normalize(float4(((_932.xyz * SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.w) + (SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz * _924)) + cross(SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz, _932.xyz), (SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.w * _924) - dot(SourcePoints_1._data[gl_GlobalInvocationID.x].Rotation.xyz, _932.xyz)));
    float _657 = _643.y * P.Offset;
    float _976 = cos(_657 * 0.5);
    float4 _984 = float4(float3(0.0, 1.0, 0.0) * sin(_657 * 0.5), _976);
    float4 _661 = fast::normalize(float4(((_984.xyz * _652.w) + (_652.xyz * _976)) + cross(_652.xyz, _984.xyz), (_652.w * _976) - dot(_652.xyz, _984.xyz)));
    float _666 = _643.z * P.Offset;
    float _1028 = cos(_666 * 0.5);
    float4 _1036 = float4(float3(0.0, 0.0, 1.0) * sin(_666 * 0.5), _1028);
    ResultPoints_1._data[gl_GlobalInvocationID.x].Position = float3(SourcePoints_1._data[gl_GlobalInvocationID.x].Position) + _1125;
    ResultPoints_1._data[gl_GlobalInvocationID.x].FX1 = _1107 + ((_597.w * P.RandomizeW) * _608);   // LegacyPoint.W
    ResultPoints_1._data[gl_GlobalInvocationID.x].Rotation = fast::normalize(float4(((_1036.xyz * _661.w) + (_661.xyz * _1028)) + cross(_661.xyz, _1036.xyz), (_661.w * _1028) - dot(_661.xyz, _1036.xyz)));
    ResultPoints_1._data[gl_GlobalInvocationID.x].Color = _1111;
    ResultPoints_1._data[gl_GlobalInvocationID.x].Scale = _1114;    // LegacyPoint.Stretch
    ResultPoints_1._data[gl_GlobalInvocationID.x].FX2 = _1117;      // LegacyPoint.Selected
}
