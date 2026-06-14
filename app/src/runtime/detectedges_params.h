// Shared host<->shader params for the TiXL-ported DetectEdges IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/DetectEdges.hlsl and DetectEdges.cs/.t3.
// TiXL authority: DetectEdges.cs (Image/SampleRadius/Strength/Contrast/Color/MixOriginal/
// OutputAsTransparent inputs) + DetectEdges.hlsl (the single-pass kernel: 4-neighbour absolute
// colour difference edge magnitude, * Strength + Contrast, tinted by Color, lerp to original).
//
// Only the ParamConstants(b0) fields are op params. DetectEdges.hlsl's second/third cbuffers
// (b1 Resolution = framework-injected TargetWidth/TargetHeight, used here via the texture's own
// dimensions; b2 ParamConstants{int Invert} — NOT a .cs input, never wired, always 0) are NOT
// exposed as ports (orchestrator guardrail: unused 2nd cbuffer fields ≠ op params).
//
// HLSL ParamConstants order: float4 Color; then SampleRadius/Strength/Contrast/MixOriginal/
// OutputAsTransparent. Layout: Color(16) + 5 floats(20) = 36, padded to 48 (16-byte multiple).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct DetectEdgesParams {
  // TiXL DetectEdges.hlsl ParamConstants (b0): float4 Color first, then the 5 scalars.
  float ColorR, ColorG, ColorB, ColorA;  // TiXL Color (Vec4), default (1,1,1,1); edge tint
  float SampleRadius;       // TiXL SampleRadius (Single), default 1.0; neighbour offset in pixels
  float Strength;           // TiXL Strength (Single), default 1.0; edge magnitude gain
  float Contrast;           // TiXL Contrast (Single), default 0.0; flat add to edge magnitude
  float MixOriginal;        // TiXL MixOriginal (Single), default 0.0; lerp edge<->original
  float OutputAsTransparent;// TiXL OutputAsTransparent (bool), default false; alpha=edge vs Color.a
  float _pad[3];            // pad 36 -> 48 (16-byte multiple)
};

enum DetectEdgesBinding {
  DETECTEDGES_Params = 0,  // constant DetectEdgesParams& (b0)
  // texture(0) = Image, sampler(0) = linear+clamp; bound directly. Dimensions read in-shader.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DetectEdgesParams) == 48, "DetectEdgesParams 48 bytes (16-byte multiple)");
#endif
