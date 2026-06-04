#include <metal_stdlib>
using namespace metal;

struct PbrVertex80
{
    packed_float3 position;
    packed_float3 normal;
    packed_float3 tangent;
    packed_float3 bitangent;
    packed_float2 texCoord;
    packed_float2 texCoord2;
    float selected;
    packed_float3 colorRGB;
};

struct VertexOut
{
    float4 position [[position]];
    float3 color;
};

vertex VertexOut my_world_mesh_vertex(
    uint vertexId [[vertex_id]],
    device const PbrVertex80* vertices [[buffer(0)]],
    device const packed_int3* faceIndices [[buffer(1)]])
{
    const uint faceId = vertexId / 3u;
    const uint corner = vertexId - faceId * 3u;
    const packed_int3 face = faceIndices[faceId];
    const int vertexIndex = face[corner];
    const PbrVertex80 pbrVertex = vertices[vertexIndex];

    VertexOut out;
    out.position = float4(float3(pbrVertex.position), 1.0);
    out.color = saturate(float3(pbrVertex.colorRGB) + float3(pbrVertex.selected * 0.08));
    return out;
}

fragment float4 my_world_mesh_fragment(VertexOut in [[stage_in]])
{
    return float4(in.color, 1.0);
}
