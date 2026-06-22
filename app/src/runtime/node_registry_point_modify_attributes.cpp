// runtime/node_registry_point_modify_attributes — Attribute set / select / soft-transform / clear point modifiers (TiXL point/modify + point/transform).
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

static const PointModifyOp _reg_SetPointAttributes{
      {"SetPointAttributes",
       "SetPointAttributes",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // modified output bag (port 1)
        // strength controls — Amount (Single) * factor(AmountFactor: None/F1/F2).
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 1.0f},
        {"AmountFactor", "AmountFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"None", "F1", "F2"}},
        // each attribute: a Set* gate (Widget::Bool) + its target value.
        {"SetPosition", "SetPosition", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Position.x", "Position", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"SetRotation", "SetRotation", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"RotationAxis.x", "RotationAxis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"RotationAxis.y", "RotationAxis.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"RotationAxis.z", "RotationAxis.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"RotationAngle", "RotationAngle", "Float", true, 0.0f, -360.0f, 360.0f},
        {"SetStretch", "SetStretch", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Stretch.x", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Stretch.z", "Stretch.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"SetFx1", "SetFx1", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Fx1", "Fx1", "Float", true, 0.0f, 0.0f, 1.0f},
        {"SetFx2", "SetFx2", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Fx2", "Fx2", "Float", true, 0.0f, 0.0f, 1.0f},
        {"SetColor", "SetColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr}
};

// ---- batch 21 (lane point_modify): SelectPoints ----------------------------
// TiXL parity: external/tixl .../point/modify/SelectPoints.cs + .hlsl
// A count-preserving MODIFIER: computes a per-point volume-selection scalar (Sphere/Box/
// Plane/Zebra/Noise) shaped by FallOff + GainAndBias, combined with the existing FX1/FX2
// weight by SelectMode (Override/Add/Sub/Multiply/Invert), and writes the result into FX1
// or FX2 (WriteTo). Position is untouched. Defaults from SelectPoints.t3:
//   Strength=1, StrengthFactor=None, WriteTo=F1, Mode=Override, ClampResult=false,
//   VolumeShape=Sphere, VolumeCenter=(0,0,0), VolumeStretch=(1,1,1), VolumeScale=1,
//   VolumeRotate=(0,0,0), FallOff=0, GainAndBias=(0.5,0.5), Scatter=0, Phase=0,
//   Threshold=0, DiscardNonSelected=false.
// FORK (selectpoints.metal):
//   - TransformVolume (float4x4) composed IN-shader from VolumeCenter/Stretch/Scale/Rotate
//     (same fork as TransformSomePoints; SelectPoints has no Pivot/Shear ports).
//   - SetW/Visibility [Input]s are DEAD in the .hlsl (commented out) -> dropped.
//   - WriteTo enum values F1/F2 only (Override=0 → write w=1 path); WriteTo=0 (None) writes
//     nothing (TiXL switch has no case 0), matching the .hlsl switch.
static const PointModifyOp _reg_SelectPoints{
      {"SelectPoints",
       "SelectPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // FX1/FX2-written output bag (port 1)
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 1.0f},
        {"StrengthFactor", "StrengthFactor", "Float", true, 0.0f, 0.0f, 2.0f,
         Widget::Enum, {"None", "F1", "F2"}},
        {"WriteTo", "WriteTo", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum, {"None", "F1", "F2"}},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"Override", "Add", "Sub", "Multiply", "Invert"}},
        {"ClampResult", "ClampResult", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"VolumeShape", "VolumeShape", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"Sphere", "Box", "Plane", "Zebra", "Noise"}},
        // VolumeCenter (TiXL Vector3, ITransformable TranslationInput) — volume position.
        {"VolumeCenter.x", "VolumeCenter", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"VolumeCenter.y", "VolumeCenter.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"VolumeCenter.z", "VolumeCenter.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // VolumeStretch (TiXL Vector3, default 1,1,1) — per-axis volume scale.
        {"VolumeStretch.x", "VolumeStretch", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"VolumeStretch.y", "VolumeStretch.y", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"VolumeStretch.z", "VolumeStretch.z", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"VolumeScale", "VolumeScale", "Float", true, 1.0f, 0.0f, 10.0f},
        // VolumeRotate (TiXL Vector3, Euler°, ITransformable RotationInput) — volume orientation.
        {"VolumeRotate.x", "VolumeRotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"VolumeRotate.y", "VolumeRotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"VolumeRotate.z", "VolumeRotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        // FallOff default 0.5 per SelectPoints.t3 (NOT 0).
        {"FallOff", "FallOff", "Float", true, 0.5f, 0.0f, 5.0f},
        // GainAndBias (TiXL Vector2, default 0.5,0.5) — shapes the selection ramp.
        {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Scatter", "Scatter", "Float", true, 0.0f, 0.0f, 1.0f},
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, -1.0f, 1.0f},
        {"DiscardNonSelected", "DiscardNonSelected", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr}
};

// ---- batch 21 (lane point_modify): SoftTransformPoints ---------------------
// TiXL parity: external/tixl .../point/transform/SoftTransformPoints.cs + .hlsl
// A count-preserving MODIFIER: computes a volume-falloff weight (Sphere/Box/Plane/Zebra,
// smoothstep) shaped by GainAndBias × Strength × StrengthFactor, then SOFT-applies a
// Translate/Rotate/Scale transform to Position (lerp by the weight), composes Rotation by
// the X-axis rotation, and lerps FX1 by ScaleFx1/OffsetFx1. Defaults from SoftTransformPoints.t3:
//   Amount=1 (note: not a shader cbuffer field — see FORK), Translate=(0,0,0), Dither=0,
//   Stretch=(1,1,1), Scale=1, Rotate=(0,0,0), ScaleW=1, OffsetW=0, VolumeCenter=(0,0,0),
//   VolumeType=Sphere, VolumeStretch=(1,1,1), VolumeSize=1, FallOff=0, Bias=0,
//   UseWAsWeight=false, GainAndBias=(0.5,0.5), StrengthFactor=None.
// FORK (softtransformpoints.metal):
//   - TransformVolume (float4x4) composed IN-shader from VolumeCenter/Stretch/Size (no Rotate
//     on the VOLUME in the .hlsl — only translate+scale build the volume matrix; the Rotate
//     port drives the POINT rotation, not the volume).
//   - Strength cbuffer field = Amount (the .cs ITransformable wires Amount → shader Strength;
//     verified by the single Strength scalar the .hlsl multiplies into the weight).
//   - Dither / ScaleW(volume) / Bias / UseWAsWeight / Visibility are DEAD in the .hlsl -> dropped.
//     (.hlsl reads Strength, Translate, Scale·ScaleMagnitude, RotateAxis, FallOff, GainAndBias,
//      Phase, Threshold, ScaleFx1, OffsetFx1, VolumeShape, StrengthFactor only.)
//   - ScaleW/OffsetW [Input]s map to the .hlsl ScaleFx1/OffsetFx1 cbuffer fields.
static const PointModifyOp _reg_SoftTransformPoints{
      {"SoftTransformPoints",
       "SoftTransformPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // softly-transformed output bag (port 1)
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 1.0f},
        // Translate (TiXL Vector3) — soft positional offset (scaled by the weight).
        {"Translate.x", "Translate", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Translate.y", "Translate.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Translate.z", "Translate.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Stretch (TiXL Vector3, default 1,1,1) — per-axis point scale within the volume.
        {"Stretch.x", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Stretch.z", "Stretch.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f},
        // Rotate (TiXL Vector3, Euler°) — soft rotation axis (scaled by the weight).
        {"Rotate.x", "Rotate", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotate.y", "Rotate.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotate.z", "Rotate.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"ScaleW", "ScaleW", "Float", true, 1.0f, 0.0f, 10.0f},
        {"OffsetW", "OffsetW", "Float", true, 0.0f, -10.0f, 10.0f},
        // VolumeCenter (TiXL Vector3, ITransformable TranslationInput) — volume position.
        {"VolumeCenter.x", "VolumeCenter", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"VolumeCenter.y", "VolumeCenter.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"VolumeCenter.z", "VolumeCenter.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"VolumeType", "VolumeType", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"Sphere", "Box", "Plane", "Zebra"}},
        // VolumeStretch (TiXL Vector3, default 1,1,1) — per-axis volume scale.
        {"VolumeStretch.x", "VolumeStretch", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"VolumeStretch.y", "VolumeStretch.y", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"VolumeStretch.z", "VolumeStretch.z", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"VolumeSize", "VolumeSize", "Float", true, 1.0f, 0.0f, 10.0f},
        // FallOff default 1.0 per SoftTransformPoints.t3 (NOT 0).
        {"FallOff", "FallOff", "Float", true, 1.0f, 0.0f, 5.0f},
        // GainAndBias (TiXL Vector2, default 0.5,0.5) — shapes the falloff weight.
        {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"StrengthFactor", "StrengthFactor", "Float", true, 0.0f, 0.0f, 2.0f,
         Widget::Enum, {"None", "F1", "F2"}}},
       nullptr}
};

// ---- batch 20 (lane point_modify): ClearSomePoints -------------------------
// TiXL parity: external/tixl .../point/modify/ClearSomePoints.cs + .hlsl
// A count-preserving MODIFIER: per-point hash(Resolution,Seed,Repeat,blockIdx) <= Ratio
// kills the point by setting p.Scale = NAN. Ratio=0 → no kill; Ratio=1 → all killed.
// Defaults from ClearSomePoints.t3 (GUID-keyed):
//   Ratio=0.0, Seed=0, Repeat=0, Resolution=0.
// NOTE: the .cs defines Spaces/OffsetModes/Interpolations enums but NONE are [Input] slots
// in the .cs source — they are leftover dead code (the .hlsl kernel has no such branches).
// We match: no enum ports.
static const PointModifyOp _reg_ClearSomePoints{
      {"ClearSomePoints",
       "ClearSomePoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // killed output bag (port 1)
        // .cs Ratio (float, default 0.0) — fraction of points to kill; 0=none, 1=all
        {"Ratio", "Ratio", "Float", true, 0.0f, 0.0f, 1.0f},
        // .cs Seed (int, default 0) — hash seed
        {"Seed", "Seed", "Float", true, 0.0f, 0.0f, 100.0f},
        // .cs Repeat (int, default 0) — period for the hash pattern; 0 = aperiodic
        {"Repeat", "Repeat", "Float", true, 0.0f, 0.0f, 1000.0f},
        // .cs Resolution (int, default 0) — block size; points in same block share one hash
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 100.0f}},
       nullptr}
};

}  // namespace
}  // namespace sw
