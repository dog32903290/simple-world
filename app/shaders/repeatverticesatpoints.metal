// repeatverticesatpoints.metal — mathv transpiler-batch kernel INVENTORY (MATH_VERIFY_WORKFLOW.md §10,
// 2026-07-10 wave-2). NOT wired to any stage/owner yet (see runtime/repeatverticesatpoints_params.h).
//
// PROVENANCE (transpiler-first, §10.1 recipe):
//   glslang(16.3.0) -D --hlsl-iomap --amb --sbb 0 --sib 8 --sub 16 --stb 24 -e main -S comp -V
//     --target-env vulkan1.0 -I external/tixl/Operators/Lib/Assets/shaders
//     external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-RepeatVerticesAtPoints.hlsl
//   spirv-cross(1.4.350.1) --msl --msl-version 20000
// The body below is the transpiler's ARITHMETIC verbatim (same numbered temporaries: _874/_876/.../
// _843/_758 etc — spirv-cross already INLINED HLSL's `mul(v, transpose(qToMatrix(q)))` (Position) and
// the cross-product quaternion-rotate formula (Normal/Tangent/Bitangent, the same identity
// mathv_ref_shared_quat.h::qRotateVec3 implements) — no manual math was written, only structural
// adapter fixes (§10.2③), same two classes as the other wave-2 kernels' header notes:
//   (1) entry renamed main0 -> repeatverticesatpoints; [[buffer(N)]] replaced with the RVATP enum
//       (FRESH bindings — the two HLSL cbuffers are flattened into one Params struct, §10.2③, so no
//       single raw-output binding number applies unchanged); GetDimensions/spvBufferSizeConstants
//       replaced with two host-ABI Count fields (§10.5①: PointsCount, VerticesCount)
//   (2) PbrVertex packed_float3 fix (SAME transpiler gap as flipnormals.metal/combinevertexbuffers.
//       metal — spirv-cross again left Position/Normal/Tangent/Bitangent as plain float3, wrapped in
//       spvPaddedArrayElement<PbrVertex,112>; fixed + unwrapped here, `.data` indirection removed)
//   NOTE: `Point` (SwPoint's HLSL twin) came out CORRECTLY packed in the raw output this time
//       (packed_float3 Position/Scale) — spirv-cross's packing heuristic gets it right when a vec3 is
//       immediately followed by a differently-sized field (float FX1 right after Position); it only
//       breaks on RUNS of consecutive same-alignment vec3 fields (PbrVertex's Position/Normal/Tangent/
//       Bitangent). No fix needed for Point here.
#include <metal_stdlib>
#include <simd/simd.h>
#include "../src/runtime/repeatverticesatpoints_params.h"

using namespace metal;

struct Point {
    packed_float3 Position;
    float FX1;
    float4 Rotation;
    float4 Color;
    packed_float3 Scale;
    float FX2;
};
struct Points { Point _data[1]; };

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

kernel void repeatverticesatpoints(
    device Points& Points_1 [[buffer(RVATP_Points)]],
    device SourceVertices& SourceVertices_1 [[buffer(RVATP_SourceVertices)]],
    device ResultVertices& ResultVertices_1 [[buffer(RVATP_ResultVertices)]],
    constant RepeatVerticesAtPointsParams& P [[buffer(RVATP_Params)]],
    uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    if ((gl_GlobalInvocationID.y >= P.PointsCount) || (gl_GlobalInvocationID.x >= P.VerticesCount)) {
        return;
    }
    int _480 = int((gl_GlobalInvocationID.y * P.VerticesCount) + gl_GlobalInvocationID.x);
    float3 _874 = SourceVertices_1._data[gl_GlobalInvocationID.x].Normal;
    float3 _876 = SourceVertices_1._data[gl_GlobalInvocationID.x].Tangent;
    float3 _878 = SourceVertices_1._data[gl_GlobalInvocationID.x].Bitangent;
    float2 _881 = SourceVertices_1._data[gl_GlobalInvocationID.x].TexCoord;
    float2 _884 = SourceVertices_1._data[gl_GlobalInvocationID.x].TexCoord2;
    float _887 = SourceVertices_1._data[gl_GlobalInvocationID.x].Selected;
    float3 _890 = float3(SourceVertices_1._data[gl_GlobalInvocationID.x].ColorRGB);
    float4 _897 = Points_1._data[gl_GlobalInvocationID.y].Rotation;
    float4 _899 = Points_1._data[gl_GlobalInvocationID.y].Color;
    _884.y = _884.y * ((P.TexCoord2Factor == 0) ? 1.0 : ((P.TexCoord2Factor == 1) ? Points_1._data[gl_GlobalInvocationID.y].FX1 : Points_1._data[gl_GlobalInvocationID.y].FX2));
    float _676 = _897.x + _897.x;
    float _679 = _897.y + _897.y;
    float _682 = _897.z + _897.z;
    float _685 = _897.x * _676;
    float _688 = _897.x * _679;
    float _691 = _897.x * _682;
    float _694 = _897.y * _679;
    float _697 = _897.y * _682;
    float _700 = _897.z * _682;
    float _703 = _897.w * _676;
    float _706 = _897.w * _679;
    float _709 = _897.w * _682;
    float4x4 _843 = float4x4(float4(0.0), float4(0.0), float4(0.0), float4(0.0));
    _843[0].x = 1.0 - (_694 + _700);
    _843[0].y = _688 - _709;
    _843[0].z = _691 + _706;
    _843[1].x = _688 + _709;
    _843[1].y = 1.0 - (_685 + _700);
    _843[1].z = _697 - _703;
    _843[2].x = _691 - _706;
    _843[2].y = _697 + _703;
    _843[2].z = 1.0 - (_685 + _694);
    _843[3].w = 1.0;
    float3 _758 = cross(_897.xyz, _874) * 2.0;
    float3 _777 = cross(_897.xyz, _876) * 2.0;
    float3 _796 = cross(_897.xyz, _878) * 2.0;
    ResultVertices_1._data[_480].Position = float3(((transpose(_843) * float4(float4(SourceVertices_1._data[gl_GlobalInvocationID.x].Position, 1.0).xyz * (((fast::max(float3(0.0), select(float3(1.0), float3(Points_1._data[gl_GlobalInvocationID.y].Scale), bool3(P.ApplyScale != 0.0))) * float3(P.Stretch)) * P.Size) * ((P.ScaleFX == 0) ? 1.0 : ((P.ScaleFX == 1) ? Points_1._data[gl_GlobalInvocationID.y].FX1 : Points_1._data[gl_GlobalInvocationID.y].FX2))), 1.0)) + float4(Points_1._data[gl_GlobalInvocationID.y].Position[0], Points_1._data[gl_GlobalInvocationID.y].Position[1], Points_1._data[gl_GlobalInvocationID.y].Position[2], 0.0)).xyz);
    ResultVertices_1._data[_480].Normal = (_874 + (_758 * _897.w)) + cross(_897.xyz, _758);
    ResultVertices_1._data[_480].Tangent = (_876 + (_777 * _897.w)) + cross(_897.xyz, _777);
    ResultVertices_1._data[_480].Bitangent = (_878 + (_796 * _897.w)) + cross(_897.xyz, _796);
    ResultVertices_1._data[_480].TexCoord = _881;
    ResultVertices_1._data[_480].TexCoord2 = _884;
    ResultVertices_1._data[_480].Selected = _887;
    ResultVertices_1._data[_480].ColorRGB = _890 * _899.xyz;
}
