// runtime/node_registry — the NodeSpec table (one row per node type) + the pure
// value-node evaluate fns it points at. Split out of graph.cpp so the data table can
// grow with each fan-out batch without bloating the graph model/eval code (graph.cpp
// kept < 400; this is the data-driven registry, ARCHITECTURE rule 7). runtime leaf.
#include "runtime/graph.h"
#include "runtime/Particle.h"  // full EvaluationContext definition (for the evaluate fns)

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace sw {
namespace {

// ----- Value-node evaluate functions (pure value, no GPU). -----
// in[] is ordered by the Float input ports in the spec; n is the count.

float evalTime(int, const float*, int, const EvaluationContext& ctx) { return ctx.time; }
// AudioReaction is stateful (TiXL parity) and has no pure evaluate — it's cooked in main from
// the live spectrum into Node::outCache, which evalFloat returns directly (see below).
float evalConst(int, const float* in, int n, const EvaluationContext&) { return n > 0 ? in[0] : 0.0f; }
float evalMultiply(int, const float* in, int n, const EvaluationContext&) {
  return n >= 2 ? in[0] * in[1] : 0.0f;
}
float evalSine(int, const float* in, int n, const EvaluationContext&) {
  return n > 0 ? std::sin(in[0]) : 0.0f;
}
float evalRemap(int, const float* in, int n, const EvaluationContext&) {
  // in: [x, outMin, outMax]. x in -1..1 → outMin..outMax.
  if (n < 3) return 0.0f;
  float t = (in[0] + 1.0f) * 0.5f;         // -1..1 → 0..1
  return in[1] + (in[2] - in[1]) * t;      // → outMin..outMax
}

// NodeSpec registry — params unified into Float input ports (schema spine, Task 1).
// id kept identical to old ParamSpec.id so Node::params map + save/load stay compatible.
const std::vector<NodeSpec>& registry() {
  static const std::vector<NodeSpec> specs = {
      {"RadialPoints",
       "RadialPoints",
       {{"points", "points", "Points", false},
        {"Count", "Count", "Float", true, 2048.0f, 16.0f, 8192.0f},
        {"Radius", "Radius", "Float", true, 2.0f, 0.1f, 10.0f},
        // Center (TiXL Vector3 TranslationInput) — first vector param on the contract.
        // Three Float components drawn as one DragFloat3; read via evalVecN("Center").
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // TiXL RadialPoints scalars the kernel already consumes (radial_points.metal:
        // angle = StartAngle° + Cycles·2π·f, length = Radius + RadiusOffset·f). Declared as
        // ports because the resolved-param seam (cookParam) reads ONLY spec ports — an
        // undeclared op-read silently falls to its default (caught by radialop-bug teeth).
        // ⚠ APPENDED, not inserted: pin ids are port-INDEX based (pinId), so saved .swproj
        // wires into Center.* would silently re-target if indices shift. v2 schema (批次 2)
        // moves connections to slot IDS, which retires this hazard.
        {"RadiusOffset", "RadiusOffset", "Float", true, 0.0f, -10.0f, 10.0f},
        {"StartAngle", "StartAngle", "Float", true, 0.0f, 0.0f, 360.0f},
        {"Cycles", "Cycles", "Float", true, 1.0f, 0.0f, 10.0f}},
       nullptr},
      {"LinePoints",
       "LinePoints",
       {{"points", "points", "Points", false},
        {"Count", "Count", "Float", true, 64.0f, 2.0f, 8192.0f},
        {"Length", "Length", "Float", true, 5.0f, 0.0f, 50.0f},
        {"Pivot", "Pivot", "Float", true, 0.5f, 0.0f, 1.0f},
        // Center (TiXL Vector3 TranslationInput) — line start before pivot/length.
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Direction (TiXL Vector3) — line orientation; .md default 0,1,0 (points up).
        {"Direction.x", "Direction", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Direction.y", "Direction.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Direction.z", "Direction.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // GainAndBias (TiXL Vector2) — distribution along the line; 0.5,0.5 = identity.
        {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // Scale (TiXL Vector2 / PointSize) — base + per-index scale.
        {"Scale.x", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Scale.y", "Scale.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      {"GridPoints",
       "GridPoints",
       {{"points", "points", "Points", false},
        // Count = output buffer CAPACITY (host sets = CountX*CountY*CountZ; PointGraph::nodeCount
        // sizes the bag from this single \"Count\" port). CountX/Y/Z below are the real grid dims.
        {"Count", "Count", "Float", true, 100.0f, 1.0f, 65536.0f},
        {"CountX", "CountX", "Float", true, 10.0f, 1.0f, 256.0f, Widget::Slider},
        {"CountY", "CountY", "Float", true, 10.0f, 1.0f, 256.0f, Widget::Slider},
        {"CountZ", "CountZ", "Float", true, 1.0f, 1.0f, 256.0f, Widget::Slider},
        {"SizeMode", "SizeMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Cell", "Bounds"}},
        // Size (TiXL Vector3) — per-axis extent (Cell: spacing, Bounds: total volume).
        {"Size.x", "Size", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Size.y", "Size.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Size.z", "Size.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Center (TiXL Vector3 TranslationInput) — translation added to every point.
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Pivot (TiXL Vector3) — grid anchor offset; 0 centers the grid on Center.
        {"Pivot.x", "Pivot", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 3},
        {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"PointScale", "PointScale", "Float", true, 1.0f, 0.0f, 5.0f, Widget::Slider}},
       nullptr},
      {"SpherePoints",
       "SpherePoints",
       {{"points", "points", "Points", false},
        {"Count", "Count", "Float", true, 2048.0f, 16.0f, 8192.0f},
        {"Radius", "Radius", "Float", true, 2.0f, 0.1f, 10.0f},
        // Center (TiXL Vector3) — vector param: 3 Float components drawn as one DragFloat3,
        // read via readVecN("Center"). Head port id "Center.x", name "Center", Vec/arity 3.
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"StartAngle", "StartAngle", "Float", true, 0.0f, 0.0f, 360.0f},
        {"Scatter", "Scatter", "Float", true, 0.0f, 0.0f, 3.14159f}},
       nullptr},
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
      {"CombineBuffers",
       "CombineBuffers",
       // COMBINE op: up to 4 Points inputs concatenated into one output bag (TiXL MultiInput).
       // Output count = sum of wired inputs (PointGraph::nodeCount sumPointsCount contract).
       {{"input0", "input0", "Points", true},
        {"input1", "input1", "Points", true},
        {"input2", "input2", "Points", true},
        {"input3", "input3", "Points", true},
        {"out", "out", "Points", false}},
       nullptr},
      {"TurbulenceForce",
       "TurbulenceForce",
       {{"force", "force", "ParticleForce", false},
        {"Amount", "Amount", "Float", true, 15.0f, 0.0f, 100.0f},
        {"Frequency", "Frequency", "Float", true, 1.2f, 0.0f, 5.0f},
        {"Phase", "Phase", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr},
      {"ParticleSystem",
       "ParticleSystem",
       {{"emit", "emit", "Points", true},
        {"forces", "forces", "ParticleForce", true},
        {"result", "result", "Points", false},
        {"Speed", "Speed", "Float", true, 1.0f, 0.0f, 3.0f},
        {"Drag", "Drag", "Float", true, 0.02f, 0.0f, 0.2f},
        {"OrientTowardsVelocity", "OrientTowardsVelocity", "Float", true, 0.15f, 0.0f, 1.0f}},
       nullptr},
      // DrawPoints (TiXL Slot<Command> out): points bag in -> a render Command out. The Command
      // output wires into a RenderTarget, which executes it into a Texture2D.
      {"DrawPoints", "DrawPoints",
       {{"points", "points", "Points", true},
        {"out", "out", "Command", false}},
       nullptr},
      // RenderTarget (TiXL Lib.image.generate.basic.RenderTarget): executes a Command chain into a
      // sized Texture2D — the RESOLUTION PIN. Command in, Texture2D out; Resolution enum picks the
      // output size (WindowFollow tracks the viewport, fixed modes pin a standard size, Custom reads
      // CustomW/H); ClearColor is the background. See docs/runtime/RENDER_TARGET_CONTRACT.md.
      {"RenderTarget", "RenderTarget",
       {{"command", "command", "Command", true},
        {"out", "out", "Texture2D", false},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"ClearColor.x", "ClearColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ClearColor.y", "ClearColor.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.z", "ClearColor.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ClearColor.w", "ClearColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr},
      // Blur (TiXL Lib.image.fx.blur.Blur): the FIRST image filter — Texture2D in -> Texture2D out,
      // a 2-pass directional Gaussian (point_ops_blur.cpp). Params mirror Blur.cs: Size (reach),
      // Samples (taps), Offset (added constant), Opacity (rgb intensity -> shader Glow2). Resolution
      // picks the output texture size (same enum as RenderTarget; default WindowFollow). FORK
      // (named): TiXL's Wrap (TextureAddressMode) input is omitted — the op uses a fixed clamp
      // sampler (= MirrorOnce default for blur); non-default Wrap is a follow-up.
      {"Blur", "Blur",
       {{"Image", "Image", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        {"Size", "Size", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Samples", "Samples", "Float", true, 8.0f, 1.0f, 10.0f},
        {"Offset", "Offset", "Float", true, 0.0f, -1.0f, 1.0f},
        {"Opacity", "Opacity", "Float", true, 1.0f, 0.0f, 4.0f},
        {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
        {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
        {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
       nullptr},
      // --- Value nodes (Task 2) ---
      {"Time", "Time", {{"out", "out", "Float", false}}, evalTime},
      // TiXL AudioReaction (full parity): 3 outputs + 10 params. STATEFUL — cooked in main
      // from the live spectrum (runtime/audio_reaction) because it needs the whole spectrum
      // (too big for ctx) + per-node memory; so it has no pure evaluate() and evalFloat reads
      // its outputs from Node::outCache. Params are pinless (Inspector knobs, no canvas pins).
      {"AudioReaction", "AudioReaction",
       {{"Level", "Level", "Float", false},
        {"WasHit", "WasHit", "Float", false},
        {"HitCount", "HitCount", "Float", false},
        {"Amplitude", "Amplitude", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
        {"InputBand", "InputBand", "Float", true, 2.0f, 0.0f, 4.0f, Widget::Enum,
         {"RawFft", "NormalizedFft", "FrequencyBands", "Peaks", "Attacks"}, true},
        {"WindowCenter", "WindowCenter", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowWidth", "WindowWidth", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowEdge", "WindowEdge", "Float", true, 1.0f, 0.0001f, 1.0f, Widget::Slider, {}, true},
        {"Threshold", "Threshold", "Float", true, 0.5f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.1f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"Output", "Output", "Float", true, 3.0f, 0.0f, 4.0f, Widget::Enum,
         {"Pulse", "TimeSinceHit", "Count", "Level", "AccumulatedLevel"}, true},
        {"Bias", "Bias", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider, {}, true},
        {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr},
      {"Const", "Const",
       {{"value", "value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalConst},
      {"Multiply", "Multiply",
       {{"a", "a", "Float", true, 1.0f, -10.0f, 10.0f},
        {"b", "b", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalMultiply},
      {"Sine", "Sine",
       {{"x", "x", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalSine},
      {"Remap", "Remap",
       {{"x", "x", "Float", true, 0.0f, -1.0f, 1.0f},
        {"outMin", "outMin", "Float", true, 0.0f, -10.0f, 10.0f},
        {"outMax", "outMax", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalRemap},
  };
  return specs;
}

}  // namespace

// Dynamic spec table (批次 3): NodeSpecs generated from COMPOUND symbols so the canvas /
// inspector / cook treat a compound child like any node. Rebuilt wholesale by
// refreshCompoundSpecs (graph_bridge) after lib edits; built-ins always win on id clash so
// a compound can never shadow an operator. std::map keeps pointer stability per entry
// across lookups within a frame (the table itself is only swapped between frames).
std::map<std::string, NodeSpec>& dynamicSpecs() {
  static std::map<std::string, NodeSpec> m;
  return m;
}

void setDynamicSpecs(std::map<std::string, NodeSpec> specs) { dynamicSpecs() = std::move(specs); }

const NodeSpec* findSpec(const std::string& type) {
  for (const auto& s : registry())
    if (s.type == type) return &s;
  auto it = dynamicSpecs().find(type);
  return it != dynamicSpecs().end() ? &it->second : nullptr;
}

std::vector<std::string> specTypes() {
  std::vector<std::string> out;
  for (const auto& s : registry()) out.push_back(s.type);
  return out;
}

// Vec group walk: POSITIONAL, the exact same consume-the-run walk the Inspector row uses
// (a head at i owns ports[i..i+N-1]) — one grouping rule, two consumers (同源, graph.h).
AnimGroup animGroupForSlot(const NodeSpec& spec, const std::string& slotId) {
  AnimGroup g{slotId, 0, 1};
  for (size_t i = 0; i < spec.ports.size(); ++i) {
    const PortSpec& p = spec.ports[i];
    if (!p.isInput) continue;
    if (p.widget == Widget::Vec && p.vecArity >= 2) {
      const int n = p.vecArity > 4 ? 4 : p.vecArity;  // same clamp as the Inspector row
      for (int k = 0; k < n && i + (size_t)k < spec.ports.size(); ++k)
        if (spec.ports[i + (size_t)k].id == slotId) return {p.id, k, n};
      i += (size_t)(n - 1);  // consume the group's component ports
      continue;
    }
    if (p.id == slotId) return g;  // scalar: its own group of 1
  }
  return g;  // unknown slot: behaves like a scalar (projection falls back to index 0)
}

}  // namespace sw
