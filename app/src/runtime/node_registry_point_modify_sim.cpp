// runtime/node_registry_point_modify_sim — Sim-family offset modifiers (TiXL point/sim).
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

// ---- batch sw-node-batch (lane point_modify, sim family): SimNoiseOffset --------------
// TiXL parity: external/tixl .../point/sim/SimNoiseOffset.cs + .hlsl
// Count-preserving MODIFIER: (simplex|curl)-noise position displacement + tangent rotation.
// Defaults from SimNoiseOffset.t3: Amount=0.2, Frequency=1.0, Phase=0.0, Variation=0.0,
//   AmountDistribution=(1,1,1), RotLookupDistance=2.0, UseCurlNoise=true.
static const PointModifyOp _reg_SimNoiseOffset{
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
       nullptr}
};

// ---- batch sw-node-batch (lane point_modify, sim family): SimCentricalOffset ----------
// TiXL parity: external/tixl .../point/sim/SimCentricalOffset.cs + .hlsl
// Count-preserving MODIFIER: radial inverse-power force along (Position-Center); positive
// Amount pushes outward. Defaults from .t3: Center=(0,0,0), MaxAcceleration=1.0,
//   Amount(->Acceleration)=0.04, DecayExponent=2.0. (.cs ShowGizmo omitted.)
static const PointModifyOp _reg_SimCentricalOffset{
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
       nullptr}
};

// ---- batch sw-node-batch (lane point_modify, sim family): SimDirectionalOffset --------
// TiXL parity: external/tixl .../point/sim/SimDirectionalOffset.cs + .hlsl
// Count-preserving MODIFIER: directional position push (Mode 0) / velocity-in-rotation
// encode (Mode 1). Defaults from .t3: Direction=(0,0.01,0), Amount=1.0, RandomAmount=0.0,
//   Mode=0 (Legacy). (.cs ShowGizmo omitted.)
static const PointModifyOp _reg_SimDirectionalOffset{
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
       nullptr}
};

// ---- batch sw-node-batch (lane point_modify, sim family): SimForceOffset --------------
// TiXL parity: external/tixl .../point/sim/SimForceOffset.cs + .hlsl
// Count-preserving MODIFIER: radial force + gravity gated by a radius/falloff window.
// Defaults from .t3: Center=(0,0,0), Radius=999.0, RadiusFallOff=0.0, RadialForce=0.0,
//   UseWForMass=0.0, Variation=0.0, Gravity=(0,0,0), ForceDecayRate=1.0. (.cs IsEnabled omitted.)
static const PointModifyOp _reg_SimForceOffset{
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
       nullptr}
};

}  // namespace
}  // namespace sw
