// Shared host<->shader params for the TiXL-ported Pixelate IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/Pixelate.hlsl and Pixelate.cs/.t3.
// TiXL authority: Pixelate.cs (Image/Color/Divisor/TileAmount/Shape inputs) + Pixelate.hlsl
// (the single-pass kernel: tile-quantize the UV, point-sample the cell center, multiply by a
// per-cell repeated Shape texture and a Color multiplier).
//
// FORK (named — Shape texture omitted): TiXL's Pixelate samples a SECOND texture `Shape` (t1)
// tiled once per cell and multiplies (`return tileShape * imageColor * Color`). Pixelate.t3
// defaults Shape to `Lib:images/basic/white.png` — a solid white texture, so the default Shape
// contributes (1,1,1,1) and is a visual no-op. We omit the Shape texture input entirely and
// treat it as that constant white default (tileShape = 1). This matches the default-wired TiXL
// node exactly; a non-default Shape (custom per-cell stamp) is a follow-up, same fork class as
// Blur/Displace's omitted Wrap. Documented in pixelate.metal too.
//
// Resolution cbuffer (b1) holds TargetWidth/TargetHeight, matching the .hlsl's `Resolution`
// register(b1). The host fills it from c.output->width()/height(). Note: the kernel derives the
// tile grid from Image.GetDimensions (the source texture size), NOT from this cbuffer — we pass
// the source dimensions in here so the Metal kernel can reproduce GetDimensions faithfully.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct PixelateParams {
  // TiXL Pixelate.hlsl cbuffer (b0): Color (float4), Divisor (float), TileAmount (float2)
  float ColorR, ColorG, ColorB, ColorA;  // TiXL Color (Vec4), default (1,1,1,1); output multiplier
  float Divisor;                          // TiXL Divisor (int), default 0; >0.5 -> uniform tiles
  float TileAmountX, TileAmountY;         // TiXL TileAmount (Int2), default (160,90)
  float _pad0;                            // pad 28 -> 32 (16-byte multiple)
};

struct PixelateResolution {
  // TiXL Pixelate.hlsl Resolution cbuffer (b1): TargetWidth, TargetHeight.
  // We pass the SOURCE image dimensions here (= Image.GetDimensions in the .hlsl tile math).
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum PixelateBinding {
  PIXELATE_Params     = 0,  // constant PixelateParams& (b0)
  PIXELATE_Resolution = 1,  // constant PixelateResolution& (b1)
  // texture(0) = Image, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(PixelateParams) == 32, "PixelateParams 32 bytes (16-byte multiple)");
static_assert(sizeof(PixelateResolution) == 16, "PixelateResolution 16 bytes");
#endif
