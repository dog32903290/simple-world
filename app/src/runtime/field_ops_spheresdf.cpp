// SphereSDF field op (field self-registration seam leaf — the proving SDF for the shader-graph
// island). TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs (+ .t3
// defaults). This single leaf owns BOTH halves of one SDF op: the codegen NODE (SphereSDFNode below,
// the preShaderCode / collectParams mechanism, pinned by --selftest-field-codegen) AND the OP layer
// (a NodeSpec so it appears in the Add menu / findSpec, plus a FieldNodeFactory so a graph walk can
// instantiate it by type name), registered via the FieldOp self-registration seam. The codegen base
// machinery (FieldNode interface, assembleFieldMSL, param packing) stays in runtime/field_graph and
// is FROZEN — so adding a field op = this one .cpp + one CMakeLists line, no shared file edited.
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
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- SphereSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) ----------------

// Distance-to-sphere field leaf. Parity: SphereSDF.cs GetPreShaderCode +
// SphereSDF.t3 defaults (Center=(0,0,0), Radius=0.5). Params collected in field-declaration order
// (Center first, Radius second) exactly like TiXL reflection order on [GraphParam] fields.
struct SphereSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float radius = 0.5f;

  explicit SphereSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix.
    prefix = "SphereSDF_" + shortId + "_";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs:35-36
    //   c.AppendCall($"f{c}.w = length(p{c}.xyz - {n}Center) - {n}Radius;");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // {n} = node prefix; {c} = context id. `length`, `.xyz`, `float4` are common HLSL/MSL syntax.
    //
    // HLSL->MSL FORK (named): in TiXL the params live in a global-scope HLSL cbuffer, so the snippet
    // reads them as a bare name `{n}Center`. In MSL they live inside the `constant FieldParams& P`
    // argument, so every param read must be qualified `P.{n}Center`. We emit the `P.` prefix here.
    // The distance MATH (length(p.xyz - Center) - Radius) is byte-identical; only the cbuffer-vs-struct
    // access syntax differs — this is the load-bearing HLSL->MSL handoff for Build-2.
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz - P." + prefix + "Center) - P." + prefix +
                 "Radius;");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // Field-declaration order = Center (vec3) then Radius (scalar), matching SphereSDF.cs reflection
    // order on [GraphParam] fields. Center(3) + Radius(1) = 4 floats = one 16B slot, no padding.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "Radius", radius);
  }
};

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

// PF-0c param-apply: project a RESOLVED param map onto a SphereSDFNode via setter-lambdas (NOT offsetof —
// SphereSDFNode is non-standard-layout). Slot ids EQUAL the NodeSpec PortSpec.id (Center.x/.y/.z, Radius).
// A missing key leaves the member at its ctor .t3 default (applyFloatSlot's contract) → byte-identical to
// a no-graph-param build. Routed here by configureFieldNodeFromParams via the fieldConfigurers() sink.
void configureSphereSdf(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<SphereSDFNode*>(&node)) {
    applyFloatSlot(m, "Center.x", [&](float v) { n->centerX = v; });
    applyFloatSlot(m, "Center.y", [&](float v) { n->centerY = v; });
    applyFloatSlot(m, "Center.z", [&](float v) { n->centerZ = v; });
    applyFloatSlot(m, "Radius", [&](float v) { n->radius = v; });
  }
}

const FieldOp g_sphereSdfOp(sphereSdfSpec(), makeSphereSdf, configureSphereSdf);

}  // namespace
}  // namespace sw
