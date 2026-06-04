#include <metal_stdlib>
using namespace metal;

struct Transforms
{
    float4x4 CameraToClipSpace;
    float4x4 ClipSpaceToCamera;
    float4x4 WorldToCamera;
    float4x4 CameraToWorld;
    float4x4 WorldToClipSpace;
    float4x4 ClipSpaceToWorld;
    float4x4 ObjectToWorld;
    float4x4 WorldToObject;
    float4x4 ObjectToCamera;
    float4x4 ObjectToClipSpace;
};

struct ParamsB1
{
    float4 Color;
    float AlphaCutOff;
    float UseFlatShading;
    float SpecularAA;
};

struct FogParams
{
    float4 FogColor;
    float FogDistance;
    float FogBias;
};

struct PbrParams
{
    float4 BaseColor;
    float4 EmissiveColor;
    float Roughness;
    float Specular;
    float Metal;
};

kernel void my_world_constant_buffer_packing_probe(
    constant Transforms& b0 [[buffer(2)]],
    constant ParamsB1& b1 [[buffer(3)]],
    constant FogParams& b2 [[buffer(4)]],
    constant PbrParams& b4 [[buffer(6)]],
    device uint* out [[buffer(0)]])
{
    out[0] = uint(sizeof(Transforms));
    out[1] = uint(sizeof(ParamsB1));
    out[2] = uint(sizeof(FogParams));
    out[3] = uint(sizeof(PbrParams));

    out[10] = as_type<uint>(b0.CameraToClipSpace[0][0]);
    out[11] = as_type<uint>(b0.CameraToClipSpace[3][3]);
    out[12] = as_type<uint>(b0.ClipSpaceToCamera[0][0]);
    out[13] = as_type<uint>(b0.ClipSpaceToCamera[3][3]);
    out[14] = as_type<uint>(b0.WorldToCamera[0][0]);
    out[15] = as_type<uint>(b0.WorldToCamera[3][3]);
    out[16] = as_type<uint>(b0.CameraToWorld[0][0]);
    out[17] = as_type<uint>(b0.CameraToWorld[3][3]);
    out[18] = as_type<uint>(b0.WorldToClipSpace[0][0]);
    out[19] = as_type<uint>(b0.WorldToClipSpace[3][3]);
    out[20] = as_type<uint>(b0.ClipSpaceToWorld[0][0]);
    out[21] = as_type<uint>(b0.ClipSpaceToWorld[3][3]);
    out[22] = as_type<uint>(b0.ObjectToWorld[0][0]);
    out[23] = as_type<uint>(b0.ObjectToWorld[3][3]);
    out[24] = as_type<uint>(b0.WorldToObject[0][0]);
    out[25] = as_type<uint>(b0.WorldToObject[3][3]);
    out[26] = as_type<uint>(b0.ObjectToCamera[0][0]);
    out[27] = as_type<uint>(b0.ObjectToCamera[3][3]);
    out[28] = as_type<uint>(b0.ObjectToClipSpace[0][0]);
    out[29] = as_type<uint>(b0.ObjectToClipSpace[3][3]);

    out[30] = as_type<uint>(b1.Color.x);
    out[31] = as_type<uint>(b1.Color.y);
    out[32] = as_type<uint>(b1.Color.z);
    out[33] = as_type<uint>(b1.Color.w);
    out[34] = as_type<uint>(b1.AlphaCutOff);
    out[35] = as_type<uint>(b1.UseFlatShading);
    out[36] = as_type<uint>(b1.SpecularAA);

    out[40] = as_type<uint>(b2.FogColor.x);
    out[41] = as_type<uint>(b2.FogColor.y);
    out[42] = as_type<uint>(b2.FogColor.z);
    out[43] = as_type<uint>(b2.FogColor.w);
    out[44] = as_type<uint>(b2.FogDistance);
    out[45] = as_type<uint>(b2.FogBias);

    out[50] = as_type<uint>(b4.BaseColor.x);
    out[51] = as_type<uint>(b4.BaseColor.y);
    out[52] = as_type<uint>(b4.BaseColor.z);
    out[53] = as_type<uint>(b4.BaseColor.w);
    out[54] = as_type<uint>(b4.EmissiveColor.x);
    out[55] = as_type<uint>(b4.EmissiveColor.y);
    out[56] = as_type<uint>(b4.EmissiveColor.z);
    out[57] = as_type<uint>(b4.EmissiveColor.w);
    out[58] = as_type<uint>(b4.Roughness);
    out[59] = as_type<uint>(b4.Specular);
    out[60] = as_type<uint>(b4.Metal);
}
