// Shared host<->shader params for the TiXL-ported EdgeRepeat IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/EdgeRepeat.hlsl and EdgeRepeat.cs/.t3.
// TiXL authority: EdgeRepeat.cs (Image/Fill/Background/LineColor/Center/Width/Rotation/LineThickness/
// Resolution inputs) + EdgeRepeat.t3 (defaults Fill=(1,1,1,1), Background=(1,0.99999,0.99999,0.804),
// LineColor=(1,1,1,1), Center=(0,0), Width=0.25, Rotation=45, LineThickness=0, Wrap="Mirror",
// BlendMode=4) + EdgeRepeat.hlsl (mirror-line reflect/repeat — a kaleidoscope fold across a
// rotated line through Center).
//
// The .hlsl ParamConstants cbuffer (b0) field order is verbatim:
//   float4 Fill; float4 Background; float4 LineColor; float2 Center; float Width; float Rotation;
//   float LineThickness;
// We mirror that field order with flat float arrays/scalars so byte offsets match the HLSL
// register(b0) exactly (each float4 occupies a 16-byte row; Center+Width+Rotation pack into one row;
// LineThickness starts the next row).
//
// The .hlsl ALSO declares a TimeConstants cbuffer (b1, globalTime/time/runTime/beatTime) that is
// UNUSED by psMain — we do NOT expose it as a port (work-order guard). A second cbuffer holds the
// resolution (TargetWidth/TargetHeight), which psMain DOES use for aspectRatio. In the .hlsl this
// resolution lives in register(b2) (confusingly also named "TimeConstants"); the host fills it from
// c.output->width/height and binds it at our EDGEREPEAT_Resolution slot.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct EdgeRepeatParams {
  // TiXL EdgeRepeat.hlsl cbuffer (b0), field order verbatim (flat scalars to match HLSL offsets).
  float Fill[4];        // TiXL Fill (Vector4), default (1,1,1,1); color tint on the kept (in-range) region
  float Background[4];  // TiXL Background (Vector4), default (1,.99999,.99999,.804); tint on repeated region
  float LineColor[4];   // TiXL LineColor (Vector4), default (1,1,1,1); color of the fold line
  float Center[2];      // TiXL Center (Vector2), default (0,0); the fold-line anchor point
  float Width;          // TiXL Width (Single), default 0.25; band half-width before mirroring
  float Rotation;       // TiXL Rotation (Single), default 45.0; fold-line angle in degrees
  float LineThickness;  // TiXL LineThickness (Single), default 0.0; visible fold-line thickness
  float _pad[3];        // pad to 80 (matches the HLSL cbuffer's LineThickness row = 16 bytes)
};

struct EdgeRepeatResolution {
  // TiXL EdgeRepeat.hlsl resolution cbuffer (b2, "TimeConstants" name): TargetWidth, TargetHeight.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum EdgeRepeatBinding {
  EDGEREPEAT_Params     = 0,  // constant EdgeRepeatParams& (b0)
  EDGEREPEAT_Resolution = 1,  // constant EdgeRepeatResolution& (resolution; .hlsl b2)
  // texture(0) = ImageA, sampler(0) = linear + MIRROR wrap (TiXL .t3 Wrap="Mirror", load-bearing).
};

#ifndef __METAL_VERSION__
// 4+4+4+2+1+1+1 = 17 floats of data + 3 pad floats = 20 floats = 80 bytes. Flat layout offsets:
// Fill 0..15, Background 16..31, LineColor 32..47, Center 48..55, Width 56, Rotation 60,
// LineThickness 64 — byte-identical to the HLSL cbuffer (b0). The Metal shader reads each field as
// a flat float[]/scalar (NOT float4/float2) to preserve this layout.
static_assert(sizeof(EdgeRepeatParams) == 80, "EdgeRepeatParams 80 bytes (20 floats)");
static_assert(sizeof(EdgeRepeatResolution) == 16, "EdgeRepeatResolution 16 bytes");
#endif
