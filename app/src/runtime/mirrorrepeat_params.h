// Shared host<->shader params for the TiXL-ported MirrorRepeat IMAGE FILTER (image/transform).
// Mirrors external/tixl Operators/Lib/Assets/shaders/img/fx/MirrorRepeat.hlsl + MirrorRepeat.cs/.t3.
//
// ============================ cbuffer ORDER FORK (load-bearing) ============================
// The HLSL cbuffer ParamConstants(b0) is NOT in .cs [Input] order. It is:
//     float RotateMirror; float RotateImage; float Width;  float Offset;     // 0,4,8,12
//     float2 OffsetImage; float __dummy__;   float ShadeAmount;              // 16,24,28
//     float4 ShadeColor;  float OffsetEdge;                                  // 32(16-aln),48
// This params struct is laid out in cbuffer order, INCLUDING `__dummy__`. That float is a real
// HLSL packing slot: HLSL packs OffsetImage(2 floats)+__dummy__(1)+ShadeAmount(1) into one 16-byte
// row, then float4 ShadeColor starts the next 16-byte row at offset 32. Dropping `__dummy__` would
// shift ShadeColor and OffsetEdge by 4 bytes -> silent color/edge corruption. So it stays.
// The MSL struct matches this byte-for-byte; the kernel reads `float2 OffsetImage` etc. by name.
//
// NodeSpec ports (point_ops_mirrorrepeat.cpp) follow .cs [Input] order instead (Image, RotateMirror,
// RotateImage, Width, Offset, OffsetEdge, Offsetimage, ShadeAmount, ShadeColor, Resolution). The
// cook reads each named param and assembles THIS struct in cbuffer order — the two orderings are
// decoupled (cs-order at the port boundary, cbuffer-order at the GPU boundary), per the護欄.
//
// ResolutionConstants(b1) in the HLSL (TargetWidth/TargetHeight) is folded onto the tail here and
// bound as the same buffer; the cook fills it from the output texture dimensions (= the resolution
// the node is rendered at), mirroring how Displace passes TargetWidth/TargetHeight.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct MirrorRepeatParams {
  // --- cbuffer ParamConstants(b0), HLSL byte order (see header) ---
  float RotateMirror;   // 0
  float RotateImage;    // 4
  float Width;          // 8
  float Offset;         // 12

#ifdef __METAL_VERSION__
  float2 OffsetImage;   // 16
#else
  float OffsetImageX;   // 16
  float OffsetImageY;   // 20
#endif
  float __dummy__;      // 24  — REAL HLSL packing slot; do NOT remove (alignment fork)
  float ShadeAmount;    // 28

#ifdef __METAL_VERSION__
  float4 ShadeColor;    // 32 (16-byte aligned row)
#else
  float ShadeColorX;    // 32
  float ShadeColorY;    // 36
  float ShadeColorZ;    // 40
  float ShadeColorW;    // 44
#endif
  float OffsetEdge;     // 48

  // --- ResolutionConstants(b1) folded on (filled from output texture dims) ---
  float TargetWidth;    // 52
  float TargetHeight;   // 56
  // Pad 60..80. Host packs all-float (no float2/float4) -> 5 floats = exactly 80. MSL's float4
  // ShadeColor forces 16-byte struct alignment; these 5 floats end at 80 (already 16-multiple),
  // so both sides agree on sizeof == 80 (asserted below).
  float _padTail[5];    // 60..80
};

enum MirrorRepeatBinding {
  MR_Params = 0,  // constant MirrorRepeatParams& (b0)
  // texture(0) = inputTexture, sampler(0) = linear + mirror-repeat wrap; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(MirrorRepeatParams) == 80, "MirrorRepeatParams 80 bytes (16-byte multiple)");
#endif
