// runtime/node_registry_attr_cluster — the shared per-point ATTRIBUTE NodeSpec cluster appended by
// every point GENERATOR (param-completion fan-out). Split from node_registry_generators.cpp to keep
// that file under the line-count ratchet (ARCHITECTURE rule 4). Decl in node_registry_generators.h.
#include "runtime/node_registry_generators.h"

namespace sw {

// Color(Vec4) + OrientationAxis(Vec3) + OrientationAngle — spelled exactly as RadialPoints/GridPoints
// did inline, extracted so the fan-out writes the cluster once. APPEND-ONLY (pin ids are port-INDEX
// based; these MUST stay at the tail of each spec). `axisDefault` = this node's .t3 OrientationAxis.
void appendPointOrientationSpec(std::vector<PortSpec>& ports, const float axisDefault[3]) {
  // Color (Vector4) — per-point color. .t3 default white on both nodes.
  ports.push_back({"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4});
  ports.push_back({"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1});
  ports.push_back({"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1});
  ports.push_back({"Color.w", "Color.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1});
  // OrientationAxis (Vector3) — the .t3 default differs per node, so it is a parameter (caller passes).
  ports.push_back({"OrientationAxis.x", "OrientationAxis", "Float", true, axisDefault[0], -1.0f, 1.0f, Widget::Vec, {}, true, 3});
  ports.push_back({"OrientationAxis.y", "OrientationAxis.y", "Float", true, axisDefault[1], -1.0f, 1.0f, Widget::Vec, {}, true, 1});
  ports.push_back({"OrientationAxis.z", "OrientationAxis.z", "Float", true, axisDefault[2], -1.0f, 1.0f, Widget::Vec, {}, true, 1});
  // OrientationAngle (degrees). .t3 default 0.0.
  ports.push_back({"OrientationAngle", "OrientationAngle", "Float", true, 0.0f, -360.0f, 360.0f});
}

}  // namespace sw
