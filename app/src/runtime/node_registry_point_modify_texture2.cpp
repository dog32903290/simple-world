// runtime/node_registry_point_modify_texture2 — Texture2D input point modifiers, batch lane-b14 (split
// from node_registry_point_modify_texture.cpp to keep that file ≤400 lines, ARCHITECTURE rule 4 + ratchet).
//
// Self-registering point-modify NodeSpec leaf (same texture-into-points seam as the _texture file: each
// op's Texture2D input port is gathered by the cook drivers into PointCookCtx::inputTextures[0]). Each spec
// is a PURE NodeSpec CARRIER (cook=nullptr): the real cook is dispatched BY TYPE NAME in the point cook
// driver + the per-op point_ops_* leaves (point_ops_samplepointattributes.cpp / _displacepoints2d.cpp /
// _transformwithimage.cpp). Adding a point-modify op here = drop a PointModifyOp registrar; the central
// manifest is never touched.
#include "runtime/graph.h"                      // NodeSpec, PortSpec, Widget
#include "runtime/point_modify_op_registry.h"   // PointModifyOp / pointModifySpecSink

namespace sw {
namespace {

// ---- texture-into-points seam: SamplePointAttributes_v1 ------------------------------------------
// TiXL parity: external/tixl .../point/modify/SamplePointAttributes_v1.{cs,t3} +
// .../Assets/shaders/points/modify/SamplePointAttributes.hlsl. A count-preserving MODIFIER (SAME seam
// as SamplePointColorAttributes): samples the Texture2D per point (via transformSampleSpace) and ROUTES
// the sampled L/R/G/B channels — each through a routing Attributes enum + per-channel Factor/Offset —
// into Position xyz / W(FX1) / Rotation xyz / Stretch(Scale) xyz. The Texture2D input (after GPoints,
// matching .cs order) is gathered into inputTextures[0]. Stretch/Scale/TextureRotate (+ Aspect) compose
// transformSampleSpace; TextureMode drives the sampler wrap. Ports 1:1 with SamplePointAttributes_v1.cs
// [Input] order (.t3 defaults: Scale=1.0, RotationSpace=1 Point, TranslationSpace=0 Object, Mode=0 Add,
// all channel enums 0 NotUsed, all Factor/Offset 0). fork-alpha-dead-in-kernel: the Alpha/AlphaFactor/
// AlphaOffset ports are carried for 1:1 .cs parity but route NOTHING (commented out in the .hlsl).
static const PointModifyOp _reg_SamplePointAttributes{
      {"SamplePointAttributes",
       "SamplePointAttributes",
       {{"GPoints", "GPoints", "Points", true},        // input bag (port 0)
        {"Texture", "Texture", "Texture2D", true},     // sampled texture (port 1) — the seam input
        {"out", "out", "Points", false},               // attribute-routed output bag (port 2)
        // Channel routing enums (.cs Attributes, default 0 NotUsed) + per-channel Factor/Offset.
        // Attributes: 0 NotUsed,1 For_X,2 For_Y,3 For_Z,4 For_W,5 Rotate_X,6 Rotate_Y,7 Rotate_Z,
        //             8 Stretch_X,9 Stretch_Y,10 Stretch_Z.
        {"Brightness", "Brightness", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z"}},
        {"BrightnessFactor", "BrightnessFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"BrightnessOffset", "BrightnessOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Red", "Red", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z"}},
        {"RedFactor", "RedFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"RedOffset", "RedOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Green", "Green", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z"}},
        {"GreenFactor", "GreenFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"GreenOffset", "GreenOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Blue", "Blue", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z"}},
        {"BlueFactor", "BlueFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"BlueOffset", "BlueOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        // Alpha (.cs ports, DEAD in kernel — fork-alpha-dead-in-kernel). Carried for 1:1 .cs parity.
        {"Alpha", "Alpha", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z"}},
        {"AlphaFactor", "AlphaFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"AlphaOffset", "AlphaOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        // Center (Vec3 0) subtracted before the uv transform.
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Stretch (Vec2 (1,1)) / Scale (Single, .t3 default 1.0) / TextureRotate (Vec3 0) compose
        // transformSampleSpace; TextureMode (.t3 default Wrap) drives the sampler.
        {"Stretch.x", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 100.0f},
        {"TextureRotate.x", "TextureRotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"TextureRotate.y", "TextureRotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureRotate.z", "TextureRotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureMode", "TextureMode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"Wrap", "Clamp", "Mirror", "Border"}},
        // Mode (.cs Modes Add/Multiply, default 0 Add) / RotationSpace (.cs Spaces, default 1 Point) /
        // TranslationSpace (.cs Spaces, default 0 Object).
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Add", "Multiply"}},
        {"RotationSpace", "RotationSpace", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
         {"Object", "Point"}},
        {"TranslationSpace", "TranslationSpace", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Object", "Point"}}},
       nullptr,
       "point.modify"}
};

// ---- texture-into-points seam: DisplacePoints2d --------------------------------------------------
// TiXL parity: external/tixl .../Assets/shaders/points/modify/DisplacePoints2d.hlsl, driven by the op
// SimDisplacePoints2d.{cs,t3} (a single-pass in-place MODIFIER despite the "Sim" name — its .t3 is one
// ComputeShader+Dispatch, NO feedback state). A count-preserving MODIFIER: samples a DisplaceMap, takes the
// central-difference GRADIENT of the gray map (±SampleRadius), and shifts Position.xy along the gradient
// angle by DisplaceAmount/100. The Texture2D input (after GPoints) is gathered into inputTextures[0].
// Center/TextureRotate/TextureScale compose the op-local sample-space transform (the shader applies its
// inverse — fork-worldtoobject-op-local, NO camera). Ports 1:1 with SimDisplacePoints2d.cs [Input] order.
// .t3 defaults: DisplaceAmount=0, DisplaceOffset=0 (DEAD), Twist=0, SampleRadius=0, Center=0, TextureScale=
// (1,1), TextureRotate=0, TextureMode=Wrap (the op is a no-op at default — DisplaceAmount=0 AND radius=0).
static const PointModifyOp _reg_DisplacePoints2d{
      {"DisplacePoints2d",
       "DisplacePoints2d",
       {{"GPoints", "GPoints", "Points", true},        // input bag (port 0)
        {"Texture", "Texture", "Texture2D", true},     // DisplaceMap (port 1) — the seam input
        {"out", "out", "Points", false},               // displaced output bag (port 2)
        {"DisplaceAmount", "DisplaceAmount", "Float", true, 0.0f, -100.0f, 100.0f},
        {"DisplaceOffset", "DisplaceOffset", "Float", true, 0.0f, -10.0f, 10.0f},  // DEAD in kernel
        {"Twist", "Twist", "Float", true, 0.0f, -360.0f, 360.0f},
        {"SampleRadius", "SampleRadius", "Float", true, 0.0f, 0.0f, 100.0f},
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"TextureScale.x", "TextureScale", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"TextureScale.y", "TextureScale.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"TextureRotate.x", "TextureRotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"TextureRotate.y", "TextureRotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureRotate.z", "TextureRotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureMode", "TextureMode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"Wrap", "Clamp", "Mirror", "Border"}}},
       nullptr,
       "point.modify"}
};

// ---- texture-into-points seam: TransformWithImage -----------------------------------------------
// TiXL parity: external/tixl .../point/modify/TransformWithImage.{cs,t3} +
// .../Assets/shaders/points/modify/TranslateWithImage.hlsl (op GUID TransformWithImage; shader file
// TranslateWithImage). A count-preserving MODIFIER: samples an Image to derive a per-point strength =
// Strength·(ApplyGainAndBias(gray+scatter)+StrengthOffset)·(StrengthFactor channel), then applies a host-
// composed TRS TransformMatrix (Translate/Scale/ScaleUniform/Rotate) lerp-blended by that strength. The
// Texture2D input (after GPoints) is gathered into inputTextures[0]. Stretch/ImageScale/TextureRotate
// compose transformSampleSpace (uv); NO camera. Ports 1:1 with TransformWithImage.cs [Input] order.
// .t3 defaults: Strength=1, StrengthFactor=0 (None), Translate=0, Scale=(1,1,1), ScaleUniform=1, Rotate=0,
// Center=0, Stretch=(1,1), ImageScale=1, TextureRotate=0, TextureMode=Clamp, TranslationSpace=1 (Object),
// GainAndBias=(0.5,0.5), Scatter=0, StrengthOffset=0, Channel=0, ScaleFx1=1, ScaleFx2=1.
// fork-channel-scalefx-dead: Channel/ScaleFx1/ScaleFx2 carried for 1:1 .cs parity but unused in the kernel.
static const PointModifyOp _reg_TransformWithImage{
      {"TransformWithImage",
       "TransformWithImage",
       {{"GPoints", "GPoints", "Points", true},        // input bag (port 0)
        {"Texture", "Texture", "Texture2D", true},     // Image (port 1) — the seam input
        {"out", "out", "Points", false},               // transformed output bag (port 2)
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 1.0f},
        {"StrengthFactor", "StrengthFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "F1", "F2"}},
        {"Translate.x", "Translate", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Translate.y", "Translate.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Translate.z", "Translate.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Scale.x", "Scale", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Scale.y", "Scale.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Scale.z", "Scale.z", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"ScaleUniform", "ScaleUniform", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Rotate.x", "Rotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotate.y", "Rotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotate.z", "Rotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"ScaleFx1", "ScaleFx1", "Float", true, 1.0f, -10.0f, 10.0f},   // DEAD in kernel
        {"ScaleFx2", "ScaleFx2", "Float", true, 1.0f, -10.0f, 10.0f},   // DEAD in kernel
        {"Channel", "Channel", "Float", true, 0.0f, 0.0f, 3.0f},        // DEAD in kernel
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Stretch.x", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"ImageScale", "ImageScale", "Float", true, 1.0f, 0.0f, 100.0f},
        {"TextureRotate.x", "TextureRotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"TextureRotate.y", "TextureRotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureRotate.z", "TextureRotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureMode", "TextureMode", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
         {"Wrap", "Clamp", "Mirror", "Border"}},  // .t3 default Clamp (index 1)
        // TranslationSpace (.cs Spaces): Point=0, Object=1. .t3 default 1 (Object).
        {"TranslationSpace", "TranslationSpace", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
         {"Point", "Object"}},
        {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Scatter", "Scatter", "Float", true, 0.0f, 0.0f, 10.0f},
        {"StrengthOffset", "StrengthOffset", "Float", true, 0.0f, -10.0f, 10.0f}},
       nullptr,
       "point.modify"}
};

}  // namespace
}  // namespace sw
