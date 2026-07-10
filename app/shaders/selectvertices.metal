// selectvertices.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-3). NOT wired to any stage/owner yet (see runtime/selectvertices_params.h header
// note, incl. MATRIX CONVENTION reuse and the dead UseVertexSelection field).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-SelectVertices.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// Body is the transpiler output VERBATIM — INCLUDING the fully-inlined Ashima simplex noise (glslang
// resolved `snoise()` completely; the raw output below is exactly what it emitted, not hand-simplified)
// and the `mod(x,y)` HLSL-extension call already lowered to floored-modulo arithmetic (params.h header
// note) — except:
//   (1) entry renamed main0 -> selectvertices; [[buffer(N)]] literals replaced with the named SELECTV
//       binding enum (SAME numeric values the transpiler actually emitted — §10.5③: SourceVertices=0/
//       ResultVertices=1/Params=2); GetDimensions (spvBufferSizeConstants[0]) replaced with the
//       host-ABI Count field appended to Params (§10.2③/§10.5①)
//   (2) ★PbrVertex packed_float3 struct fix (§10.5⑦): Position/Normal/Tangent/Bitangent declared
//       packed_float3 — matches the true 80-byte SwVertex stride, no spvPaddedArrayElement wrapper.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/selectvertices_params.h"

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

struct SourceVertices { PbrVertex _data[1]; };
struct ResultVertices { PbrVertex _data[1]; };

kernel void selectvertices(
    device SourceVertices& SourceVertices_1 [[buffer(SELECTV_SourceVertices)]],
    device ResultVertices& ResultVertices_1 [[buffer(SELECTV_ResultVertices)]],
    constant SelectVerticesParams& P [[buffer(SELECTV_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if (gl_GlobalInvocationID.x >= P.Count) {
        return;
    }
    ResultVertices_1._data[gl_GlobalInvocationID.x] = SourceVertices_1._data[gl_GlobalInvocationID.x];
    float4 _716 = float4(float3(SourceVertices_1._data[gl_GlobalInvocationID.x].Position), 1.0) * P.TransformVolume;
    float3 _717 = _716.xyz;
    float _1320;
    if (P.VolumeShape < 0.5)
    {
        _1320 = smoothstep(1.0 + P.FallOff, 1.0, length(_717));
    }
    else
    {
        float _1321;
        if (P.VolumeShape < 1.5)
        {
            float3 _735 = abs(_717);
            _1321 = smoothstep(1.0 + P.FallOff, 1.0, fast::max(fast::max(_735.x, _735.y), _735.z) + P.Phase);
        }
        else
        {
            float _1322;
            if (P.VolumeShape < 2.5)
            {
                _1322 = smoothstep(P.FallOff, 0.0, _716.y);
            }
            else
            {
                float _1323;
                if (P.VolumeShape < 3.5)
                {
                    _1323 = smoothstep((P.Threshold + 0.5) + P.FallOff, P.Threshold + 0.5, 1.0 - abs(((_716.y + P.Phase) - (2.0 * floor((_716.y + P.Phase) * 0.5))) - 1.0));
                }
                else
                {
                    float _1324;
                    if (P.VolumeShape < 4.5)
                    {
                        float3 _808 = (_717 * 0.910000026226043701171875) + float3(P.Phase);
                        float3 _950 = floor(_808 + float3(dot(_808, float3(0.3333333432674407958984375))));
                        float3 _957 = (_808 - _950) + float3(dot(_950, float3(0.16666667163372039794921875)));
                        float3 _961 = step(_957.yzx, _957);
                        float3 _964 = float3(1.0) - _961;
                        float3 _968 = fast::min(_961, _964.zxy);
                        float3 _972 = fast::max(_961, _964.zxy);
                        float3 _976 = (_957 - _968) + float3(0.16666667163372039794921875);
                        float3 _980 = (_957 - _972) + float3(0.3333333432674407958984375);
                        float3 _982 = _957 - float3(0.5);
                        float3 _1211 = _950 - (floor(_950 * 0.00346020772121846675872802734375) * 289.0);
                        float4 _993 = float4(_1211.z) + float4(0.0, _968.z, _972.z, 1.0);
                        float4 _1220 = ((_993 * 34.0) + float4(1.0)) * _993;
                        float4 _1004 = ((_1220 - (floor(_1220 * 0.00346020772121846675872802734375) * 289.0)) + float4(_1211.y)) + float4(0.0, _968.y, _972.y, 1.0);
                        float4 _1238 = ((_1004 * 34.0) + float4(1.0)) * _1004;
                        float4 _1015 = ((_1238 - (floor(_1238 * 0.00346020772121846675872802734375) * 289.0)) + float4(_1211.x)) + float4(0.0, _968.x, _972.x, 1.0);
                        float4 _1256 = ((_1015 * 34.0) + float4(1.0)) * _1015;
                        float4 _1265 = _1256 - (floor(_1256 * 0.00346020772121846675872802734375) * 289.0);
                        float4 _1030 = _1265 - (floor((_1265 * 0.14285714924335479736328125) * 0.14285714924335479736328125) * 49.0);
                        float4 _1035 = floor(_1030 * 0.14285714924335479736328125);
                        float4 _1047 = (_1035 * 0.2857142984867095947265625) + float4(-0.928571403026580810546875);
                        float4 _1054 = (floor(_1030 - (_1035 * 7.0)) * 0.2857142984867095947265625) + float4(-0.928571403026580810546875);
                        float4 _1061 = (float4(1.0) - abs(_1047)) - abs(_1054);
                        float4 _1070 = float4(_1047.xy, _1054.xy);
                        float4 _1079 = float4(_1047.zw, _1054.zw);
                        float4 _1092 = -step(_1061, float4(0.0));
                        float4 _1100 = _1070.xzyw + (((floor(_1070) * 2.0) + float4(1.0)).xzyw * _1092.xxyy);
                        float4 _1108 = _1079.xzyw + (((floor(_1079) * 2.0) + float4(1.0)).xzyw * _1092.zzww);
                        float3 _1115 = float3(_1100.xy, _1061.x);
                        float3 _1122 = float3(_1100.zw, _1061.y);
                        float3 _1129 = float3(_1108.xy, _1061.z);
                        float3 _1136 = float3(_1108.zw, _1061.w);
                        float4 _1271 = float4(1.792842864990234375) - (float4(dot(_1115, _1115), dot(_1122, _1122), dot(_1129, _1129), dot(_1136, _1136)) * 0.8537347316741943359375);
                        float4 _1182 = fast::max(float4(0.60000002384185791015625) - float4(dot(_957, _957), dot(_976, _976), dot(_980, _980), dot(_982, _982)), float4(0.0));
                        float4 _1185 = _1182 * _1182;
                        _1324 = smoothstep(P.Threshold + P.FallOff, P.Threshold, 42.0 * dot(_1185 * _1185, float4(dot(_1115 * _1271.x, _957), dot(_1122 * _1271.y, _976), dot(_1129 * _1271.z, _980), dot(_1136 * _1271.w, _982))));
                    }
                    else
                    {
                        _1324 = 1.0;
                    }
                    _1323 = _1324;
                }
                _1322 = _1323;
            }
            _1321 = _1322;
        }
        _1320 = _1321;
    }
    float _1325;
    if (P.SelectMode < 0.5)
    {
        _1325 = _1320 * P.Strength;
    }
    else
    {
        float _1326;
        if (P.SelectMode < 1.5)
        {
            _1326 = _1320 + (SourceVertices_1._data[gl_GlobalInvocationID.x].Selected * P.Strength);
        }
        else
        {
            float _1327;
            if (P.SelectMode < 2.5)
            {
                _1327 = SourceVertices_1._data[gl_GlobalInvocationID.x].Selected - (_1320 * P.Strength);
            }
            else
            {
                float _1328;
                if (P.SelectMode < 3.5)
                {
                    _1328 = mix(SourceVertices_1._data[gl_GlobalInvocationID.x].Selected, SourceVertices_1._data[gl_GlobalInvocationID.x].Selected * _1320, P.Strength);
                }
                else
                {
                    float _1329;
                    if (P.SelectMode < 4.5)
                    {
                        _1329 = _1320 * (1.0 - SourceVertices_1._data[gl_GlobalInvocationID.x].Selected);
                    }
                    else
                    {
                        _1329 = _1320;
                    }
                    _1328 = _1329;
                }
                _1327 = _1328;
            }
            _1326 = _1327;
        }
        _1325 = _1326;
    }
    ResultVertices_1._data[gl_GlobalInvocationID.x].Selected = (P.ClampResult < 0.5) ? _1325 : fast::clamp(_1325, 0.0, 1.0);
}
