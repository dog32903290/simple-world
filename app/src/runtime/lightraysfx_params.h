// Shared host<->shader params for the TiXL-ported LightRaysFx IMAGE FILTER.
// Authority (ported 1:1): external/tixl Operators/Lib/image/fx/stylize/LightRaysFx.{cs,t3} +
// Operators/Lib/Assets/shaders/img/fx/LightRayFx.hlsl.
//
// ★ BACKWARD-TRACE FINDING (查 TiXL 不發明 — reality differs from the "2-pass" scout sketch):
// LightRaysFx is a SINGLE-PASS op in production. The .t3 wires the Output (root slot bdc413f2) from
// ExecuteTextureUpdate 4d393e7f, which runs Execute 87a6d72d → PixelShader ca5e278b = entrypoint
// `psMain` → renders ONCE into Texture2d ecbbe9ba = the Output. The shader's `Pass2Refine` entrypoint
// AND its whole chain (PixelShader 1c9ee15f, Texture2d 273c8ffb, Executes be7d9e8c/e75e5433, RTV
// e0663d81, etc.) are NOT reachable from Output — they are the author's abandoned refine attempt
// (the .hlsl comment: "An ill-fated attempt of a refinement pass. Sadly it has too many artifacts to
// be usable."), left wired-up-but-dead in the graph. A reachability sweep from the Output root over
// the .t3 Connections proves Pass2Refine is dead. So we port psMain only. (This is the Cut55 lesson:
// trace the ACTUAL output chain backward, do not trust a forward "looks like 2 passes" reading.)
//
// FloatsToBuffer (b941977f) packing order = its MultiInput connection order (file order = load order =
// FloatsToBuffer.cs Params.GetCollectedTypedInputs order). psMain's cbuffer (b0):
//   float2 Center; float NumSamples; float Length; float4 RayColor;
//   float Decay; float ApplyFxToBackground; float Amount; float RefineFactor; float RefineSamples;
//   float AspectRatio;
// Packed floats in connection order [0..12]: Direction.x, Direction.y, Samples(int→float), Length,
// RayColor.x/.y/.z/.w, Decay, ApplyFx, Amount, [no-input IntToFloat=0], [Div=W/H]. That is 13 floats,
// so the LAST two HLSL fields (RefineSamples@48, AspectRatio@52) receive only ONE value (the W/H Div)
// then nothing → RefineFactor=0, RefineSamples=W/H, AspectRatio=0. BUT psMain uses NONE of those three
// (only Pass2Refine reads RefineFactor/RefineSamples/AspectRatio, and Pass2Refine is dead) → the
// trailing-field misroute is irrelevant to the production output. We therefore carry ONLY the fields
// psMain actually consumes; the dead refine fields are omitted (named: not a fork — psMain output is
// byte-identical, the omitted fields never affect it).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// psMain cbuffer (the live fields only — see header note). Layout pinned to 48B (16-byte multiples).
//   Center      @0  (float2)  = Direction.xy  (shader does Center*(1,-1)+0.5 → screen-space source)
//   NumSamples  @8  (float)   = Samples (int default 100)
//   Length      @12 (float)   = ray length (default 0.4)
//   RayColor    @16 (float4)  = ray tint (default 1,1,1,1)
//   Decay       @32 (float)   = falloff exponent (default 0.9)
//   ApplyFx     @36 (float)   = blend FxImage into base (default 1.0)
//   Amount      @40 (float)   = ray strength (default 5.0)
//   HasFx       @44 (float)   = 1 if a TextureFX input is wired, else 0. When 0 the shader treats
//                              FxImage as white(1,1,1,1) — byte-identical to TiXL's FirstValidTexture
//                              fallback (TextureFX null → Lib:images/basic/white-pixel.png). NOT a
//                              fork: it reproduces the .t3 FirstValidTexture(TextureFX, white-pixel)
//                              default exactly without needing the asset decoder for the unwired case.
struct LightRaysFxParams {
#ifdef __METAL_VERSION__
  float2 Center;
#else
  float Center[2];
#endif
  float NumSamples;
  float Length;
#ifdef __METAL_VERSION__
  float4 RayColor;
#else
  float RayColor[4];
#endif
  float Decay;
  float ApplyFx;
  float Amount;
  float HasFx;
};

enum LightRaysFxBinding {
  LIGHTRAYSFX_Params = 0,  // constant LightRaysFxParams& (b0)
  // texture(0) = Image (t0), texture(1) = FxImage (t1, optional), sampler(0) = linear clamp.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(LightRaysFxParams) == 48, "LightRaysFxParams 48 bytes (2+1+1+4+1+1+1+1)");
#endif
