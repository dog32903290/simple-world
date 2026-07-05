// runtime/sw_pbr_material — the host-side PBR material PARAMETERS value type (the pbr-material seam's
// value currency). 1:1 transcription of external/tixl Core/Rendering/Material/PbrMaterial.cs:29-51 (the
// [StructLayout Size=48] PbrParameters struct — the SHADER-uploaded numeric half of a PbrMaterial).
//
// SCOPE (named, honest boundary): a full TiXL PbrMaterial (PbrMaterial.cs:13-22) is PbrParameters PLUS
// four ShaderResourceView texture maps (Albedo/Emissive/RoughnessMetallicOcclusion/Normal) PLUS a GPU
// ParameterBuffer, and it rides a context.Materials stack + context.PbrMaterial that ONLY the PBR draw
// shader reads. The texture SRVs are GPU resources and the context stack + shader consumer are the
// BLOCKED `NEW-SEAM:pbr-material` island (census ops-render.md:106-108, CLONE_MAP.md:117 "新島,最大單塊").
// This header ports the PURE-VALUE slice — the PbrParameters numeric struct that SetMaterial writes from
// its scalar/color inputs (SetMaterial.cs:34-38) — proven against the .cs by a data-layer golden. The
// texture maps + context stack + PBR shader wait for the render battle. PURE FLOAT MATH, no Metal.
//
// GPU layout (PbrParameters FieldOffsets, 48 bytes = Stride 12*4): BaseColor float4 @0 | EmissiveColor
// float4 @16 | Roughness float @32 | Specular float @36 | Metal float @40 | __padding float @44.
#pragma once

#include <simd/simd.h>

namespace sw {

// PBR shader parameters. Mirror of PbrMaterial.PbrParameters (PbrMaterial.cs:30-51). 48-byte packing per
// the [StructLayout(LayoutKind.Explicit)] offsets (incl. the trailing __padding at @44) so a future
// parameter const-buffer upload is byte-identical. Defaults here = the TiXL _defaultParameters
// (PbrMaterial.cs:83-88): the material a fresh PbrMaterial() carries BEFORE SetMaterial overrides it.
struct SwPbrParameters {
  simd::float4 baseColor = simd::make_float4(1, 1, 1, 1);      // @0  (PbrMaterial.cs:33; default Vector4.One, cs:85)
  simd::float4 emissiveColor = simd::make_float4(0, 0, 0, 1);  // @16 (cs:36; default (0,0,0,1), cs:86)
  float roughness = 0.5f;                                      // @32 (cs:39; default 0.5, cs:87)
  float specular = 10.0f;                                      // @36 (cs:42; default 10, cs:88)
  float metal = 0.0f;                                          // @40 (cs:45; default 0, cs:89)
  // @44 __padding (PbrMaterial.cs:48) — implicit in the C++ struct's own 16-byte alignment; kept in the
  // comment as the layout note so the render-pass packer accounts for the 48-byte stride.
};

// Build PbrParameters from SetMaterial's resolved scalar/color inputs — VERBATIM the field writes at
// SetMaterial.cs:34-38: Parameters.{BaseColor, EmissiveColor, Roughness, Specular, Metal} = the inputs.
// Raw passthrough, no remap. (The op's texture-map inputs + Name/MaterialId + context push are the
// deferred render-pass half; this resolver is only the numeric parameter struct.)
inline SwPbrParameters swResolvePbrParameters(simd::float4 baseColor, simd::float4 emissiveColor,
                                              float roughness, float specular, float metal) {
  SwPbrParameters p;
  p.baseColor = baseColor;         // SetMaterial.cs:34
  p.emissiveColor = emissiveColor; // cs:35
  p.roughness = roughness;         // cs:36
  p.specular = specular;           // cs:37
  p.metal = metal;                 // cs:38
  return p;
}

}  // namespace sw
