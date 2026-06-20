// Shared host<->shader params for the TiXL-ported ZollnerPattern IMAGE GENERATOR.
// TiXL authority: Operators/Lib/image/generate/pattern/ZollnerPattern.cs (inputs/GUIDs) +
// ZollnerPattern.t3 (defaults: Stretch=(0.5,1.0), Offset=(0,0), Scale=1.0, Rotate=45.0,
// Feather=0.02, BarWidth=0.2, HookRotation=60.0, HookLength=0.7, HookWidth=0.33,
// RowSwift=0.0, RAffects_BarWidth=0.0, GAffects_HookLength=0.0, BAffects_HookRotation=0.0,
// AmplifyIllusion=0.0, Background=(1,1,1,1), Fill=(0,0,0,1), Image=null) +
// Assets/shaders/img/fx/ZollnerGrid.hlsl (the single-pass Zöllner optical-illusion kernel).
//
// cbuffer b0 order traced from ZollnerPattern.t3 connections to slot 4ef6f204
// (_ImageFxShaderSetupStatic FloatsToBuffer input), document connection order:
//   Fill→Vec4Components (R,G,B,A)     x4
//   Background→Vec4Components (R,G,B,A) x4
//   Stretch→Vec2Components (X,Y)       x2
//   Offset→Vec2Components (X,Y)        x2
//   Scale (direct float)               x1
//   Rotate (direct float)              x1
//   Feather (direct float)             x1
//   HookRotation (direct float)        x1
//   HookLength (direct float)          x1
//   HookWidth (direct float)           x1
//   BarWidth (direct float)            x1
//   RowSwift (direct float)            x1
//   RAffects_BarWidth (direct float)   x1
//   GAffects_HookLength (direct float) x1
//   BAffects_HookRotation (direct)     x1
//   AmplifyIllusion (direct float)     x1
//
// Total: 4+4+2+2+1*12 = 24 floats = 96 bytes (already 16-byte aligned, no pad needed).
//
// cbuffer b1 = ZollnerPatternResolution: TargetWidth/TargetHeight (aspect ratio in shader).
//
// FORK (named): Image input (optional, TiXL default null) — when wired, imgColorForCel.r/g/b
// modulate BarWidth/HookLength+Width/HookRotation. We bind a 1×1 transparent-black dummy when
// null (same as Blob/SinForm generator convention, Cut 61). This means all affect floats
// contribute 0 → clean generator output identical to TiXL with Image=null.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ZollnerPatternParams {
  // TiXL ZollnerGrid.hlsl cbuffer ParamConstants (b0) field order (STEP-0 traced):
  float FillR,  FillG,  FillB,  FillA;         // Fill (Vec4),       default (0,0,0,1) black
  float BgR,    BgG,    BgB,    BgA;            // Background (Vec4), default (1,1,1,1) white
  float StretchX, StretchY;                      // Stretch (Vec2),    default (0.5,1.0)
  float OffsetX,  OffsetY;                       // Offset (Vec2),     default (0,0)
  float ScaleFactor;                             // Scale,             default 1.0
  float Rotate;                                  // Rotate (deg),      default 45.0
  float Feather;                                 // Feather,           default 0.02
  float HookRotation;                            // HookRotation,      default 60.0
  float HookLength;                              // HookLength,        default 0.7
  float HookWidth;                               // HookWidth,         default 0.33
  float BarWidth;                                // BarWidth,          default 0.2
  float RowSwift;                                // RowSwift,          default 0.0
  float RAffects_BarWidth;                       // RAffects_BarWidth, default 0.0
  float GAffects_HookLength;                     // GAffects_HookLength, default 0.0
  float BAffects_HookRotation;                   // BAffects_HookRotation, default 0.0
  float AmplifyIllusion;                         // AmplifyIllusion,   default 0.0
  // 24 floats = 96 bytes — 16-byte aligned, no pad needed
};

struct ZollnerPatternResolution {
  // TiXL ZollnerGrid.hlsl Resolution cbuffer (b1): TargetWidth/TargetHeight.
  float TargetWidth;
  float TargetHeight;
  float _pad[2];  // pad 8 -> 16 (16-byte multiple)
};

enum ZollnerPatternBinding {
  ZOLLNER_Params     = 0,  // constant ZollnerPatternParams& (b0)
  ZOLLNER_Resolution = 1,  // constant ZollnerPatternResolution& (b1)
  ZOLLNER_Texture    = 0,  // texture2d Image at register(t0) — may be 1×1 black dummy
  ZOLLNER_Sampler    = 0,  // sampler at register(s0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ZollnerPatternParams) == 96,
              "ZollnerPatternParams must be 96 bytes (24 floats, 16-byte aligned)");
static_assert(sizeof(ZollnerPatternResolution) == 16,
              "ZollnerPatternResolution must be 16 bytes");
#endif
