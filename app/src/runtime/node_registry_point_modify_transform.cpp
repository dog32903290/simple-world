// runtime/node_registry_point_modify_transform — TRS / polar / wrap / bound / snap / offset point modifiers (TiXL point/transform + _internal).
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

static const PointModifyOp _reg_TransformPoints{
      {"TransformPoints",
       "TransformPoints",
       // points input marked required=true (experience-S0 demo). Trailing positional fields are the
       // PortSpec defaults def/minV/maxV/widget/labels/pinless/vecArity/multiInput/strDef, then the
       // new last member required=true.
       {{"points", "points", "Points", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "", true},  // input bag (port 0), REQUIRED
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
       nullptr,
       "point.transform"}  // category (experience-S0 demo, = TiXL Symbol.Namespace)
};

static const PointModifyOp _reg_OrientPoints{
      {"OrientPoints",
       "OrientPoints",
       // points input marked required=true (experience-S0 demo).
       {{"points", "points", "Points", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "", true},  // input bag (port 0), REQUIRED
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
       nullptr,
       "point.transform"}  // category (experience-S0 demo, = TiXL Symbol.Namespace)
};

// ---- batch 16 (lane P): point transform — PolarTransformPoints -------------------
// TiXL parity: external/tixl .../point/transform/PolarTransformPoints.cs + .hlsl
// A count-preserving MODIFIER: TRS pre-transform (Translation/Rotation/Scale·UniformScale)
// then a cartesian->cylindrical (Mode 0) or ->spherical (Mode 1) polar warp, composing the
// point's rotation with the polar-angle rotations.
// Defaults from PolarTransformPoints.t3 (GUID-keyed):
//   Translation=(0,0,0), Rotation=(0,0,0), Scale=(1,1,1), UniformScale=1.0, Mode=0.
// FORK (point_ops_polartransformpoints.cpp / .metal): TRS matrix composed in-shader from raw
// scalars; PolarTransform exposes NO Pivot/Shear/Invert ports (pivot=0/shear=0/invert=false).
static const PointModifyOp _reg_PolarTransformPoints{
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
       nullptr}
};

// ---- batch 16 (lane P): point transform — WrapPoints ----------------------------
// TiXL parity: external/tixl .../point/transform/WrapPoints.cs + .hlsl
// A count-preserving MODIFIER: wraps each point's position (toroidally) into a box of Size
// centered at Position, via FLOORED modulo (see wrappoints.metal FORK note).
// Defaults from WrapPoints.t3 (GUID-keyed): Position=(0,0,0), Size=(1,1,1).
// NOTE: the .cs `Spaces` enum is NOT an [Input] in TiXL -> no Mode port (matched).
static const PointModifyOp _reg_WrapPoints{
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
       nullptr}
};

// ---- batch 16 (lane P): point transform — BoundPoints ---------------------------
// TiXL parity: external/tixl .../point/transform/BoundPoints.cs + .hlsl
// A count-preserving MODIFIER: clamps each point's position into an AABB of
// (Size * UniformScale) centered at Position.
// Defaults from BoundPoints.t3 (GUID-keyed): Position=(0,0,0), Size=(1,1,1), UniformScale=1.0.
// NOTE: the .cs `Spaces` enum is NOT an [Input] in TiXL -> no Mode port (matched).
static const PointModifyOp _reg_BoundPoints{
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
       nullptr}
};

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
static const PointModifyOp _reg_TransformSomePoints{
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
       nullptr}
};

// ---- batch 19: point transform — WrapPointPosition --------------------------
// TiXL parity: external/tixl .../point/transform/WrapPointPosition.cs + .hlsl
// A count-preserving MODIFIER: CUBE FOLD wrap (offset-factor trick) — distinct from
// WrapPoints (floored-mod torus). For each axis, if |p-center| > halfSize+padding,
// apply offsetFactor ±1 -> wrappedP = p + Size * offsetFactor. W = edge-fade.
// Defaults: Position/Center=(0,0,0), Size=(2,2,2) [TiXL default].
// FORK: UseCamera baked 0 (no camera matrix in cook ctx).
//       AddLineBreaks baked 0 (W edge-fade path only; line-break variant deferred).
static const PointModifyOp _reg_WrapPointPosition{
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
       nullptr}
};

// ---- batch 19: point transform — SnapPointsToGrid ---------------------------
// TiXL parity: external/tixl .../point/transform/SnapPointsToGrid.cs + .hlsl
// A count-preserving MODIFIER: lerps each point's position toward the nearest
// grid-cell center with blend Amount, shaped by Mode (4 modes) + GainAndBias.
// Defaults from SnapPointsToGrid.t3: Amount=1.0, GridScale=1.0, GridStretch=1,1,1,
//   GridOffset=0,0,0, Mode=CenterDistance(0), BiasAndGain=0.5,0.5.
// FORK: Scatter baked 0 (hash jitter deferred); StrengthFactor=None baked;
//       UseWAsWeight/UseSelection baked 0.
static const PointModifyOp _reg_SnapPointsToGrid{
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
       nullptr}
};

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
static const PointModifyOp _reg_OffsetPoints{
      {"OffsetPoints",
       "OffsetPoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // offset output bag (port 1)
        // Direction (TiXL Vector3) — offset direction in each point's LOCAL frame (rotated by its Rotation).
        {"Direction.x", "Direction", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Direction.y", "Direction.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Direction.z", "Direction.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Distance", "Distance", "Float", true, 0.0f, -10.0f, 10.0f}},
       nullptr}
};

}  // namespace
}  // namespace sw
