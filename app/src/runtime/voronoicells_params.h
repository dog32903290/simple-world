// Shared host<->shader params for the TiXL-ported VoronoiCells IMAGE FILTER (lane image_filter).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/VoronoiCells.hlsl and VoronoiCells.cs/.t3.
// TiXL authority: VoronoiCells.cs (Image/EdgeColor/Background/Scale/EdgeWidth/Resolution/Phase
// inputs) + VoronoiCells.hlsl (the single-pass kernel: iq Voronoi cell + correct border distance,
// cell colour sampled from the input texture as the feature-point field, edge tinted by EdgeColor).
//
// b0 ParamConstants order (VoronoiCells.hlsl lines 5-15): float4 EdgeColor; float4 Background;
// float Scale; float LineWidth; float Phase. Layout: 2*16 + 3*4 = 44 -> padded to 48.
//
// The HLSL declares a Resolution cbuffer at register b2 (TargetWidth/TargetHeight) used to compute
// aspectRatio. That is framework-injected (the host fills it from the output size), NOT a port; we
// bind it as a second Metal cbuffer filled from c.output->width()/height() — same as the
// ChromaticAbberation Resolution(b1) handling. (VoronoiCells.cs's `Resolution` Int2 input is the
// OUTPUT TEXTURE SIZE selector, not this cbuffer — modelled as the standard Resolution enum +
// CustomW/H ports in the VoronoiCells NodeSpec — see point_ops_voronoicells.cpp's ImageFilterOp registrar.)
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct VoronoiCellsParams {
  float EdgeColorR, EdgeColorG, EdgeColorB, EdgeColorA;  // TiXL EdgeColor (Vec4), default (0,0,0,1)
  float BackgroundR, BackgroundG, BackgroundB, BackgroundA;  // TiXL Background (Vec4), default (1,1,1,1)
  float Scale;      // TiXL Scale (Single), default 10.0; voronoi cell grid frequency
  float LineWidth;  // TiXL EdgeWidth (Single, shader name LineWidth), default 0.68; edge thickness
  float Phase;      // TiXL Phase (Single), default 0.0; animates feature-point jitter
  float _pad;       // pad 44 -> 48 (16-byte multiple)
};

struct VoronoiCellsResolution {
  // Mirrors VoronoiCells.hlsl b2 Resolution cbuffer (TargetWidth/TargetHeight); host-filled.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16
};

enum VoronoiCellsBinding {
  VORONOI_Params     = 0,  // constant VoronoiCellsParams& (b0)
  VORONOI_Resolution = 1,  // constant VoronoiCellsResolution& (HLSL b2; bound at Metal index 1)
  // texture(0) = inputTexture, sampler(0) = linear+clamp; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(VoronoiCellsParams) == 48, "VoronoiCellsParams 48 bytes (16-byte multiple)");
static_assert(sizeof(VoronoiCellsResolution) == 16, "VoronoiCellsResolution 16 bytes");
#endif
