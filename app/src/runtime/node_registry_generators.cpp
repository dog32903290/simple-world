// runtime/node_registry_generators — NodeSpec table for point GENERATOR ops (no input bag).
// Generators are ops that produce a new point bag from scratch: RadialPoints, LinePoints,
// GridPoints, SpherePoints.  Split out of node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
// Phase B parallel lanes add NEW families in separate files — this file is generators only.
#include "runtime/node_registry_generators.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& generatorSpecs() {
  // The four CORE generators are spelled inline here; the remaining families live in
  // node_registry_generators_extra.cpp (rule-4 split). We build the combined table ONCE into
  // a function-local static so callers keep the single flat vector + stable order (core, then
  // extra appended verbatim — identical to the pre-split layout).
  static const std::vector<NodeSpec> specs = [] {
    std::vector<NodeSpec> core = {
      {"RadialPoints",
       "RadialPoints",
       [] { std::vector<PortSpec> p = {{"points", "points", "Points", false},
        // Defaults照 RadialPoints.t3: Count=100 (.t3:69), Radius=1.0 (.t3:65).
        {"Count", "Count", "Float", true, 100.0f, 16.0f, 8192.0f},
        {"Radius", "Radius", "Float", true, 1.0f, 0.1f, 10.0f},
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
        {"Cycles", "Cycles", "Float", true, 1.0f, 0.0f, 10.0f},
        // PARAM-COMPLETION GATE: the 10 TiXL inputs that were baked in the kernel are now real
        // inspector knobs. ALL APPENDED (pin ids are port-INDEX based; inserting would re-target
        // saved wires — same hazard the Center comment above flags). Defaults cite RadialPoints.t3.
        // Axis (Vector3) — spiral axis. .t3:45 default (0,0,1).
        {"Axis.x", "Axis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Axis.y", "Axis.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Axis.z", "Axis.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        // OffsetCenter (Vector3) — Center += OffsetCenter·f. .t3:99 default (0,0,0).
        {"OffsetCenter.x", "OffsetCenter", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"OffsetCenter.y", "OffsetCenter.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"OffsetCenter.z", "OffsetCenter.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // GainAndBias (Vector2) — remaps f. .t3:92 default (0.5,0.5) = identity.
        {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // CloseCircleLine (bool) — angleStepCount=count-2 + NaN terminator. .t3:53 default false.
        {"CloseCircleLine", "CloseCircleLine", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        // Scale (PointScaleRange, Vector2) — Scale = x + y·f. .t3:73 default (1,0).
        {"Scale.x", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Scale.y", "Scale.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // F1 (Vector2) — FX1 = x + y·f. .t3:18 default (1,0)  ★not (0,0).
        {"F1.x", "F1", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"F1.y", "F1.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // F2 (Vector2) — FX2 = x + y·f. .t3:29 default (1,0)  ★not (0,0).
        {"F2.x", "F2", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"F2.y", "F2.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        };
        // Color(Vec4) + OrientationAxis(Vec3) + OrientationAngle — the shared attribute cluster
        // (appendPointOrientationSpec, also used by GridPoints). RadialPoints.t3 OrientationAxis
        // default (0,0,1). Byte-identical to the prior inline rows → pin indices unchanged.
        const float axisDefault[3] = {0.0f, 0.0f, 1.0f};
        appendPointOrientationSpec(p, axisDefault);
        // OrientationMode (enum). .t3:61 default 0 (Classic) — RadialPoints-specific, appended after
        // the shared cluster so the indices of every prior port stay put.
        p.push_back({"OrientationMode", "OrientationMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
                     {"Classic", "AlignedToCurvature"}});
        return p; }(),
       nullptr,
       "point.generate"},
      {"LinePoints",
       "LinePoints",
       // PARAM-COMPLETION GATE: LinePoints.cs has 18 [Input]s; the shape/distribution params
       // (Count/Length/Pivot/Center/Direction/GainAndBias/Scale = 7 logical) were already wired.
       // This fan-out adds the remaining 11 (F1, F2, ColorA, ColorB, Orientation, Twist,
       // OrientationAxis, OrientationAngle, AddSeparator, W, WOffset) so the inspector covers the
       // .cs 1:1. APPEND-ONLY (pin ids are port-INDEX based; inserting re-targets saved wires).
       // Defaults cite LinePoints.t3. NOTE: LinePoints has DUAL color (ColorA/ColorB) so the
       // shared appendPointOrientationSpec helper (single Color) does NOT fit — ColorA/ColorB and
       // the OrientationAxis/Angle tail are spelled inline here.
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
        // Scale (TiXL Vector2 / PointSize) — base + per-index scale. .t3 default (1,0).
        {"Scale.x", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"Scale.y", "Scale.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // ---- param-completion fan-out: the 11 baked TiXL inputs now exposed --------------------
        // F1 (Vector2) — FX1 = x + y·f1. .t3 default (1,0)  ★not (0,0).
        {"F1.x", "F1", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"F1.y", "F1.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // F2 (Vector2) — FX2 = x + y·f1. .t3 default (1,0)  ★not (0,0).
        {"F2.x", "F2", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 2},
        {"F2.y", "F2.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // ColorA / ColorB (Vector4) — per-point color = lerp(ColorA, ColorB, f1). .t3 default white both.
        {"ColorA.x", "ColorA", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ColorA.y", "ColorA.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ColorA.z", "ColorA.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ColorA.w", "ColorA.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ColorB.x", "ColorB", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"ColorB.y", "ColorB.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ColorB.z", "ColorB.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"ColorB.w", "ColorB.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        // Orientation (enum, MappedType OrientationModes). .t3 default 1 = Simple (NOT UsingUpVector).
        {"Orientation", "Orientation", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
         {"UsingUpVector", "Simple"}},
        // Twist (degrees) — added to OrientationAngle, ramped by f. .t3 default 0.0.
        {"Twist", "Twist", "Float", true, 0.0f, -360.0f, 360.0f},
        // OrientationAxis (Vector3, Simple-mode rotation axis). .t3 default (0,0,1).
        {"OrientationAxis.x", "OrientationAxis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"OrientationAxis.y", "OrientationAxis.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OrientationAxis.z", "OrientationAxis.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        // OrientationAngle (degrees). .t3 default 0.0.
        {"OrientationAngle", "OrientationAngle", "Float", true, 0.0f, -360.0f, 360.0f},
        // AddSeparator (bool) — last point gets NaN scale (a line terminator) + shrinks the step span
        // by 1 so the visible points still span Length. .t3 default false.
        {"AddSeparator", "AddSeparator", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        // W / WOffset (Single) — declared [Input] in LinePoints.cs but UNUSED by the .hlsl (the
        // cbuffer fields AND the ResultPoints[i].W write are commented out there). Faithful-dead
        // knobs, exposed for 1:1 [Input] parity. .t3 defaults W=1.0, WOffset=0.0.
        {"W", "W", "Float", true, 1.0f, -100.0f, 100.0f},
        {"WOffset", "WOffset", "Float", true, 0.0f, -100.0f, 100.0f}},
       nullptr,
       "point.generate"},
      {"GridPoints",
       "GridPoints",
       // PARAM-COMPLETION GATE: GridPoints.cs has 16 [Input]s. Shape params were already wired; this
       // fan-out adds the 8 baked TiXL inputs (Scale, Tiling, F1, F2, Color, OrientationAxis,
       // OrientationAngle, W) so the inspector covers the .cs 1:1. Color/Orientation tail = the shared
       // appendPointOrientationSpec helper. APPEND-ONLY (pin ids are index-based). Defaults cite .t3.
       [] {
         std::vector<PortSpec> p = {
             {"points", "points", "Points", false},
             // Count = output buffer CAPACITY (host sets = CountX*CountY*CountZ). CountX/Y/Z = grid dims.
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
             {"PointScale", "PointScale", "Float", true, 1.0f, 0.0f, 5.0f, Widget::Slider},
             // ---- param-completion fan-out: the 8 baked TiXL inputs now exposed --------------
             // Scale (.t3 default 0.1) — UNIFORM Size multiplier (.t3 ScaleVector3: shader Size = Size·Scale).
             {"Scale", "Scale", "Float", true, 0.1f, 0.0f, 10.0f, Widget::Slider},
             // Tiling (.t3 default 0=Cartesian). Other 3 branches still baked (Cartesian-only fork); knob for parity.
             {"Tiling", "Tiling", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
              {"Cartesian", "Triangular", "HoneyCombs", "Diagonal"}},
             // F1/F2 (TiXL Single → SwPoint.FX1/FX2). .t3 default 1.0 each (★not 0 — old kernel baked 0).
             {"F1", "F1", "Float", true, 1.0f, -10.0f, 10.0f},
             {"F2", "F2", "Float", true, 1.0f, -10.0f, 10.0f},
         };
         const float axisDefault[3] = {1.0f, 0.0f, 0.0f};  // GridPoints.t3 OrientationAxis default (1,0,0)
         appendPointOrientationSpec(p, axisDefault);        // Color + OrientationAxis + OrientationAngle
         // W (TiXL Single, .t3 default 1.0) — declared [Input] in GridPoints.cs but UNUSED by the .hlsl
         // (no cbuffer field / no FloatsToBuffer wire). Faithful-dead knob, exposed for 1:1 [Input] parity.
         p.push_back({"W", "W", "Float", true, 1.0f, -100.0f, 100.0f});
         return p;
       }(),
       nullptr,
       "point.generate"},
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
       nullptr,
       "point.generate"},
    };
    // Append the extra-family generators verbatim — preserves the pre-split flat order.
    const std::vector<NodeSpec>& extra = generatorSpecsExtra();
    core.insert(core.end(), extra.begin(), extra.end());
    return core;
  }();
  return specs;
}

}  // namespace sw
