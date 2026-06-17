// SphereSDF field op (field self-registration seam leaf — the proving SDF for the shader-graph
// island). TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs (+ .t3
// defaults). The codegen MECHANISM (preShaderCode / collectParams / param packing) lives in
// runtime/field_graph (SphereSDFNode) and is already pinned by --selftest-field-codegen. This leaf
// is the OP layer: a NodeSpec (so SphereSDF appears in the Add menu / findSpec like any op) plus a
// FieldNodeFactory (so a graph walk can instantiate a SphereSDFNode by type name), registered via
// the FieldOp self-registration seam — adding a field op = this one .cpp, no shared list edited.
//
//   SphereSDF.cs inputs:  Center (Vector3, [GraphParam]),  Radius (float, [GraphParam]).
//   SphereSDF.t3 defaults: Center = (0,0,0), Radius = 0.5  (mirrored in SphereSDFNode's ctor).
//   GetPreShaderCode: f{c}.w = length(p{c}.xyz - {n}Center) - {n}Radius;  (the distance formula).
//
// Param order parity: Center (vec3) THEN Radius (scalar) — SphereSDFNode::collectParams emits them
// in that reflection order, and the Inspector ports below follow it (Center.x/.y/.z as a Vec3 head
// run, then Radius). Center=4-float-aligned + Radius = exactly one 16B slot, no padding (pinned in
// the codegen golden).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>

#include "runtime/field_graph.h"          // SphereSDFNode, FieldNode
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

NodeSpec sphereSdfSpec() {
  NodeSpec s;
  s.type = "SphereSDF";
  s.title = "Sphere SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0). Radius = scalar Float, default 0.5.
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  PortSpec r; r.id = "Radius"; r.name = "Radius"; r.dataType = "Float"; r.isInput = true;
  r.def = 0.5f; r.minV = 0.0f; r.maxV = 10.0f;
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, r, out};
  return s;
}

// Factory: build a SphereSDFNode for an instance. Center/Radius default to the .t3 values; a graph
// cook would override them from the node's params before assembly (Build-2 golden sets them directly).
std::shared_ptr<FieldNode> makeSphereSdf(const std::string& shortId) {
  return std::make_shared<SphereSDFNode>(shortId);
}

const FieldOp g_sphereSdfOp(sphereSdfSpec(), makeSphereSdf);

}  // namespace
}  // namespace sw
