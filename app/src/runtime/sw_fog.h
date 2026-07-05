// runtime/sw_fog — the host-side Fog parameters value type (the lighting seam's fog currency).
//
// 1:1 transcription of external/tixl Core/Rendering/FogSettings.cs:27-40 (the [StructLayout Size=32]
// FogParameters struct). SetFog.cs:20-25 builds one from its inputs and pushes it as the scoped
// context.FogParameters const-buffer for the SubGraph (SetFog.cs:27-33). The PBR/scene pass reads that
// buffer; that pass is the BLOCKED `NEW-SEAM:lighting` island (census ops-render.md:114), so this
// currency + resolver land NOW (proven against the .cs by a data-layer golden) and the context push +
// shader consumer wait for the render battle. PURE FLOAT MATH — no Metal, no platform dep.
//
// GPU layout (FogSettings.cs FieldOffsets, 32 bytes): Color float4 @0 | Distance float @16 | Bias float @20.
//
// ★TWO defaults, do NOT conflate (a P2-identity trap): the FogParameters STRUCT default (the ambient
// fog when no SetFog runs) is FogSettings.DefaultSettingsBuffer = {Color=(0,0,0,1), Distance=10000,
// Bias=2} (FogSettings.cs:15-20). But SetFog's INPUT SLOTS default to {Distance=0, Bias=0, Color=0}
// (SetFog.cs:41-48 — bare InputSlot<float>()/InputSlot<Vector4>(), no default arg). SetFog builds the
// struct STRICTLY from its inputs (SetFog.cs:20-25), so a SetFog at its input defaults produces
// {Color=(0,0,0,0), Distance=0, Bias=0} — NOT the 10000/2 ambient default. The 10000/2 values live only
// in the fallback buffer SetFog never reads. swResolveFog takes the inputs; swFogAmbientDefault() is the
// separate ambient fallback (the render pass uses it when no SetFog is in scope).
#pragma once

#include <simd/simd.h>

namespace sw {

// Fog parameters. Mirror of FogSettings.FogParameters (FogSettings.cs:28-39). 32-byte packing per the
// [StructLayout(LayoutKind.Explicit)] offsets so a future fog const-buffer upload is byte-identical.
struct SwFogParameters {
  simd::float4 color = simd::make_float4(0, 0, 0, 0);  // @0  (FogSettings.cs:31)
  float distance = 0.0f;                               // @16 (cs:34)
  float bias = 0.0f;                                   // @20 (cs:37)
};

// Build FogParameters from SetFog's resolved inputs — VERBATIM SetFog.cs:20-25:
//   new FogParameters { Bias = Bias, Distance = Distance, Color = Color }.
// No remap; raw input passthrough. Field write order in the .cs is Bias/Distance/Color (immaterial —
// struct fields, not sequential math).
inline SwFogParameters swResolveFog(simd::float4 color, float distance, float bias) {
  SwFogParameters f;
  f.color = color;       // SetFog.cs:24
  f.distance = distance; // cs:23
  f.bias = bias;         // cs:22
  return f;
}

// The AMBIENT fog fallback (used by the render pass when NO SetFog is in scope): the
// FogSettings.DefaultSettingsBuffer values (FogSettings.cs:15-20). NOT what SetFog produces at its input
// defaults — kept separate so the render-pass seam can pick the right one, and so the golden can prove
// the two are distinct (guarding the P2-identity trap of testing SetFog only at 10000/2).
inline SwFogParameters swFogAmbientDefault() {
  SwFogParameters f;
  f.color = simd::make_float4(0, 0, 0, 1);  // FogSettings.cs:19
  f.distance = 10000.0f;                    // cs:18
  f.bias = 2.0f;                            // cs:17
  return f;
}

}  // namespace sw
