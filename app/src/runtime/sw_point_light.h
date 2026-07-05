// runtime/sw_point_light — the host-side PointLight value type (the lighting seam's currency).
//
// 1:1 transcription of external/tixl Core/Rendering/PointLightStack.cs:10-36 (the [StructLayout Size=48]
// PointLight struct), the same posture SwGradient mirrors Core/DataTypes/Gradient.cs. A PointLight is a
// pure CPU value the Command-rail SetPointLight op constructs from its inputs and pushes onto the
// EvaluationContext.PointLights stack for the SubGraph's lifetime (SetPointLight.cs:16-26). The PBR draw
// pass reads that stack; sw's PBR render pass is the BLOCKED `NEW-SEAM:lighting` island (census
// ops-render.md:112-114, CLONE_MAP.md:117 "新島,最大單塊"), so this currency + its resolver are the
// value slice that lands NOW — proven against the .cs by a data-layer golden — while the context-stack
// push/pop + PBR-shader consumer wait for that render battle. PURE FLOAT MATH — no Metal, no platform dep.
//
// GPU layout (PointLightStack.cs FieldOffsets, 48 bytes): the render pass will pack this struct for the
// light const-buffer; the field order/offsets are transcribed so that packing is byte-parity when it lands.
//   Position float3 @0 | Intensity float @12 | Color float4 @16 | Range float @32 | Decay float @36
#pragma once

#include <simd/simd.h>

namespace sw {

// One point light. Mirror of T3.Core.Rendering.PointLight (PointLightStack.cs:11-36). Field order and the
// implied 48-byte packing match the [StructLayout(LayoutKind.Explicit)] offsets so a future light-buffer
// upload is byte-identical. Defaults here = the CONSTRUCTOR's default arg only (decay=2, PointLightStack.cs:13);
// Position/Intensity/Color/Range have no constructor default (the SetPointLight input-slot defaults supply
// those — all zero, SetPointLight.cs:33-42 — resolved by swResolvePointLight below).
struct SwPointLight {
  simd::float3 position = simd::make_float3(0, 0, 0);       // @0   (PointLightStack.cs:23)
  float intensity = 0.0f;                                   // @12  (cs:26)
  simd::float4 color = simd::make_float4(0, 0, 0, 0);       // @16  (cs:29)
  float range = 0.0f;                                       // @32  (cs:32)
  float decay = 2.0f;                                       // @36  (cs:35; constructor default arg =2, cs:13)
};

// Build a PointLight from the SetPointLight op's resolved inputs — VERBATIM the constructor call at
// SetPointLight.cs:17-21: new PointLight(Position, Intensity, Color, Range, Decay). No clamping, no
// remap: the op passes its raw input values straight into the struct (the only defaulting is the input
// SLOTS' own defaults, all zero except none — Decay's input slot default is 0, SetPointLight.cs:44-45,
// which OVERRIDES the constructor's decay=2 because the op always passes Decay.GetValue explicitly).
// So at op defaults every field is 0 including decay (the constructor default never fires through the op).
inline SwPointLight swResolvePointLight(simd::float3 position, float intensity, simd::float4 color,
                                        float range, float decay) {
  SwPointLight l;
  l.position = position;   // SetPointLight.cs:17
  l.intensity = intensity; // cs:18
  l.color = color;         // cs:19
  l.range = range;         // cs:20
  l.decay = decay;         // cs:21 (op passes Decay explicitly → input-slot default 0 wins, not ctor =2)
  return l;
}

}  // namespace sw
