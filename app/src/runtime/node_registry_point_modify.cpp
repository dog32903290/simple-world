// runtime/node_registry_point_modify — NodeSpec table for point MODIFIER ops.
// Modifiers take a Points input bag and emit a modified Points output bag (count-preserving
// or count-changing).  Ops: TransformPoints, OrientPoints, RandomizePoints, SetPointAttributes,
// AddNoise, FilterPoints.  Split from node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
// Phase B "point-transform lane" adds NEW point ops here — this file is the extension point.
#include "runtime/node_registry_point_modify.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& pointModifySpecs() {
  static const std::vector<NodeSpec> specs = {
      {"TransformPoints",
       "TransformPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // transformed output bag (port 1)
        {"Space", "Space", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Point", "Object"}},
        // Translation / Rotation(Euler°) / Stretch / Pivot — TiXL Vector3 inputs (Widget::Vec).
        {"Translation.x", "Translation", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Stretch.x", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Stretch.y", "Stretch.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Stretch.z", "Stretch.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Pivot.x", "Pivot", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 1.0f}},
       nullptr},
      {"OrientPoints",
       "OrientPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // re-oriented output bag (port 1)
        {"OrientationMode", "OrientationMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"LookAtTarget", "Screen", "LookAtCamera"}},
        {"AmountFactor", "AmountFactor", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"None", "F1", "F2"}},
        {"Flip", "Flip", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 1.0f},
        // Target / UpVector — TiXL Vector3 inputs (Widget::Vec, read via readVecN).
        {"Target.x", "Target", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Target.y", "Target.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Target.z", "Target.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"UpVector.x", "UpVector", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"UpVector.y", "UpVector.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"UpVector.z", "UpVector.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}},
       nullptr},
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
       nullptr},
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
       nullptr},
      // ---- batch 15: point modify — AddNoise ----------------------------------------
      // TiXL parity: external/tixl .../point/modify/AddNoise.cs + AddNoise.hlsl
      // A count-preserving MODIFIER: displaces Position by snoiseVec3 field and updates
      // Rotation to follow the displaced tangent frame (RotationLookupDistance probe).
      // Defaults from AddNoise.t3 (GUID-keyed):
      //   Strength=1.0, StrengthFactor=None, Frequency=1.0, Phase=0.0, Variation=0.0,
      //   AmountDistribution=(1,1,1), RotationLookupDistance=0.25, NoiseOffset=(0,0,0)
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
       nullptr},
      // ---- batch 15: point modify — FilterPoints -------------------------------------
      // TiXL parity: external/tixl .../point/modify/FilterPoints.cs + FilterPoints.hlsl
      // Re-samples/re-indexes the input bag into a new fixed-size output buffer (Count port).
      // Output COUNT = Count port (not the input bag count) — this op changes count.
      // Shader does scatter-copy: ResultPoints[i] = SourcePoints[imod2(StartIndex +
      //   (i*StepSize) + scatterOffset, SourceCount)] where scatterOffset = Scatter>0 ?
      //   SourceCount*Scatter*hash11u(i+Seed*SourceCount+StartIndex) : 0.
      // Defaults from FilterPoints.t3: Count=1, StartIndex=0, Step=1.0, ScatterSelect=0.0, Seed=0
      // Fork from TiXL: Count is a Float port (not Int) to match the resolved-param spine
      // contract; the shader receives it cast to int. NAMED FORK (refuter-R-PMF3 修帳): TiXL clamps
      // to [0,1000000]; we have NO runtime cap — only the port UI max (8192, conservative).
      {"FilterPoints",
       "FilterPoints",
       {{"points", "points", "Points", true},   // input bag (port 0)
        {"out", "out", "Points", false},         // resampled output bag (port 1)
        // Count drives the OUTPUT buffer size (changes point count — not a modifier!).
        {"Count", "Count", "Float", true, 1.0f, 0.0f, 8192.0f},
        {"StartIndex", "StartIndex", "Float", true, 0.0f, 0.0f, 8191.0f},
        {"Step", "Step", "Float", true, 1.0f, 0.0f, 100.0f},
        {"ScatterSelect", "ScatterSelect", "Float", true, 0.0f, 0.0f, 1.0f},
        {"Seed", "Seed", "Float", true, 0.0f, 0.0f, 100.0f}},
       nullptr},
  };
  return specs;
}

}  // namespace sw
