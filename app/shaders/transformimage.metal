// TransformImage: TiXL-ported offset/stretch/scale/rotation image transform, single pass.
// Faithful line-by-line port of external/tixl Operators/Lib/Assets/shaders/img/fx/
// TransformImage.hlsl (self-contained — no shared include).
//
// ============================== LOAD-BEARING PARITY NOTES (named) ==============================
// [fork-getdimensions] HLSL `ImageA.GetDimensions(height, width)` — HLSL's GetDimensions writes
//   (width, height) into its out params IN THAT ORDER, but TiXL named the FIRST out param `height`
//   and the SECOND `width`. So the variable `height` actually holds the texture WIDTH and the
//   variable `width` holds the texture HEIGHT, and `aspect2 = width/height` = texH/texW. We port
//   this verbatim (variable names + the cross-wired assignment) so aspect2 matches TiXL bit-for-bit.
//   (For a square source aspect2 == 1, which the golden uses.)
// [fork-b2-source-aspect] sourceAspectRatio = OrgResolution.x / OrgResolution.y, read from the
//   IntParams cbuffer (TiXL b2, fed GetTextureSize(Image) i.e. the SOURCE texture size). The cook
//   feeds inputTexture->width()/height() into OrgResolution.
// [fork-offset-xneg]  offset = Offset * float2(-1, 1)  — X is NEGATED (TiXL: "translation on X will
//   match the user's movement"). Ported verbatim.
// [fork-rotation-sign] imageRotationRad = (-Rotation - 90)/180 * 3.141578; then
//   sina/cosa = sin/cos(-imageRotationRad - 3.141578/2). The TiXL idiom uses its literal 3.141578
//   (NOT a more-precise pi) — ported verbatim (matters for parity at the float level).
// [fork-repeatmode] RepeatMode cbuffer field is declared but the active path IGNORES it (the
//   `(RepeatMode>3.5)?...` line is commented out in the .hlsl). Wrapping comes from the SAMPLER
//   (TiXL t3 default TextureAddressMode=Wrap, Filter=MinMagMipLinear) — set in the cook, not here.
#include <metal_stdlib>
#include "transformimage_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
// Same convention as convertcolors_vs / tint_vs across the image filters.
vertex VSOut transformimage_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// HLSL float mod(x,y) = x - y*floor(x/y). Declared in the .hlsl but unused by the active psMain
// path; omitted here (no behavior change).

// Mirror of TransformImage.hlsl psMain (lines 43-83), line for line.
fragment float4 transformimage_fs(VSOut psInput [[stage_in]],
                                  texture2d<float> ImageA           [[texture(0)]],
                                  sampler texSampler                [[sampler(0)]],
                                  constant TransformImageParams& P  [[buffer(TI_Params)]]) {
  // [fork-getdimensions] HLSL: ImageA.GetDimensions(height, width);  -> height=texW, width=texH.
  float height = (float)ImageA.get_width();
  float width  = (float)ImageA.get_height();
  float2 aspect2 = width / height;                 // = texH / texW (TiXL verbatim)

  float2 uv = psInput.texCoord;                    // (declared in .hlsl; unused below, kept for parity)

  // [fork-b2-source-aspect]
  float sourceAspectRatio = (float)P.OrgResolutionX / (float)P.OrgResolutionY;

  float2 divisions = float2(sourceAspectRatio / P.StretchX, 1.0f / P.StretchY) / P.Scale;
  float2 p = psInput.texCoord;
  float2 offset = float2(P.OffsetX, P.OffsetY) * float2(-1.0f, 1.0f);  // [fork-offset-xneg]
  p += offset;
  p -= 0.5f;

  // Rotate — [fork-rotation-sign] (TiXL literal 3.141578)
  float imageRotationRad = (-P.Rotation - 90.0f) / 180.0f * 3.141578f;

  float sina = sin(-imageRotationRad - 3.141578f / 2.0f);
  float cosa = cos(-imageRotationRad - 3.141578f / 2.0f);

  p.x *= sourceAspectRatio;

  p = float2(
      cosa * p.x - sina * p.y,
      cosa * p.y + sina * p.x);

  p.x *= aspect2.x / sourceAspectRatio;            // aspect2 is float2 in .hlsl; .x == .y, use .x
  p *= divisions;

  // [fork-repeatmode] samplePos always = p + 0.5 (the RepeatMode branch is commented out in TiXL).
  float2 samplePos = (p + 0.5f);

  float4 imgColorForCel = ImageA.sample(texSampler, samplePos);
  return imgColorForCel;
}
