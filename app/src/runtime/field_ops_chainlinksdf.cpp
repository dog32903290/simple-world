// ChainLinkSDF field op (field self-registration seam leaf — a clean Phase-C fan-out on the proven
// shader-graph island). TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/ChainLinkSDF.cs
// (+ .t3 defaults). Like SphereSDF this single leaf owns BOTH halves of one SDF op: the codegen NODE
// (ChainLinkSDFNode below) AND the OP layer (NodeSpec for the Add menu / findSpec + a FieldNodeFactory
// so a graph walk can instantiate it by type name), registered via the FieldOp self-registration seam.
// The codegen base machinery (FieldNode interface, assembleFieldMSL, param packing) stays FROZEN in
// runtime/field_graph — so adding this field op = this one .cpp + one CMakeLists line, no shared edit.
//
//   ChainLinkSDF.cs inputs:  Center (Vector3, [GraphParam]), Length (float), Size (float),
//                            Thickness (float)  — all [GraphParam], in that reflection order.
//   ChainLinkSDF.t3 defaults: Center=(0,0,0), Length=0.5, Size=0.5, Thickness=0.25.
//   GetPreShaderCode: registers a GLOBAL helper `fChainLink` and calls it:
//     f{c}.w = fChainLink(p{c}.xyz - {n}Center, {n}Length, {n}Size, {n}Thickness);
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//   ARG MAPPING (load-bearing): le=Length, r1=Size, r2=Thickness — exactly the .cs call order.
//
// GLOBALS PATH (unlike SphereSDF, which was fully inline): this op emits a reusable helper via
// addGlobals (key "fChainLink"). VERIFIED wired: field_graph.cpp collectEmbeddedShaderCode calls
// node.addGlobals(cac) on every node, and assembleFieldMSL collects cac.globals (de-duped by map key)
// into the /*{GLOBALS}*/ hook. So the helper lands in the compiled MSL once, regardless of how many
// ChainLink nodes share the graph.
//
// Param order parity: Center (vec3) THEN Length, Size, Thickness (scalars) — collectParams emits them
// in that reflection order. Layout (padForVec3 handles vec3 alignment): Center=floats[0..2] as one
// packed_float3, Length=[3], Size=[4], Thickness=[5] = 6 floats, no padding (Center starts at index 0,
// not straddling a 16B boundary; the trailing 3 scalars pack tight after it).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- ChainLinkSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) -------------

// Distance-to-chain-link field leaf. Parity: ChainLinkSDF.cs GetPreShaderCode + ChainLinkSDF.t3
// defaults (Center=(0,0,0), Length=0.5, Size=0.5, Thickness=0.25). Params collected in
// field-declaration order (Center, Length, Size, Thickness), matching TiXL reflection order.
struct ChainLinkSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float length = 0.5f;
  float size = 0.5f;
  float thickness = 0.25f;

  explicit ChainLinkSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix.
    prefix = "ChainLinkSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/ChainLinkSDF.cs:30-37
    //   c.Globals["fChainLink"] = "float fChainLink(float3 p, float le, float r1, float r2) {...}".
    // De-duped by map key "fChainLink": multiple ChainLink nodes emit ONE copy into /*{GLOBALS}*/.
    // The helper body is identical text in HLSL and MSL (float3/float2/length/max/abs are common).
    c.globals["fChainLink"] =
        "float fChainLink(float3 p, float le, float r1, float r2) {\n"
        "  float3 q = float3( p.x, max(abs(p.y)-le,0.0), p.z );\n"
        "  return length(float2(length(q.xy)-r1,q.z)) - r2;\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/ChainLinkSDF.cs:39-41
    //   c.AppendCall($"f{c}.w = fChainLink(p{c}.xyz - {n}Center, {n}Length, {n}Size, {n}Thickness);");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // ARG MAPPING (load-bearing): le=Length, r1=Size, r2=Thickness (the .cs call order).
    //
    // HLSL->MSL FORK (named, same as SphereSDF): TiXL reads cbuffer-global param names `{n}Length`;
    // in MSL the params live in `constant FieldParams& P`, so every read is qualified `P.{n}Length`.
    // The distance MATH is byte-identical; only the cbuffer-vs-struct access syntax differs.
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = fChainLink(p" + ctx + ".xyz - P." + prefix + "Center, P." + prefix +
                 "Length, P." + prefix + "Size, P." + prefix + "Thickness);");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // Field-declaration order = Center (vec3), Length, Size, Thickness (scalars), matching
    // ChainLinkSDF.cs reflection order. padForVec3 (inside appendVec3Param) owns the vec3 alignment;
    // never hand-roll pads. Center starts at index 0 (no pad) -> floats[0..2]; Length/Size/Thickness
    // pack tight at [3]/[4]/[5] = 6 floats total.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "Length", length);
    appendScalarParam(floatParams, paramFields, prefix + "Size", size);
    appendScalarParam(floatParams, paramFields, prefix + "Thickness", thickness);
  }
};

NodeSpec chainLinkSdfSpec() {
  NodeSpec s;
  s.type = "ChainLinkSDF";
  s.title = "Chain Link SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0). Length/Size/Thickness = scalar Floats with the
  // .t3 defaults (0.5 / 0.5 / 0.25). min/max are sensible authoring ranges (>=0 for the radii/length).
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  PortSpec le; le.id = "Length"; le.name = "Length"; le.dataType = "Float"; le.isInput = true;
  le.def = 0.5f; le.minV = 0.0f; le.maxV = 10.0f;
  PortSpec sz; sz.id = "Size"; sz.name = "Size"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 0.5f; sz.minV = 0.0f; sz.maxV = 10.0f;
  PortSpec th; th.id = "Thickness"; th.name = "Thickness"; th.dataType = "Float"; th.isInput = true;
  th.def = 0.25f; th.minV = 0.0f; th.maxV = 10.0f;
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, le, sz, th, out};
  return s;
}

// Factory: build a ChainLinkSDFNode for an instance. Params default to the .t3 values; a graph cook
// would override them from the node's params before assembly (the golden uses the defaults directly).
std::shared_ptr<FieldNode> makeChainLinkSdf(const std::string& shortId) {
  return std::make_shared<ChainLinkSDFNode>(shortId);
}

// PF-0c param-apply (WAVE 2): project a RESOLVED param map onto a ChainLinkSDFNode via setter-lambdas
// (NOT offsetof). Slot ids EQUAL the NodeSpec PortSpec.id (Center.x/.y/.z, Length, Size, Thickness). A
// missing key keeps the member's ctor .t3 default (applyFloatSlot's contract). Routed via the
// fieldConfigurers() table.
void configureChainLinkSdfFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<ChainLinkSDFNode*>(&node)) {
    applyFloatSlot(m, "Center.x", [&](float v) { n->centerX = v; });
    applyFloatSlot(m, "Center.y", [&](float v) { n->centerY = v; });
    applyFloatSlot(m, "Center.z", [&](float v) { n->centerZ = v; });
    applyFloatSlot(m, "Length", [&](float v) { n->length = v; });
    applyFloatSlot(m, "Size", [&](float v) { n->size = v; });
    applyFloatSlot(m, "Thickness", [&](float v) { n->thickness = v; });
  }
}

// slot ids = the SAME ids configureChainLinkSdfFromParams applies (Option B guard reads them, can't drift).
const FieldOp g_chainLinkSdfOp(chainLinkSdfSpec(), makeChainLinkSdf, configureChainLinkSdfFromParams,
                               {"Center.x", "Center.y", "Center.z", "Length", "Size", "Thickness"});

}  // namespace
}  // namespace sw
