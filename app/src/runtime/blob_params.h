// Shared host<->shader params for the TiXL-ported Blob IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/basic/Blob.cs (inputs/defaults) +
// Blob.t3 (defaults: Scale=0.5, Stretch=(1,1), Rotate=0, Feather=1.0, FeatherBias=0,
// Position=(0,0), Color=(1,1,1,1), Background=(1,1,1,0), BlendMode=0, Image=null,
// GenerateMips=false, TextureFormat=R16G16B16A16_Float) +
// Assets/shaders/img/generate/Blob.hlsl (the single-pass kernel: aspect-corrected
// rotation, radial SDF with smoothstep feather, pow-bias, optional blend over Image).
//
// cbuffer b0 order traced from Blob.t3 connections to slot 4ef6f204 (_ImageFxShaderSetupStatic
// FloatsToBuffer input), document order — ZERO intermediate math nodes (no Multiply etc):
//   Color→Vec4Components (R,G,B,A) → Fill x4
//   Background→Vec4Components (R,G,B,A) → Background x4
//   Stretch→Vec2Components (X,Y) → Stretch x2
//   Position→Vec2Components (X,Y) → Position x2
//   Scale (direct float) x1
//   Feather (direct float) x1
//   FeatherBias (direct float → GradientBias in shader) x1
//   Rotate (direct float) x1
//   BlendMode (IntToFloat) → BlendMode float x1
//   IsTextureValid (injected by _ImageFxShaderSetupStatic infra) x1
//
// Total: 18 floats = 72 bytes → pad to 80 (16-byte multiple).
//
// cbuffer b1 = BlobResolution: TargetWidth/TargetHeight (aspect correction in the shader).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct BlobParams {
  // TiXL Blob.hlsl cbuffer ParamConstants (b0) field order:
  float FillR, FillG, FillB, FillA;        // Fill/Color (Vec4), default (1,1,1,1)
  float BgR, BgG, BgB, BgA;                // Background (Vec4), default (1,1,1,0)
  float StretchX, StretchY;                 // Stretch (Vec2), default (1,1)
  float PositionX, PositionY;               // Position (Vec2), default (0,0)
  float Scale;                              // Scale (float), default 0.5
  float Feather;                            // Feather (float), default 1.0
  float GradientBias;                       // FeatherBias/GradientBias (float), default 0.0
  float Rotate;                             // Rotate (float), default 0.0
  float BlendMode;                          // BlendMode (int→float via IntToFloat), default 0
  float IsTextureValid;                     // Injected by cook infra: 1.0=Image wired, 0.0=null
  float _pad[2];                            // pad 72 -> 80 (16-byte multiple)
};

struct BlobResolution {
  // TiXL Blob.hlsl Resolution cbuffer (b1): TargetWidth/TargetHeight for aspect ratio.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum BlobBinding {
  BLOB_Params     = 0,  // constant BlobParams& (b0)
  BLOB_Resolution = 1,  // constant BlobResolution& (b1)
  BLOB_Texture    = 0,  // texture2d ImageA at register(t0) — may be 1x1 black dummy
  BLOB_Sampler    = 0,  // sampler at register(s0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(BlobParams) == 80, "BlobParams must be 80 bytes (16-byte multiple)");
static_assert(sizeof(BlobResolution) == 16, "BlobResolution must be 16 bytes");
#endif
