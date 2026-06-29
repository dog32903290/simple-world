// runtime/node_registry_generators — NodeSpec sub-table for point GENERATOR ops.
// Split from node_registry.cpp (批次16-R). Included by node_registry.cpp builder.
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& generatorSpecs();

// ---- common point-attribute NodeSpec cluster (param-completion fan-out) -------------------
// Every TiXL point GENERATOR ends its [Input] list with the same "per-point attribute" knobs:
// Color (Vector4) + OrientationAxis (Vector3) + OrientationAngle (float). RadialPoints and
// GridPoints both spell these the SAME way (.t3 defaults: Color=white; RadialPoints Axis=+Z,
// GridPoints Axis=+X — the default differs per node, so it is a parameter, not baked here).
// This builder appends those ports to a node's port list so the cluster is written once and the
// integrity gate sees the same shape on every generator. APPEND-ONLY: pin ids are port-INDEX
// based, so these MUST stay at the tail of the spec (re-ordering would re-target saved wires).
// `axisDefault` = the .t3 OrientationAxis default for THIS node (e.g. {0,0,1} or {1,0,0}).
void appendPointOrientationSpec(std::vector<PortSpec>& ports, const float axisDefault[3]);
}  // namespace sw
