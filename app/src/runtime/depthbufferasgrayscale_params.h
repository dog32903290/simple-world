// Shared host<->shader params for the TiXL-ported DepthBufferAsGrayScale IMAGE FILTER
// (lane image_filter). Mirrors external/tixl Operators/Lib/Assets/shaders/img/post-fx/
// depth-to-linear.hlsl and DepthBufferAsGrayScale.cs/.t3.
//
// TiXL authority: DepthBufferAsGrayScale.cs (Texture2d/NearFarRange/OutputRange/ClampOutput/Mode
// inputs) + DepthBufferAsGrayScale.t3 (defaults + the FloatsToBuffer cbuffer routing) +
// depth-to-linear.hlsl (the compute kernel: reverse-projects the depth-buffer .r channel into a
// linear distance, remaps it through OutputRange, optionally saturates).
//
// CBUFFER ORDER (depth-to-linear.hlsl ParamConstants b0, VERBATIM):
//   float Near; float Far; float OutrangeMin; float OutrangeMax; float ClampRange; float Mode;
// The .t3 FloatsToBuffer feeds those 6 scalars in this exact connection order (verified against
// DepthBufferAsGrayScale.t3 Connections, lines 259-293): NearFarRange.X, NearFarRange.Y,
// OutputRange.X, OutputRange.Y, BoolToFloat(ClampOutput), IntToFloat(Mode). 1:1, no FloatsToBuffer
// math-node routing trap (each scalar is a direct Vector2Components / Bool/IntToFloat output).
//
// Layout: 6 floats (24) padded to 32 (16-byte multiple).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct DepthBufferAsGrayScaleParams {
  // depth-to-linear.hlsl ParamConstants (b0), in cbuffer order.
  float Near;        // TiXL NearFarRange.X, default 0.01; camera near clip
  float Far;         // TiXL NearFarRange.Y, default 1000.0; camera far clip
  float OutrangeMin; // TiXL OutputRange.X, default 0.0; remap floor (skipped if Min==0 && Max==0)
  float OutrangeMax; // TiXL OutputRange.Y, default 5.0; remap ceiling
  float ClampRange;  // TiXL ClampOutput (bool->float), default 0.0; saturate output if > 0.5
  float Mode;        // TiXL Mode (int->float), default 0.0; <0.5 = standard, >=0.5 = legacy DoF
  float _pad[2];     // pad 24 -> 32 (16-byte multiple)
};

enum DepthBufferAsGrayScaleBinding {
  DEPTHBUFFERASGRAYSCALE_Params = 0,  // constant DepthBufferAsGrayScaleParams& (b0)
  // texture(0) = Texture2d (depth buffer; .r read as depth), sampler(0) = point+clamp.
  // Dimensions read in-shader (TiXL GetTextureSize -> no Resolution cbuffer port).
};

#ifndef __METAL_VERSION__
static_assert(sizeof(DepthBufferAsGrayScaleParams) == 32,
              "DepthBufferAsGrayScaleParams 32 bytes (16-byte multiple)");
#endif
