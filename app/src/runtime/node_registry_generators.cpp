// runtime/node_registry_generators — NodeSpec table for point GENERATOR ops (no input bag).
// Generators are ops that produce a new point bag from scratch: RadialPoints, LinePoints,
// GridPoints, SpherePoints.  Split out of node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
// Phase B parallel lanes add NEW families in separate files — this file is generators only.
#include "runtime/node_registry_generators.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& generatorSpecs() {
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
        // sizes the bag from this single "Count" port). CountX/Y/Z below are the real grid dims.
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
      // ---- batch 19: point generate — HexGridPoints --------------------------------
      // TiXL parity: external/tixl .../point/generate/HexGridPoints.cs + .hlsl (Pattern=2 Hexa)
      // A GENERATOR op: no input bag. Writes a hexagonal tiling grid of SwPoints.
      // Math: same cell->clampedCount->zeroAdjustedSize->SizeMode base as GridPoints,
      // then applies hex X-offset = HexOffsetsAndAngles[hexAttrIndex].x * sizeX * 0.3333
      // and rescales pos.x *= 0.578 * 3 (HexScale), with per-cell rotation rotDelta.
      // Defaults: CountX=4, CountY=4, CountZ=1, Size=(1,1,1), Center=(0,0,0),
      //   W=1.0, OrientationAxis=(0,1,0), OrientationAngle=0, Pivot=(0,0,0), SizeMode=Cell.
      // NOTE: Count = buffer CAPACITY = CountX * CountY * CountZ (host responsibility).
      // FORK: Pattern baked to 2 (Hexa); Triangular (1) and default (3) branches deferred.
      //       Color baked white; Scale baked 1.
      {"HexGridPoints",
       "HexGridPoints",
       {{"points", "points", "Points", false},
        // Count = output buffer capacity (host sets = CountX*CountY*CountZ)
        {"Count", "Count", "Float", true, 16.0f, 1.0f, 65536.0f},
        {"CountX", "CountX", "Float", true, 4.0f, 1.0f, 256.0f, Widget::Slider},
        {"CountY", "CountY", "Float", true, 4.0f, 1.0f, 256.0f, Widget::Slider},
        {"CountZ", "CountZ", "Float", true, 1.0f, 1.0f, 256.0f, Widget::Slider},
        {"SizeMode", "SizeMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Cell", "Bounds"}},
        // Size (TiXL Vector3) — per-axis cell extent
        {"Size.x", "Size", "Float", true, 1.0f, 0.01f, 10.0f, Widget::Vec, {}, true, 3},
        {"Size.y", "Size.y", "Float", true, 1.0f, 0.01f, 10.0f, Widget::Vec, {}, true, 1},
        {"Size.z", "Size.z", "Float", true, 1.0f, 0.01f, 10.0f, Widget::Vec, {}, true, 1},
        // Center (TiXL Vector3) — global translation
        {"Center.x", "Center", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3},
        {"Center.y", "Center.y", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Center.z", "Center.z", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1},
        // Pivot (TiXL Vector3) — grid anchor offset
        {"Pivot.x", "Pivot", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 3},
        {"Pivot.y", "Pivot.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"Pivot.z", "Pivot.z", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
        {"W", "W", "Float", true, 1.0f, 0.0f, 1.0f},
        // OrientationAxis (TiXL Vector3, default 0,1,0) + OrientationAngle (degrees)
        {"OrientationAxis.x", "OrientationAxis", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"OrientationAxis.y", "OrientationAxis.y", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OrientationAxis.z", "OrientationAxis.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"OrientationAngle", "OrientationAngle", "Float", true, 0.0f, -360.0f, 360.0f}},
       nullptr},
  };
  return specs;
}

}  // namespace sw
