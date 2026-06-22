// runtime/node_registry_point_modify_noise — Randomize / noise-displacement point modifiers (TiXL point/modify).
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

static const PointModifyOp _reg_RandomizePoints{
      {"RandomizePoints",
       "RandomizePoints",
       {{"points", "points", "Points", true},   // input bag (port 0)
        {"out", "out", "Points", false},         // jittered output bag (port 1)
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 1.0f},
        {"StrengthFactor", "StrengthFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"None", "F1", "F2"}},
        // Position / Rotation(Euler°) / Stretch / ColorHSB / GainAndBias — TiXL vector inputs (Widget::Vec).
        {"Position.x", "Position", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"F1", "F1", "Float", true, 0.0f, -10.0f, 10.0f},
        {"F2", "F2", "Float", true, 0.0f, -10.0f, 10.0f},
        {"ColorHSB.x", "ColorHSB", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ColorHSB.y", "ColorHSB.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ColorHSB.z", "ColorHSB.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ColorHSB.w", "ColorHSB.w", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Stretch.x", "Stretch", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Stretch.y", "Stretch.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Stretch.z", "Stretch.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"RandomPhase", "RandomPhase", "Float", true, 0.0f, 0.0f, 100.0f},
        {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OffsetMode", "OffsetMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Add", "Scatter"}},
        {"Space", "Space", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Point", "Object"}},
        {"Interpolation", "Interpolation", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum, {"None", "Linear", "Smooth"}},
        {"Repeat", "Repeat", "Float", true, 0.0f, 0.0f, 100000.0f},
        {"ClampColorsEtc", "ClampColorsEtc", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr}
};

// ---- batch 15: point modify — AddNoise ----------------------------------------
// TiXL parity: external/tixl .../point/modify/AddNoise.cs + AddNoise.hlsl
// A count-preserving MODIFIER: displaces Position by snoiseVec3 field and updates
// Rotation to follow the displaced tangent frame (RotationLookupDistance probe).
// Defaults from AddNoise.t3 (GUID-keyed):
//   Strength=1.0, StrengthFactor=None, Frequency=1.0, Phase=0.0, Variation=0.0,
//   AmountDistribution=(1,1,1), RotationLookupDistance=0.25, NoiseOffset=(0,0,0)
static const PointModifyOp _reg_AddNoise{
      {"AddNoise",
       "AddNoise",
       {{"points", "points", "Points", true},   // input bag (port 0)
        {"out", "out", "Points", false},         // displaced output bag (port 1)
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 5.0f},
        {"StrengthFactor", "StrengthFactor", "Float", true, 0.0f, 0.0f, 2.0f,
         Widget::Enum, {"None", "F1", "F2"}},
        {"Frequency", "Frequency", "Float", true, 1.0f, 0.0f, 20.0f},
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Variation", "Variation", "Float", true, 0.0f, 0.0f, 1.0f},
        {"AmountDistribution.x", "AmountDistribution", "Float", true, 1.0f, 0.0f, 2.0f,
         Widget::Vec, {}, true, 3},
        {"AmountDistribution.y", "AmountDistribution.y", "Float", true, 1.0f, 0.0f, 2.0f,
         Widget::Vec, {}, true, 1},
        {"AmountDistribution.z", "AmountDistribution.z", "Float", true, 1.0f, 0.0f, 2.0f,
         Widget::Vec, {}, true, 1},
        {"RotationLookupDistance", "RotationLookupDistance", "Float", true, 0.25f, 0.0f, 2.0f},
        {"NoiseOffset.x", "NoiseOffset", "Float", true, 0.0f, -10.0f, 10.0f,
         Widget::Vec, {}, true, 3},
        {"NoiseOffset.y", "NoiseOffset.y", "Float", true, 0.0f, -10.0f, 10.0f,
         Widget::Vec, {}, true, 1},
        {"NoiseOffset.z", "NoiseOffset.z", "Float", true, 0.0f, -10.0f, 10.0f,
         Widget::Vec, {}, true, 1}},
       nullptr}
};

// ---- batch 24 (lane point_modify): PointAttributeFromNoise ------------------
// TiXL parity: external/tixl .../point/modify/PointAttributeFromNoise.cs +
//              .../Assets/shaders/points/modify/PointAttributesFromNoise.hlsl
// A count-preserving MODIFIER: samples a 3D simplex-noise field per point and routes it into
// chosen attributes via 4 channels (Brightness/L, Red, Green, Blue), each with its own
// attribute target (Attributes enum) + Factor + Offset. enum: NotUsed=0, For_X/Y/Z/W=1/2/3/4,
// Rotate_X/Y/Z=5/6/7. Position += routed channels; rotation accumulates X->Y->Z (qMul order).
// Defaults: all selectors NotUsed(0), Factors=1, Offsets=0, Center=(0,0,0), Phase=0,
//   Frequency=1, Amount=1, Variation=0.
// FORK (named): TiXL's optional RemapNoise(Gradient)/UseRemapCurve/remapCurveTexture branch is
//   NOT wired (work order); UseRemapCurve baked false -> always `c *= Amount/100`. No such ports.
static const PointModifyOp _reg_PointAttributeFromNoise{
      {"PointAttributeFromNoise",
       "PointAttributeFromNoise",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // noise-driven output bag (port 1)
        // .cs Brightness/Red/Green/Blue = int attribute enum (MappedType=Attributes); each + Factor + Offset.
        {"Brightness", "Brightness", "Float", true, 0.0f, 0.0f, 7.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z"}},
        {"BrightnessFactor", "BrightnessFactor", "Float", true, 1.0f, -10.0f, 10.0f},
        {"BrightnessOffset", "BrightnessOffset", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Red", "Red", "Float", true, 0.0f, 0.0f, 7.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z"}},
        {"RedFactor", "RedFactor", "Float", true, 1.0f, -10.0f, 10.0f},
        {"RedOffset", "RedOffset", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Green", "Green", "Float", true, 0.0f, 0.0f, 7.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z"}},
        {"GreenFactor", "GreenFactor", "Float", true, 1.0f, -10.0f, 10.0f},
        {"GreenOffset", "GreenOffset", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Blue", "Blue", "Float", true, 0.0f, 0.0f, 7.0f, Widget::Enum,
         {"NotUsed", "For_X", "For_Y", "For_Z", "For_W", "Rotate_X", "Rotate_Y", "Rotate_Z"}},
        {"BlueFactor", "BlueFactor", "Float", true, 1.0f, -10.0f, 10.0f},
        {"BlueOffset", "BlueOffset", "Float", true, 0.0f, -10.0f, 10.0f},
        // Center (TiXL Vector3) — added to position before the noise lookup (.hlsl line 91).
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Frequency", "Frequency", "Float", true, 1.0f, 0.0f, 20.0f},
        {"Amount", "Amount", "Float", true, 1.0f, -200.0f, 200.0f},
        {"Variation", "Variation", "Float", true, 0.0f, 0.0f, 100.0f}},
       nullptr}
};

}  // namespace
}  // namespace sw
