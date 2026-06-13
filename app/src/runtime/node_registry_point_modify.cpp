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
  };
  return specs;
}

}  // namespace sw
