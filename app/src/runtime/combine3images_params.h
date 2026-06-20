// Shared host<->shader params for the TiXL-ported Combine3Images IMAGE FILTER (lane multi-image,
// image/use). Mirrors external/tixl Operators/Lib/Assets/shaders/img/use/img-combine-3.hlsl.
// Combine3Images samples THREE graph-wired images (ImageA @ t0, ImageB @ t1, ImageC @ t2), multiplies
// each by its tint color, then PACKS one selected channel from any of the three into each of the new
// image's R/G/B and picks the alpha from a 5-way mode. The THIRD consumer of the multi-image seam
// (Displace = 2 inputs, DistortAndShade = 2 inputs; this = 3 inputs) — the seam already gathers up to
// kMaxTexInputs=4 Texture2D ports into TexCookCtx::inputTextures[] (cookTexNode, flat + resident), so a
// 3-image leaf needs ZERO shared-graph edit.
//
// cbuffer ParamConstants (HLSL b0) field order — packed here as all-scalar floats (particle_params.h
// discipline: no Vector4 forcing host/shader layout drift). img-combine-3.hlsl b0 is:
//     float4 ImageAColor;  float4 ImageBColor;  float4 ImageCColor;
//     float Select_R;  float Select_G;  float Select_B;  float AlphaMode;
//
// .t3 PARAM ROUTING (STEP-0, the Cut-55 FloatsToBuffer connection-order rule — verified CLEAN 1:1, no
// math junctions). Combine3Images.t3's _trippleImageFxSetup FloatParams MultiInput (slot 39c7dd84) is
// fed by 16 connections whose order EXACTLY matches the cbuffer field order:
//     [0-3]  Vector4Components(ColorA).{X,Y,Z,W}   -> ImageAColor.xyzw
//     [4-7]  Vector4Components(ColorB).{X,Y,Z,W}   -> ImageBColor.xyzw
//     [8-11] Vector4Components(ColorC).{X,Y,Z,W}   -> ImageCColor.xyzw
//     [12]   IntToFloat(43825270) <- SelectChannel_R -> Select_R
//     [13]   IntToFloat(1e731012) <- SelectChannel_G -> Select_G
//     [14]   IntToFloat(1f7560ad) <- SelectChannel_B -> Select_B
//     [15]   IntToFloat(24ac575b) <- SelectAlphaChannel -> AlphaMode
// Vector4Components output slots are X=cfb58526 Y=2f8e90dd Z=162bb4fe W=e1dede5f (Vector4Components.cs);
// the .t3 wires them in exactly that X,Y,Z,W order. IntToFloat is an identity int->float cast. So every
// cbuffer float maps 1:1 to its op port — no Cut-55 mis-route. (See the static_assert below.)
//
// TiXL .t3 defaults (Combine3Images.t3): ColorA (1,0,0,1) red, ColorB (0,1,0,1) green, ColorC (0,0,1,1)
// blue; SelectChannel_R 0 (ImageA_R), SelectChannel_G 6 (ImageB_G), SelectChannel_B 12 (ImageC_B),
// SelectAlphaChannel 4 (SetToOne). ImageA/B/C default null (unwired). GenerateMips false.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct Combine3ImagesParams {
  // cbuffer ParamConstants (HLSL b0), same field order:
  float ImageAColorR, ImageAColorG, ImageAColorB, ImageAColorA;  // float4 ImageAColor, default (1,0,0,1)
  float ImageBColorR, ImageBColorG, ImageBColorB, ImageBColorA;  // float4 ImageBColor, default (0,1,0,1)
  float ImageCColorR, ImageCColorG, ImageCColorB, ImageCColorA;  // float4 ImageCColor, default (0,0,1,1)
  float Select_R;   // 0..14 enum: which channel of which image fills the new R (default 0 = ImageA_R)
  float Select_G;   // default 6 = ImageB_G
  float Select_B;   // default 12 = ImageC_B
  float AlphaMode;  // 0..4 enum: which image's alpha (or 0/1) fills the new A (default 4 = SetToOne)
};

enum Combine3ImagesBinding {
  COMBINE3IMAGES_Params = 0,  // constant Combine3ImagesParams& (b0)
  // texture(0) = ImageA, texture(1) = ImageB, texture(2) = ImageC, sampler(0) = linear; bound directly.
};

#ifndef __METAL_VERSION__
static_assert(sizeof(Combine3ImagesParams) == 64,
              "Combine3ImagesParams 64 bytes (16 floats = 3 vec4 + 4 scalars, 16-byte multiple)");
#endif
