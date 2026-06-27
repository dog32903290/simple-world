// Shared host<->shader params for the TiXL-ported DisplacePoints2d — a texture-into-points seam consumer.
// Mirrors external/tixl .../Assets/shaders/points/modify/DisplacePoints2d.hlsl. The TiXL kernel binds TWO
// cbuffers: a `Transforms` cbuffer (10 camera/object matrices, of which it reads ONLY WorldToObject) and a
// `Params` cbuffer (DisplaceAmount/DisplaceOffset/Twist/SampleRadius/Center). The TiXL op (SimDisplacePoints2d
// .t3) feeds WorldToObject from an OP-LOCAL Transform (Translation=Center, Rotation=TextureRotate,
// Scale=TextureScale·z=1) pushed onto the context — NO camera knob exists on the op. So we DON'T port the
// 10-matrix Transforms cbuffer: we compose the op-local sample-space transform host-side (Scale3 + Euler,
// SAME discipline as samplepointcolorattributes_params.h) and the shader applies its INVERSE
// (WorldToObject = (R·S)^-1) — fork-worldtoobject-op-local (no camera; the .t3 has no camera input).
//
//   M3 (ObjectToWorld 3x3) = R · diag(Scale3),  R = PitchYawRoll(TextureRotate) (Y·X·Z Euler order)
//   Scale3 = (TextureScale.x·Aspect, TextureScale.y, 1)   [Aspect = mapW/mapH; .t3 has no Scale uniform]
//   posInObject = mul(float4(pos,0), WorldToObject).xyz == qRotateVec3(pos, conj(R)) / Scale3
//                 (w=0 drops translation; Center is applied separately by `pos -= Center`).
//
// The shader then takes the CENTRAL-DIFFERENCE gradient of the gray DisplaceMap at ±SampleRadius/mapDim,
// turns it into an angle (atan2(d.x,d.y)+Twist), and shifts Position.xy by direction·DisplaceAmount/100.
// DisplaceOffset is read by the .cs but UNUSED in the kernel (dead — carried for parity).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — DisplacePoints2d (16-byte rows). Count is OUR addition (the TiXL kernel reads no count
// guard — it dispatches exactly pointCount threads via CalcDispatchCount; we guard tid>=Count like every
// other ported point op). Scale3 (Aspect folded host-side) + the TextureRotate Euler compose the op-local
// transform; the shader applies its inverse.
struct DisplaceParams2d {
#ifdef __METAL_VERSION__
  uint  Count;
#else
  uint32_t Count;
#endif
  float CenterX, CenterY, CenterZ;            // -> 16. Center (Vector3), subtracted from position
  float ScaleX, ScaleY, ScaleZ, _padScale;    // -> 16. composed Scale3 (Aspect folded) + pad
  float RotX, RotY, RotZ, _padRot;            // -> 16. TextureRotate Euler degrees (Y·X·Z) + pad
  float DisplaceAmount, Twist, SampleRadius, _padTail;  // -> 16. amount(/100) + twist(deg) + radius(px)
};

enum DisplaceBinding2d {
  DISP2D_SourcePoints = 0,  // const device SwPoint* (t0) — the op reads then writes the SAME bag in TiXL
  DISP2D_ResultPoints = 1,  // device SwPoint*       (u0)
  DISP2D_Params       = 2,  // constant DisplaceParams2d& (b0)
};
enum DisplaceTexBinding2d {
  DISP2D_DisplaceMap = 0,   // Texture2D<float4> DisplaceMap (t1; texture(0) in MSL)
};
enum DisplaceSamplerBinding2d {
  DISP2D_TexSampler = 0,    // sampler texSampler (s0)
};

#ifndef __METAL_VERSION__
// 16 (Count+Center) + 16 (Scale3+pad) + 16 (Rot+pad) + 16 (Amount/Twist/Radius+pad) = 64 bytes.
static_assert(sizeof(DisplaceParams2d) == 64, "DisplaceParams2d must be 64 bytes (4x16)");
#endif
