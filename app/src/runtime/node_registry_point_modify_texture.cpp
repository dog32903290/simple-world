// runtime/node_registry_point_modify_texture — Texture2D / Curve / Gradient input point modifiers (the texture-into-points seam, TiXL point/modify).
//
// Self-registering point-modify NodeSpec leaf (split from the 852-line node_registry_point_modify.cpp,
// ARCHITECTURE rule 4 + ratchet-debt). Each spec is a PURE NodeSpec CARRIER (cook=nullptr): the real
// cook is dispatched BY TYPE NAME in the point cook driver (point_graph.cpp) + the per-op point_ops_*
// leaves. Every spec below is moved VERBATIM from the old manifest — name / ports / widgets / defaults
// / emission semantics unchanged. Adding a point-modify op here = drop a PointModifyOp registrar; the
// central manifest is never touched again (mirror of the image-filter / value-op / string-op sinks).
#include "runtime/graph.h"                      // NodeSpec, PortSpec, Widget
#include "runtime/point_modify_op_registry.h"   // PointModifyOp / pointModifySpecSink

namespace sw {
namespace {

// SamplePointColorAttributes — the FIRST Points op with a Texture2D INPUT (the texture-into-
// points seam's proving op). Ports 1:1 with SamplePointColorAttributes.cs (.t3 defaults). The
// Texture2D input port (after the Points input, matching .cs GPoints→Texture order) is gathered
// by the cook drivers' Texture2D loop into PointCookCtx::inputTextures[0]. BlendMode labels =
// SharedEnums.RgbBlendModes (Core/Utils/SharedEnums.cs). Stretch/Scale/TextureRotate (+ the
// texW/texH Aspect correction) compose transformSampleSpace; TextureMode drives the sampler wrap
// — all LIVE (the .cpp composes Scale3 host-side + the shader applies Scale·Rotate; sampler =
// Repeat+Nearest per .t3 TextureMode=Wrap / SamplerState=MinMagMipPoint).
static const PointModifyOp _reg_SamplePointColorAttributes{
      {"SamplePointColorAttributes",
       "SamplePointColorAttributes",
       {{"GPoints", "GPoints", "Points", true},        // input bag (port 0)
        {"Texture", "Texture", "Texture2D", true},     // sampled texture (port 1) — the seam input
        {"out", "out", "Points", false},               // color-blended output bag (port 2)
        // BaseColor (Vec4, TiXL default (1,1,1,1)) — multiplies the sampled texel. Read per-channel.
        {"BaseColor.r", "BaseColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"BaseColor.g", "BaseColor.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"BaseColor.b", "BaseColor.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"BaseColor.a", "BaseColor.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // BlendMode (int enum RgbBlendModes, TiXL default 0 Normal) -> shader Mode.
        {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
         {"Normal", "Screen", "Multiply", "Overlay", "Difference", "UseImageA_RGB", "UseImageB_RGB",
          "ColorDodge", "LinearDodge", "MultiplyA"}},
        // Center (Vec3, TiXL default (0,0,0)) — subtracted from position before the uv transform.
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Stretch (Vec2, TiXL default (1,1)) / Scale (Single, TiXL default 2.0) / TextureRotate (Vec3,
        // default 0) / TextureMode (TextureAddressMode enum, default Wrap) — LIVE: Stretch/Scale/
        // TextureRotate compose transformSampleSpace (see the .cpp); TextureMode drives the sampler wrap.
        {"Stretch.x", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 2.0f, 0.0f, 100.0f},
        {"TextureRotate.x", "TextureRotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"TextureRotate.y", "TextureRotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureRotate.z", "TextureRotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureMode", "TextureMode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"Wrap", "Clamp", "Mirror", "Border"}}},
       nullptr}
};

// AttributesFromImageChannels — a Points op with a Texture2D INPUT (same texture-into-points seam
// as SamplePointColorAttributes). Samples the texture per point and ROUTES the sampled
// Brightness(L)/Red/Green/Blue channels — each through a per-channel Factor/Offset gain — into a
// SELECTED point attribute (the Attributes enum: position xyz / F1 / F2 / rotate xyz / scale),
// scaled by Strength·alpha. Ports 1:1 with AttributesFromImageChannels.cs (.t3 defaults). The
// Texture2D input (after GPoints, matching .cs order) is gathered into inputTextures[0]. The
// channel-routing enums (Brightness/Red/Green/Blue) = the .cs Attributes enum; their Factor/Offset
// gains + Strength/StrengthFactor/GainAndBias drive how strongly each channel moves its target.
// Stretch/Scale/TextureRotate (+ Aspect + the .t3 UniformScale=0.5) compose transformSampleSpace.
static const PointModifyOp _reg_AttributesFromImageChannels{
      {"AttributesFromImageChannels",
       "AttributesFromImageChannels",
       {{"GPoints", "GPoints", "Points", true},        // input bag (port 0)
        {"Texture", "Texture", "Texture2D", true},     // sampled texture (port 1) — the seam input
        {"out", "out", "Points", false},               // attribute-routed output bag (port 2)
        // Channel routing enums (.cs Attributes, default 0 NotUsed) + per-channel Factor/Offset gains.
        // Attributes: 0 NotUsed,1 X,2 Y,3 Z,4 F1,5 F2,6 RotX,7 RotY,8 RotZ,9 ScaleUniform,10 SX,11 SY,12 SZ.
        {"Brightness", "Brightness", "Float", true, 0.0f, 0.0f, 12.0f, Widget::Enum,
         {"NotUsed", "X", "Y", "Z", "F1", "F2", "Rotate_X", "Rotate_Y", "Rotate_Z", "Scale_Uniform",
          "Scale_X", "Scale_Y", "Scale_Z"}},
        {"BrightnessFactor", "BrightnessFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"BrightnessOffset", "BrightnessOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Red", "Red", "Float", true, 0.0f, 0.0f, 12.0f, Widget::Enum,
         {"NotUsed", "X", "Y", "Z", "F1", "F2", "Rotate_X", "Rotate_Y", "Rotate_Z", "Scale_Uniform",
          "Scale_X", "Scale_Y", "Scale_Z"}},
        {"RedFactor", "RedFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"RedOffset", "RedOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Green", "Green", "Float", true, 0.0f, 0.0f, 12.0f, Widget::Enum,
         {"NotUsed", "X", "Y", "Z", "F1", "F2", "Rotate_X", "Rotate_Y", "Rotate_Z", "Scale_Uniform",
          "Scale_X", "Scale_Y", "Scale_Z"}},
        {"GreenFactor", "GreenFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"GreenOffset", "GreenOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Blue", "Blue", "Float", true, 0.0f, 0.0f, 12.0f, Widget::Enum,
         {"NotUsed", "X", "Y", "Z", "F1", "F2", "Rotate_X", "Rotate_Y", "Rotate_Z", "Scale_Uniform",
          "Scale_X", "Scale_Y", "Scale_Z"}},
        {"BlueFactor", "BlueFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"BlueOffset", "BlueOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        // Center (Vec3 0) — subtracted from position before the uv transform.
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Stretch (Vec2 (1,1)) / Scale (Single, TiXL default 1.0) / TextureRotate (Vec3 0) compose
        // transformSampleSpace; TextureMode (TextureAddressMode, .t3 default Clamp) drives the sampler.
        {"Stretch.x", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 100.0f},
        {"TextureRotate.x", "TextureRotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"TextureRotate.y", "TextureRotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureRotate.z", "TextureRotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"TextureMode", "TextureMode", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
         {"Wrap", "Clamp", "Mirror", "Border"}},  // .t3 default Clamp (index 1)
        // RotationSpace (.cs Spaces, default 1 Point) / TranslationSpace (default 0 Object) / Mode (.cs
        // Modes Add/Multiply, default 0 Add — DEAD in the active kernel) / Strength (1) / StrengthFactor
        // (.cs FModes None/F1/F2, default 0 None) / GainAndBias (Vec2 (0.5,0.5) = identity).
        {"RotationSpace", "RotationSpace", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
         {"Object", "Point"}},
        {"TranslationSpace", "TranslationSpace", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Object", "Point"}},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Add", "Multiply"}},
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 10.0f},
        {"StrengthFactor", "StrengthFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "F1", "F2"}},
        {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr}
};

// LinearSamplePointAttributes — a Points op with a Texture2D INPUT (same texture-into-points seam
// as SamplePointColorAttributes / AttributesFromImageChannels). Samples the texture along the point
// INDEX (uv = (i/pointCount, 0.5) — a 1D LINEAR strip, hence the name; NO position-derived uv, NO
// transformSampleSpace, NO Center) and ROUTES the sampled Brightness(L)/Red/Green/Blue channels —
// each through a per-channel Factor/Offset gain — into a SELECTED point attribute (position xyz /
// F1 / rotate xyz / stretch xyz / F2), blended by Strength. Ports 1:1 with
// LinearSamplePointAttributes.cs [Input] order (.t3 defaults). The Texture2D input (after GPoints,
// matching .cs order) is gathered into inputTextures[0]. The channel-routing enums = the .cs
// Attributes enum (DIFFERENT from AttributesFromImageChannels: here F1=4, Rot=5/6/7, Stretch=8/9/10,
// F2=11). See linearsamplepointattributes_params.h / .metal. NO .t3 FloatsToBuffer routing trap
// (the .hlsl's two scalar cbuffers map 1:1 to the .cs ports — no matrix slot).
static const PointModifyOp _reg_LinearSamplePointAttributes{
      {"LinearSamplePointAttributes",
       "LinearSamplePointAttributes",
       {{"GPoints", "GPoints", "Points", true},        // input bag (port 0)
        {"Texture", "Texture", "Texture2D", true},     // sampled texture (port 1) — the seam input
        {"out", "out", "Points", false},               // attribute-routed output bag (port 2)
        // Channel routing enums (.cs Attributes, default 0 NotUsed) + per-channel Factor/Offset gains.
        // Attributes: 0 NotUsed,1 For_X,2 For_Y,3 For_Z,4 For_F1,5 Rotate_X,6 Rotate_Y,7 Rotate_Z,
        //             8 Stretch_X,9 Stretch_Y,10 Stretch_Z,11 For_F2.
        {"Brightness", "Brightness", "Float", true, 0.0f, 0.0f, 11.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_F1", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z", "For_F2"}},
        {"BrightnessFactor", "BrightnessFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"BrightnessOffset", "BrightnessOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Red", "Red", "Float", true, 0.0f, 0.0f, 11.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_F1", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z", "For_F2"}},
        {"RedFactor", "RedFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"RedOffset", "RedOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Green", "Green", "Float", true, 0.0f, 0.0f, 11.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_F1", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z", "For_F2"}},
        {"GreenFactor", "GreenFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"GreenOffset", "GreenOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Blue", "Blue", "Float", true, 0.0f, 0.0f, 11.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_F1", "Rotate_X", "Rotate_Y", "Rotate_Z",
          "Stretch_X", "Stretch_Y", "Stretch_Z", "For_F2"}},
        {"BlueFactor", "BlueFactor", "Float", true, 0.0f, -100.0f, 100.0f},
        {"BlueOffset", "BlueOffset", "Float", true, 0.0f, -100.0f, 100.0f},
        // Mode (.cs Modes Add/Multiply, default 0 Add) / TranslationSpace (.cs Spaces, default 0 Object)
        // / RotationSpace (.cs Spaces, default 1 Point) / Strength (1.0) / StrengthFactor (.cs FModes,
        // default 0 None). (.t3 defaults — verified GUID-keyed.)
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Add", "Multiply"}},
        {"TranslationSpace", "TranslationSpace", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Object", "Point"}},
        {"RotationSpace", "RotationSpace", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
         {"Object", "Point"}},
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 1.0f},
        {"StrengthFactor", "StrengthFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "F1", "F2"}}},
       nullptr}
};

// ---- batch sw-node-batch: point modify — MapPointAttributes (bake-into-point seam) ----------
// TiXL parity: external/tixl .../point/modify/MapPointAttributes.{cs,hlsl,t3}. A count-preserving
// MODIFIER that BAKES its host Curve (→ R32 CurveImage) + host Gradient (→ RGBA32 GradientImage,
// .t3 resolution 512) into scratch textures during cook, then per point maps an input coordinate
// (InputMode → f0 → MappingMode remap with Range/Phase) and samples both at (f,0.5) with a Clamp/
// Linear sampler to write a curve value into FX1/FX2/Scale (WriteTo) + a gradient color into Color
// (WriteColor, default Multiply). The .t3 compound bakes the host inputs via CurvesToTexture /
// GradientsToTexture + FirstValidTexture (ValueTexture OVERRIDES the baked curve) — the Curve +
// Gradient + ValueTexture INPUT ports are gathered by the cook drivers into PointCookCtx::inputCurves
// /inputGradients (Curve/Gradient) + inputTextures[0] (ValueTexture). No Curve/Gradient producer op
// exists yet, so in production these are UNWIRED → the op bakes its embedded .t3 defaults (flat-1.0
// curve, white→white gradient). Ports 1:1 with MapPointAttributes.cs [Input] order (.t3 defaults).
static const PointModifyOp _reg_MapPointAttributes{
      {"MapPointAttributes",
       "MapPointAttributes",
       {{"Points", "Points", "Points", true},          // input bag (port 0)
        {"out", "out", "Points", false},                // mapped output bag (port 1)
        // InputMode (.cs InputModes, default 0 BufferOrder) — the f0 source per point.
        {"InputMode", "InputMode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"BufferOrder", "F1", "F2", "Random"}},
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 1.0f},  // .t3 default 1.0
        {"StrengthFactor", "StrengthFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "F1", "F2"}},
        // Mapping (.cs MappingModes, default 0 Centered) — remaps f0 → f (UseOriginalW is dead in .hlsl).
        {"Mapping", "Mapping", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"Centered", "ForStart", "PingPong", "Repeat", "UseOriginalW"}},
        {"Range", "Range", "Float", true, 1.0f, -10.0f, 10.0f},   // .t3 default 1.0
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},   // .t3 default 0.0
        // WriteColor (.cs WriteColorModes, .t3 DEFAULT 2 Multiply) — how the gradient color writes Color.
        {"WriteColor", "WriteColor", "Float", true, 2.0f, 0.0f, 2.0f, Widget::Enum,
         {"None", "Replace", "Multiply"}},
        // Gradient (host value input, .t3 white→white) — baked into GradientImage @t2. Gathered into
        // PointCookCtx::inputGradients; unwired → embedded white default (Multiply identity).
        {"Gradient", "Gradient", "Gradient", true},
        // WriteTo (.cs WriteToModes, default 0 None) — which attribute the curve value writes.
        {"WriteTo", "WriteTo", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"None", "F1", "F2", "Scale"}},
        // WriteMode (.cs WriteModes, default 0 Replace) → shader ApplyMode (Replace/Multiply/Add).
        {"WriteMode", "WriteMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"Replace", "Multiply", "Add"}},
        // MappingCurve (host value input, .t3 flat-1.0 LINEAR line) — baked into CurveImage @t1. Gathered
        // into PointCookCtx::inputCurves (no producer yet → embedded flat-1.0 default).
        {"MappingCurve", "MappingCurve", "Curve", true},
        // ValueTexture (Texture2D input, .t3 null) — OVERRIDES the baked CurveImage when wired
        // (FirstValidTexture). Gathered into inputTextures[0].
        {"ValueTexture", "ValueTexture", "Texture2D", true}},
       nullptr}
};

}  // namespace
}  // namespace sw
