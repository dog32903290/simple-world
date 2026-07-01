// computeshaderstage_displacemeshnoise — the PROVING kernel for the ComputeShaderStage seam on the
// MESH currency with an INTERLEAVED mixed-slot FloatsToBuffer.Params cb (骨9: proves 骨7b's MultiInput
// declaration-order fix holds on real SwVertex currency). Consumes/produces the 80-byte SwVertex.
//
// It is a faithful MSL port of external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-LegacyNoiseDisplace.hlsl
// (the shader DisplaceMeshNoise.t3's ComputeShader.Source names). Verbatim math from that .hlsl:
//   variationOffset = hash31((i.x % 1234)/0.123) * Variation
//   GetNoise(pos,var) = (snoiseVec3((pos*0.91 + var)*Frequency + Phase) + OffsetDirection)
//                       * Amount/100 * AmountDistribution
//   Space<0.5 : posInWorld = float3(v.TexCoord.xy, 0)   (PointSpace)
//   Space<1.5 : posInWorld = mul(float4(pos,1), ObjectToWorld).xyz  (WorldSpace)
//   offset = GetNoise(posInWorld, variationOffset) * weight   (weight=1)
//   Direction>0.5 : offset = mul(offset, TBN·ObjectToWorld3x3)  (SurfaceNormal)
//   selection = UseVertexSelection>0.5 ? v.Selected : 1;  offset *= selection
//   Position = v.Position + offset   (final line — note: adds to v.Position, not posInWorld)
//   Normal/Tangent/Bitangent recomputed from noise at tangent anchors (see .hlsl).
//
// It is driven by the GENERIC ComputeShaderStage atom exactly as TiXL wires DisplaceMeshNoise.t3:
//   - the SRV (t0) is the input mesh vertex buffer (SwVertex, stride 80),
//   - the UAV (u0) is the StructuredBufferWithViews write target (80*N),
//   - cb0 (b0) = TransformsConstBuffer (640B, 10 transposed matrices; ObjectToWorld @ offset 384/16 floats),
//   - cb1 (b1) = FloatsToBuffer.Params — THE INTERLEAVED mixed-slot cb. Its 13 floats, in the .t3's Params
//     MultiInput DECLARATION ORDER (== the HLSL cbuffer Params layout, verified against DisplaceMeshNoise.t3):
//       [0]Amount [1]Frequency [2]Phase [3]Variation [4..6]AmountDistribution.xyz
//       [7]RotationLookupDistance [8]UseWAsWeight [9]Space [10]Direction [11]OffsetDirection [12]UseVertexSelection
//     If 骨7b's flatten did NOT preserve this order, these floats land in the WRONG cb1 slots (e.g. Amount
//     and UseVertexSelection swap) → the displacement magnitude/params corrupt → the golden's Position
//     parity goes RED. THAT is the 骨9 tooth.
#include <metal_stdlib>
#include "sw_mesh.h"                        // SwVertex (80B)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
#include "shared/hash.metal.h"              // hash31
#include "shared/noise.metal.h"             // snoiseVec3
using namespace metal;

// cb1 (Params) field accessors — positional, mirroring the HLSL cbuffer Params layout.
struct DmnParams {
  float Amount, Frequency, Phase, Variation;
  float3 AmountDistribution;
  float RotationLookupDistance;
  float UseWAsWeight, Space, Direction, OffsetDirection;
  float UseVertexSelection;
};
static inline DmnParams readParams(constant float* cb) {
  DmnParams p;
  p.Amount = cb[0]; p.Frequency = cb[1]; p.Phase = cb[2]; p.Variation = cb[3];
  p.AmountDistribution = float3(cb[4], cb[5], cb[6]);
  p.RotationLookupDistance = cb[7];
  p.UseWAsWeight = cb[8]; p.Space = cb[9]; p.Direction = cb[10]; p.OffsetDirection = cb[11];
  p.UseVertexSelection = cb[12];
  return p;
}

// mul(float4(v,1), ObjectToWorld) under TransformsConstBuffer's transposed row-based cb layout.
// ObjectToWorld is the 7th matrix (offset 384 bytes = 96 floats) in the 640B TransformsConstBuffer.
static inline float3 mulObjectToWorldPoint(float3 v, constant float* cb0) {
  constant float* m = cb0 + 96;  // ObjectToWorld @ 384B / 4 = 96 floats
  return float3(m[0]*v.x + m[1]*v.y + m[2]*v.z + m[3],
                m[4]*v.x + m[5]*v.y + m[6]*v.z + m[7],
                m[8]*v.x + m[9]*v.y + m[10]*v.z + m[11]);
}

// GetNoise(pos, var) — verbatim mesh-LegacyNoiseDisplace.hlsl GetNoise().
static inline float3 getNoise(float3 pos, float3 var, thread const DmnParams& P) {
  float3 noiseLookup = (pos * 0.91f + var) * P.Frequency + P.Phase;
  float3 noise = snoiseVec3(noiseLookup);
  return (noise + P.OffsetDirection) * P.Amount / 100.0f * P.AmountDistribution;
}

kernel void computeshaderstage_displacemeshnoise(
    const device SwVertex* SourceVerts [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwVertex*       ResultVerts [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*        cb0         [[buffer(CS_CB_BASE + 0)]],    // b0: TransformsConstBuffer (640B)
    constant float*        cb1         [[buffer(CS_CB_BASE + 1)]],    // b1: FloatsToBuffer.Params (interleaved)
    constant uint&         numStructs  [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (SourceVerts count)
    uint3 i [[thread_position_in_grid]]) {
  if (i.x >= numStructs) return;

  DmnParams P = readParams(cb1);

  SwVertex v = SourceVerts[i.x];
  ResultVerts[i.x] = v;  // .hlsl: ResultVertices[i.x] = SourceVertices[i.x]; (copy through)

  float3 variationOffset = hash31((float)(i.x % 1234) / 0.123f) * P.Variation;

  float weight = 1.0f;
  float3 posInWorld = float3(v.Position);
  if (P.Space < 0.5f) {
    posInWorld = float3(v.Texcoord.x, v.Texcoord.y, 0.0f);         // PointSpace
  } else if (P.Space < 1.5f) {
    posInWorld = mulObjectToWorldPoint(float3(v.Position), cb0);   // WorldSpace
  }

  float3 offset = getNoise(posInWorld, variationOffset, P) * weight;

  if (P.Direction > 0.5f) {
    // SurfaceNormal: rotate offset into the (Tangent,Bitangent,Normal)·ObjectToWorld frame.
    float3x3 TBN = float3x3(float3(v.Tangent), float3(v.Bitangent), float3(v.Normal));
    constant float* m = cb0 + 96;
    float3x3 o2w = float3x3(float3(m[0], m[1], m[2]), float3(m[4], m[5], m[6]), float3(m[8], m[9], m[10]));
    TBN = TBN * o2w;
    offset = offset * TBN;
  }

  float selection = (P.UseVertexSelection > 0.5f) ? v.Selection : 1.0f;
  offset *= selection;

  // Position: v.Position + offset (final .hlsl line — adds to the real 3D Position, not posInWorld).
  ResultVerts[i.x].Position = SW_MESH_PACKED3(float3(v.Position) + offset);

  // Normal/Tangent/Bitangent recompute from noise at tangent anchors (verbatim .hlsl).
  float3 newPos = posInWorld + offset;
  float lookUpDistance = P.RotationLookupDistance / P.Frequency;
  weight *= selection;

  float3 tAnchor  = posInWorld + float3(v.Tangent) * lookUpDistance;
  float3 tAnchor2 = posInWorld - float3(v.Tangent) * lookUpDistance;
  float3 newTangent  =  normalize(tAnchor  + getNoise(tAnchor,  variationOffset, P) * weight - newPos);
  float3 newTangent2 = -normalize(tAnchor2 + getNoise(tAnchor2, variationOffset, P) * weight - newPos);
  ResultVerts[i.x].Tangent = SW_MESH_PACKED3(mix(newTangent, newTangent2, 0.5f));

  float3 bAnchor  = posInWorld + float3(v.Bitangent) * lookUpDistance;
  float3 bAnchor2 = posInWorld - float3(v.Bitangent) * lookUpDistance;
  float3 newBitangent  =  normalize(bAnchor  + getNoise(bAnchor,  variationOffset, P) * weight - newPos);
  float3 newBitangent2 = -normalize(bAnchor2 + getNoise(bAnchor2, variationOffset, P) * weight - newPos);
  ResultVerts[i.x].Bitangent = SW_MESH_PACKED3(mix(newBitangent, newBitangent2, 0.5f));

  ResultVerts[i.x].Normal = SW_MESH_PACKED3(cross(float3(ResultVerts[i.x].Tangent),
                                                  float3(ResultVerts[i.x].Bitangent)));
}
