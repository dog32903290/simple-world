#include <metal_stdlib>
using namespace metal;

struct B5Params
{
    packed_float3 SphereSDF_nG1CBDm_Center;
    float SphereSDF_nG1CBDm_Radius;
};

kernel void my_world_b5_shadergraph_params_packing_probe(
    constant B5Params& b5 [[buffer(7)]],
    device uint* out [[buffer(0)]])
{
    out[0] = uint(sizeof(B5Params));
    out[1] = as_type<uint>(b5.SphereSDF_nG1CBDm_Center.x);
    out[2] = as_type<uint>(b5.SphereSDF_nG1CBDm_Center.y);
    out[3] = as_type<uint>(b5.SphereSDF_nG1CBDm_Center.z);
    out[4] = as_type<uint>(b5.SphereSDF_nG1CBDm_Radius);
}
