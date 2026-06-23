// Shared host<->shader params for the TiXL-ported Grain IMAGE FILTER (Lane B Phase-C fan-out).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/generate/Grain.hlsl ParamConstants (b0)
// and Grain.cs / Grain.t3. TiXL authority:
//   Grain.cs    — Image / Amount / Color / Exponent / Brightness / Animate / RandomPhase / Scale /
//                 Resolution / GenerateMipmaps inputs.
//   Grain.hlsl  — single-pass kernel: per-pixel animated colour-noise added to the source.
//   Grain.t3    — defaults: Amount 0.05 / Color 0 / Exponent 1 / Brightness 0 / Animate 5 /
//                 RandomPhase 0 / Scale 0.
//
// Grain.hlsl cbuffer b0 declares (verbatim order): Amount, Color, Exponent, Brightness, Time, Scale.
//   - `Time` is the HOST-supplied animation phase. In TiXL the host multiplies the global time by
//     Animate and adds RandomPhase (the .cs feeds those into the setup's time slot). Headless port:
//     Time is a single float the host fills (selftest passes 0 -> deterministic). The Animate /
//     RandomPhase ports remain in the NodeSpec for parity and feed Time host-side (FORK, named).
//   - The Resolution cbuffer (b1) holds TargetWidth/TargetHeight — needed by the Scale>1 branch for
//     the per-pixel step. Host fills it from c.output->width()/height().
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct GrainParams {
  // Grain.hlsl ParamConstants (b0), verbatim declaration order.
  float Amount;      // Grain.t3 default 0.05; noise gain added to source rgb
  float Color;       // Grain.t3 default 0.0;  0 = grayscale noise, 1 = per-channel colour noise
  float Exponent;    // Grain.t3 default 1.0;  contrast curve on the signed noise
  float Brightness;  // Grain.t3 default 0.0;  additive bias on the noise
  float Time;        // host-supplied animation phase (Animate*time + RandomPhase); 0 = static
  float Scale;       // Grain.t3 default 0.0;  >1 enables the quantised (blocky) noise branch
  float _pad[2];     // pad 6 floats -> 8 = 32 bytes (16-byte multiple)
};

struct GrainResolution {
  // Grain.hlsl Resolution cbuffer (b1): TargetWidth, TargetHeight.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum GrainBinding {
  GRAIN_Params     = 0,  // constant GrainParams& (b0)
  GRAIN_Resolution = 1,  // constant GrainResolution& (b1)
  // texture(0) = ImageA, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(GrainParams) == 32, "GrainParams 32 bytes (16-byte multiple)");
static_assert(sizeof(GrainResolution) == 16, "GrainResolution 16 bytes");
#endif
