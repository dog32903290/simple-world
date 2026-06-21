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
      // ---- batch 16 (lane P): point transform — PolarTransformPoints -------------------
      // TiXL parity: external/tixl .../point/transform/PolarTransformPoints.cs + .hlsl
      // A count-preserving MODIFIER: TRS pre-transform (Translation/Rotation/Scale·UniformScale)
      // then a cartesian->cylindrical (Mode 0) or ->spherical (Mode 1) polar warp, composing the
      // point's rotation with the polar-angle rotations.
      // Defaults from PolarTransformPoints.t3 (GUID-keyed):
      //   Translation=(0,0,0), Rotation=(0,0,0), Scale=(1,1,1), UniformScale=1.0, Mode=0.
      // FORK (point_ops_polartransformpoints.cpp / .metal): TRS matrix composed in-shader from raw
      // scalars; PolarTransform exposes NO Pivot/Shear/Invert ports (pivot=0/shear=0/invert=false).
      {"PolarTransformPoints",
       "PolarTransformPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // warped output bag (port 1)
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"CartesianToCylindrical", "CartesianToSpherical"}},
        // Translation / Rotation(Euler°) / Scale — TiXL Vector3 inputs (Widget::Vec).
        {"Translation.x", "Translation", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Scale.x", "Scale", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Scale.y", "Scale.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale.z", "Scale.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f}},
       nullptr},
      // ---- batch 16 (lane P): point transform — WrapPoints ----------------------------
      // TiXL parity: external/tixl .../point/transform/WrapPoints.cs + .hlsl
      // A count-preserving MODIFIER: wraps each point's position (toroidally) into a box of Size
      // centered at Position, via FLOORED modulo (see wrappoints.metal FORK note).
      // Defaults from WrapPoints.t3 (GUID-keyed): Position=(0,0,0), Size=(1,1,1).
      // NOTE: the .cs `Spaces` enum is NOT an [Input] in TiXL -> no Mode port (matched).
      {"WrapPoints",
       "WrapPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // wrapped output bag (port 1)
        {"Position.x", "Position", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Size.x", "Size", "Float", true, 1.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 3},
        {"Size.y", "Size.y", "Float", true, 1.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 1},
        {"Size.z", "Size.z", "Float", true, 1.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // ---- batch 16 (lane P): point transform — BoundPoints ---------------------------
      // TiXL parity: external/tixl .../point/transform/BoundPoints.cs + .hlsl
      // A count-preserving MODIFIER: clamps each point's position into an AABB of
      // (Size * UniformScale) centered at Position.
      // Defaults from BoundPoints.t3 (GUID-keyed): Position=(0,0,0), Size=(1,1,1), UniformScale=1.0.
      // NOTE: the .cs `Spaces` enum is NOT an [Input] in TiXL -> no Mode port (matched).
      {"BoundPoints",
       "BoundPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // clamped output bag (port 1)
        {"Position.x", "Position", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Size.x", "Size", "Float", true, 1.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 3},
        {"Size.y", "Size.y", "Float", true, 1.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 1},
        {"Size.z", "Size.z", "Float", true, 1.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f}},
       nullptr},
      // ---- batch 18 (lane P): point transform — TransformSomePoints -------------------
      // TiXL parity: external/tixl .../point/transform/TransformSomePoints.cs + .hlsl
      // A count-preserving MODIFIER: applies a TRS transform to each point, lerp-weighted by
      // the point's W channel (selection weight) when WIsWeight=true.
      // Defaults from TransformSomePoints.t3 (GUID-keyed):
      //   Translation=(0,0,0), Rotation=(0,0,0), Scale=(1,1,1), UniformScale=1.0,
      //   Space=PointSpace(0), WIsWeight=false(0), UpdateRotation=true(baked).
      // FORK (transformsomepoints_params.h / .metal):
      //   - TRS matrix composed in-shader from raw scalars (pivot=0/shear=0; no such ports).
      //   - Euler order Y·X·Z = CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z).
      //   - No Strength port (TiXL TransformSomePoints.cs has none; per-point weighting via WIsWeight×W).
      //   - UpdateRotation baked to true; ScaleW/OffsetW baked to identity (1/0).
      //   - Take/Skip/RangeStart/LengthFactor/Scatter/OnlyKeepTakes baked (all points transformed).
      //   - Space=WorldSpace(2) baked to ObjectSpace (no view transform in cook ctx).
      {"TransformSomePoints",
       "TransformSomePoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // transformed output bag (port 1)
        {"Space", "Space", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Point", "Object"}},
        {"WIsWeight", "WIsWeight", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        // Translation / Rotation(Euler°) / Scale — TiXL Vector3 inputs (Widget::Vec).
        {"Translation.x", "Translation", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Translation.y", "Translation.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Translation.z", "Translation.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Rotation.x", "Rotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Scale.x", "Scale", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Scale.y", "Scale.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Scale.z", "Scale.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 10.0f}},
       nullptr},
      // ---- batch 19: point transform — WrapPointPosition --------------------------
      // TiXL parity: external/tixl .../point/transform/WrapPointPosition.cs + .hlsl
      // A count-preserving MODIFIER: CUBE FOLD wrap (offset-factor trick) — distinct from
      // WrapPoints (floored-mod torus). For each axis, if |p-center| > halfSize+padding,
      // apply offsetFactor ±1 -> wrappedP = p + Size * offsetFactor. W = edge-fade.
      // Defaults: Position/Center=(0,0,0), Size=(2,2,2) [TiXL default].
      // FORK: UseCamera baked 0 (no camera matrix in cook ctx).
      //       AddLineBreaks baked 0 (W edge-fade path only; line-break variant deferred).
      {"WrapPointPosition",
       "WrapPointPosition",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // cube-folded output bag (port 1)
        // .cs slot "Position" = box center in world space
        {"Position.x", "Position", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Position.y", "Position.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Position.z", "Position.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // .cs slot "Size" = box extents; default (2,2,2) per TiXL
        {"Size.x", "Size", "Float", true, 2.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 3},
        {"Size.y", "Size.y", "Float", true, 2.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 1},
        {"Size.z", "Size.z", "Float", true, 2.0f, 0.0f, 20.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // ---- batch 19: point transform — SnapPointsToGrid ---------------------------
      // TiXL parity: external/tixl .../point/transform/SnapPointsToGrid.cs + .hlsl
      // A count-preserving MODIFIER: lerps each point's position toward the nearest
      // grid-cell center with blend Amount, shaped by Mode (4 modes) + GainAndBias.
      // Defaults from SnapPointsToGrid.t3: Amount=1.0, GridScale=1.0, GridStretch=1,1,1,
      //   GridOffset=0,0,0, Mode=CenterDistance(0), BiasAndGain=0.5,0.5.
      // FORK: Scatter baked 0 (hash jitter deferred); StrengthFactor=None baked;
      //       UseWAsWeight/UseSelection baked 0.
      {"SnapPointsToGrid",
       "SnapPointsToGrid",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // snapped output bag (port 1)
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 2.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"CenterDistance", "CornersDistance", "AxisCenterDistance", "AxisEdgeDistance"}},
        {"GridScale", "GridScale", "Float", true, 1.0f, 0.01f, 10.0f},
        // GridStretch (TiXL Vector3, default 1,1,1) — per-axis grid cell scale
        {"GridStretch.x", "GridStretch", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"GridStretch.y", "GridStretch.y", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"GridStretch.z", "GridStretch.z", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
        // GridOffset (TiXL Vector3, default 0,0,0) — phase offset within grid
        {"GridOffset.x", "GridOffset", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"GridOffset.y", "GridOffset.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"GridOffset.z", "GridOffset.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        // BiasAndGain (TiXL Vector2, default 0.5,0.5) — shapes the snap blend curve
        {"BiasAndGain.x", "BiasAndGain", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"BiasAndGain.y", "BiasAndGain.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // ---- batch 20 (lane point_modify): ClearSomePoints -------------------------
      // TiXL parity: external/tixl .../point/modify/ClearSomePoints.cs + .hlsl
      // A count-preserving MODIFIER: per-point hash(Resolution,Seed,Repeat,blockIdx) <= Ratio
      // kills the point by setting p.Scale = NAN. Ratio=0 → no kill; Ratio=1 → all killed.
      // Defaults from ClearSomePoints.t3 (GUID-keyed):
      //   Ratio=0.0, Seed=0, Repeat=0, Resolution=0.
      // NOTE: the .cs defines Spaces/OffsetModes/Interpolations enums but NONE are [Input] slots
      // in the .cs source — they are leftover dead code (the .hlsl kernel has no such branches).
      // We match: no enum ports.
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
       nullptr},
      // ---- batch 21 (lane point_modify): ReorientLinePoints ----------------------
      // TiXL parity: external/tixl .../point/transform/ReorientLinePoints.cs + .hlsl
      // A count-preserving MODIFIER: re-orients each live point's Rotation so its +Z forward
      // follows the local line tangent (prevLiveNeighbour -> nextLiveNeighbour), blended by
      // Amount via qSlerp. Defaults: Amount=1.0.
      // FORK: the .cs Center/UpVector/WIsWeight/Flip [Input]s are DEAD in the .hlsl kernel
      //       (main() reads only Amount) -> dropped (porting them = inventing dead knobs).
      {"ReorientLinePoints",
       "ReorientLinePoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // re-oriented output bag (port 1)
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 1.0f}},
       nullptr},
      // ---- batch 36 (lane point_modify): ResampleLinePoints ----------------------
      // TiXL parity: external/tixl .../point/modify/ResampleLinePoints.cs + .hlsl
      // A COUNT-CHANGING MODIFIER: resamples the source line bag into `Count` points along the
      // source's normalized PARAMETER f in [0,1] (TiXL samples by linear-index parameter, NOT true
      // arc-length — see SamplePosAtF). Each output is a SmoothDistance-weighted average over
      // (1 + 2*Samples) parameter taps; SEPARATOR points (NaN Scale) break the line into segments.
      // Defaults from ResampleLinePoints.t3: Count=100, RangeMode=StartEnd(0), SampleRange=(0,1),
      //   SmoothDistance=0.5, Samples=3, Rotation=Interpolate(0), RotationUpVector=(0,0,1).
      // Ports in .cs [Input] order: Points, Count, RangeMode, SampleRange, SmoothDistance, Samples,
      //   Rotation, RotationUpVector. EVERY port is read by the kernel — no dead port dropped.
      // NAMED FORK: Count is a Float port (resolved-param spine), cast to int in the cook (clamp
      //   1..100000 per the .t3 ClampInt). Samples clamped 1..10 (.t3 ClampInt). Same Float-port
      //   convention as FilterPoints.
      {"ResampleLinePoints",
       "ResampleLinePoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // resampled output bag (port 1)
        {"Count", "Count", "Float", true, 100.0f, 1.0f, 8192.0f},  // output point count (.t3 default 100)
        {"RangeMode", "RangeMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"StartEnd", "StartLength"}},
        // SampleRange (TiXL Vector2, default (0,1)) — (start, end-or-length) of the f sweep.
        {"SampleRange.x", "SampleRange", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"SampleRange.y", "SampleRange.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"SmoothDistance", "SmoothDistance", "Float", true, 0.5f, 0.0f, 10.0f},
        {"Samples", "Samples", "Float", true, 3.0f, 1.0f, 10.0f},
        {"Rotation", "Rotation", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Interpolate", "Recompute"}},
        // RotationUpVector (TiXL Vector3, default (0,0,1)) — up vector for Recompute(qLookAt).
        {"RotationUpVector.x", "RotationUpVector", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"RotationUpVector.y", "RotationUpVector.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"RotationUpVector.z", "RotationUpVector.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // ---- batch 37 (lane point_modify): SubdivideLinePoints ----------------------
      // TiXL parity: external/tixl .../point/generate/SubdivideLinePoints.cs + .hlsl
      // A COUNT-CHANGING MODIFIER: subdivides every line SEGMENT, inserting InsertCount interpolated
      // points per segment (subdiv = InsertCount + 1). Open line of SourceCount points ->
      // SourceCount * subdiv outputs; ClosedShape adds a closing segment (lastValid -> firstValid)
      // and separators (NaN Scale) carve the closed segments. Output count = clamp(sourceCount*subdiv,
      // 1, 1000000) via the cook's static-stash countTransform (see point_ops_subdividelinepoints.cpp).
      // Defaults from SubdivideLinePoints.t3: Count(InsertCount)=100, ClosedShape=false.
      // Ports in .cs [Input] order: Points, Count, ClosedShape.
      // NAMED FORK [port-id=InsertCount]: the .cs port is named "Count" but the cook driver hijacks any
      //   Float port whose id == "Count" as the OUTPUT point count. SubdivideLinePoints' Count is
      //   per-segment InsertCount, NOT the output count, so the port id is "InsertCount" (inspector
      //   label "Count" matches TiXL); the driver then uses the source bag count and countTransform
      //   multiplies by subdiv. See point_ops_subdividelinepoints.cpp COUNT POLICY.
      {"SubdivideLinePoints",
       "SubdivideLinePoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // subdivided output bag (port 1)
        // .cs Count (int, default 100) = InsertCount: points inserted per segment. Port id forked to
        //   "InsertCount" to dodge the driver's "Count"==output-count hijack (see fork note above).
        {"InsertCount", "Count", "Float", true, 100.0f, 0.0f, 1000.0f},
        // .cs ClosedShape (bool, default false) — add a closing segment (last -> first).
        {"ClosedShape", "ClosedShape", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
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
       nullptr},
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
       nullptr},
      // ---- batch 24 (lane point_modify): OffsetPoints ----------------------------
      // TiXL parity: external/tixl .../point/_internal/_OffsetPoints.cs (.cs ports lines 10-17) +
      //              .../Assets/shaders/points/modify/OffsetPoints.hlsl (.hlsl math lines 30-45)
      // The cleanest count-preserving modifier (no simplification): per point
      //   Position += qRotateVec3(Direction * Distance, Point.Rotation)   (.hlsl line 40)
      // The offset is rotated by the point's OWN existing Rotation — no new rotation is built, so
      // there is no Euler/rotation-order question here. Rotation/Color/Scale/W preserved verbatim.
      // .cs ports: Points / Direction(Vector3) / Distance(float). Op name drops TiXL's internal-
      // namespace leading underscore (class is _OffsetPoints in Lib.point._internal).
      // Defaults: Direction=(0,0,1), Distance=0 (Distance 0 -> identity no-op until the user dials it).
      {"OffsetPoints",
       "OffsetPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // offset output bag (port 1)
        // Direction (TiXL Vector3) — offset direction in each point's LOCAL frame (rotated by its Rotation).
        {"Direction.x", "Direction", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Direction.y", "Direction.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Direction.z", "Direction.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Distance", "Distance", "Float", true, 0.0f, -10.0f, 10.0f}},
       nullptr},
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
       nullptr},
      // ---- batch sw-node-batch (lane point_modify, sim family): SimNoiseOffset --------------
      // TiXL parity: external/tixl .../point/sim/SimNoiseOffset.cs + .hlsl
      // Count-preserving MODIFIER: (simplex|curl)-noise position displacement + tangent rotation.
      // Defaults from SimNoiseOffset.t3: Amount=0.2, Frequency=1.0, Phase=0.0, Variation=0.0,
      //   AmountDistribution=(1,1,1), RotLookupDistance=2.0, UseCurlNoise=true.
      {"SimNoiseOffset",
       "SimNoiseOffset",
       {{"points", "points", "Points", true},   // input bag (port 0)
        {"out", "out", "Points", false},         // displaced output bag (port 1)
        {"Amount", "Amount", "Float", true, 0.2f, -200.0f, 200.0f},
        {"Frequency", "Frequency", "Float", true, 1.0f, 0.0f, 20.0f},
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Variation", "Variation", "Float", true, 0.0f, 0.0f, 100.0f},
        {"AmountDistribution.x", "AmountDistribution", "Float", true, 1.0f, 0.0f, 2.0f,
         Widget::Vec, {}, true, 3},
        {"AmountDistribution.y", "AmountDistribution.y", "Float", true, 1.0f, 0.0f, 2.0f,
         Widget::Vec, {}, true, 1},
        {"AmountDistribution.z", "AmountDistribution.z", "Float", true, 1.0f, 0.0f, 2.0f,
         Widget::Vec, {}, true, 1},
        {"RotLookupDistance", "RotLookupDistance", "Float", true, 2.0f, 0.0f, 10.0f},
        {"UseCurlNoise", "UseCurlNoise", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
      // ---- batch sw-node-batch (lane point_modify, sim family): SimCentricalOffset ----------
      // TiXL parity: external/tixl .../point/sim/SimCentricalOffset.cs + .hlsl
      // Count-preserving MODIFIER: radial inverse-power force along (Position-Center); positive
      // Amount pushes outward. Defaults from .t3: Center=(0,0,0), MaxAcceleration=1.0,
      //   Amount(->Acceleration)=0.04, DecayExponent=2.0. (.cs ShowGizmo omitted.)
      {"SimCentricalOffset",
       "SimCentricalOffset",
       {{"points", "points", "Points", true},   // input bag (port 0)
        {"out", "out", "Points", false},         // forced output bag (port 1)
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"MaxAcceleration", "MaxAcceleration", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Amount", "Amount", "Float", true, 0.04f, -100.0f, 100.0f},
        {"DecayExponent", "DecayExponent", "Float", true, 2.0f, 0.0f, 10.0f}},
       nullptr},
      // ---- batch sw-node-batch (lane point_modify, sim family): SimDirectionalOffset --------
      // TiXL parity: external/tixl .../point/sim/SimDirectionalOffset.cs + .hlsl
      // Count-preserving MODIFIER: directional position push (Mode 0) / velocity-in-rotation
      // encode (Mode 1). Defaults from .t3: Direction=(0,0.01,0), Amount=1.0, RandomAmount=0.0,
      //   Mode=0 (Legacy). (.cs ShowGizmo omitted.)
      {"SimDirectionalOffset",
       "SimDirectionalOffset",
       {{"points", "points", "Points", true},   // input bag (port 0)
        {"out", "out", "Points", false},         // pushed output bag (port 1)
        {"Direction.x", "Direction", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Direction.y", "Direction.y", "Float", true, 0.01f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Direction.z", "Direction.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Amount", "Amount", "Float", true, 1.0f, -100.0f, 100.0f},
        {"RandomAmount", "RandomAmount", "Float", true, 0.0f, 0.0f, 10.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Legacy", "EncodeInRotation"}}},
       nullptr},
      // ---- batch sw-node-batch (lane point_modify, sim family): SimForceOffset --------------
      // TiXL parity: external/tixl .../point/sim/SimForceOffset.cs + .hlsl
      // Count-preserving MODIFIER: radial force + gravity gated by a radius/falloff window.
      // Defaults from .t3: Center=(0,0,0), Radius=999.0, RadiusFallOff=0.0, RadialForce=0.0,
      //   UseWForMass=0.0, Variation=0.0, Gravity=(0,0,0), ForceDecayRate=1.0. (.cs IsEnabled omitted.)
      {"SimForceOffset",
       "SimForceOffset",
       {{"points", "points", "Points", true},   // input bag (port 0)
        {"out", "out", "Points", false},         // forced output bag (port 1)
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Radius", "Radius", "Float", true, 999.0f, 0.0f, 1000.0f},
        {"RadiusFallOff", "RadiusFallOff", "Float", true, 0.0f, 0.0f, 100.0f},
        {"RadialForce", "RadialForce", "Float", true, 0.0f, -100.0f, 100.0f},
        {"UseWForMass", "UseWForMass", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Variation", "Variation", "Float", true, 0.0f, 0.0f, 100.0f},
        {"Gravity.x", "Gravity", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 3},
        {"Gravity.y", "Gravity.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"Gravity.z", "Gravity.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
        {"ForceDecayRate", "ForceDecayRate", "Float", true, 1.0f, 0.0f, 10.0f}},
       nullptr},
      // SamplePointColorAttributes — the FIRST Points op with a Texture2D INPUT (the texture-into-
      // points seam's proving op). Ports 1:1 with SamplePointColorAttributes.cs (.t3 defaults). The
      // Texture2D input port (after the Points input, matching .cs GPoints→Texture order) is gathered
      // by the cook drivers' Texture2D loop into PointCookCtx::inputTextures[0]. BlendMode labels =
      // SharedEnums.RgbBlendModes (Core/Utils/SharedEnums.cs). Stretch/Scale/TextureRotate (+ the
      // texW/texH Aspect correction) compose transformSampleSpace; TextureMode drives the sampler wrap
      // — all LIVE (the .cpp composes Scale3 host-side + the shader applies Scale·Rotate; sampler =
      // Repeat+Nearest per .t3 TextureMode=Wrap / SamplerState=MinMagMipPoint).
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
       nullptr},
  };
  return specs;
}

}  // namespace sw
