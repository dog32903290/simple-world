// OctahedronSDF field op (zero-shared-file leaf on the field self-registration seam). Like SphereSDF
// this single .cpp owns BOTH halves of one SDF op: the codegen NODE (OctahedronSDFNode below) AND the
// OP layer (NodeSpec for the Add menu / findSpec + a FieldNodeFactory so a graph walk can instantiate
// it by type name), registered via the file-scope FieldOp registrar. The base machinery (FieldNode
// interface, assembleFieldMSL, param packing) stays FROZEN in runtime/field_graph — adding a field op
// = this one .cpp + one CMakeLists line, no shared file edited.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/OctahedronSDF.cs
//   inputs (reflection / [GraphParam] order): Center (Vector3), Size (float), EdgeRadius (float).
//   .t3 defaults: Center = (0,0,0), Size = 0.5, EdgeRadius = 0.002 (mirrored in the ctor below).
//   AddDefinitions: registers Globals["fsdOctahedron"] (the helper fn) — UNLIKE SphereSDF which is
//     fully inline; this op emits a GLOBAL helper via addGlobals (the field_graph globals path is
//     wired: collectEmbeddedShaderCode calls node.addGlobals, and assembleFieldMSL injects ctx.globals
//     into the /*{GLOBALS}*/ hook — verified before writing this leaf).
//   GetPreShaderCode:
//     f{c}.w = fsdOctahedron(p{c}.xyz, {n}Center, {n}Size, {n}EdgeRadius);
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//
// Param order parity: Center (vec3) -> Size (scalar) -> EdgeRadius (scalar), emitted in that
// declaration order by collectParams. Layout: Center=floats[0..2] (packed_float3, offset 0 so
// padForVec3 adds no padding), Size=floats[3], EdgeRadius=floats[4] — 5 floats total, same tight
// packed_float3 + trailing scalars layout SphereSDF uses (HLSL cbuffer parity via packed_float3).
//
// HLSL->MSL forks honored: (1) the same packed_float3 alignment fork as SphereSDF (handled by
// appendVec3Param). (2) the cbuffer-vs-struct param access: TiXL reads bare `{n}Name` from a global
// cbuffer; MSL reads `P.{n}Name` from the `constant FieldParams& P` argument — we emit the `P.`
// prefix here. The helper body math (and sign()/clamp(vec,scalar,scalar), which exist identically in
// MSL) is byte-identical to the .cs — NO math fork.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- OctahedronSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) ------------

// Distance-to-octahedron field leaf. Parity: OctahedronSDF.cs AddDefinitions + GetPreShaderCode +
// .t3 defaults (Center=(0,0,0), Size=0.5, EdgeRadius=0.002). Params collected in field-declaration
// order (Center, Size, EdgeRadius) exactly like TiXL reflection order on [GraphParam] fields.
struct OctahedronSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float size = 0.5f;
  float edgeRadius = 0.002f;

  explicit OctahedronSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix (same shape as
    // SphereSDFNode's "SphereSDF_<shortId>_").
    prefix = "OctahedronSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/OctahedronSDF.cs:37-48
    //   c.Globals["fsdOctahedron"] = "float fsdOctahedron(...) { ... }";
    // De-duped by key: std::map::operator[] overwrites with identical text if two OctahedronSDF nodes
    // are present, so the helper appears exactly once in the assembled shader (TiXL Globals semantics).
    // Body is byte-identical to the .cs (sign(), clamp(vec,scalar,scalar), min(vec,scalar), abs() all
    // exist identically in MSL — no math fork).
    c.globals["fsdOctahedron"] =
        "float fsdOctahedron(float3 p, float3 center, float s, float ra) {\n"
        "    p -= center;\n"
        "    p = abs(p);\n"
        "    float m = (p.x + p.y + p.z - s) / 3.0;\n"
        "    float3 o = p - m;\n"
        "    float3 k = min(o, 0.0);\n"
        "    o = o + (k.x + k.y + k.z) * 0.5 - k * 1.5;\n"
        "    o = clamp(o, 0.0, s);\n"
        "    return length(p - o) * sign(m) - ra;\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY OctahedronSDF.cs:53-54
    //   c.AppendCall($"f{c}.w = fsdOctahedron(p{c}.xyz, {n}Center, {n}Size, {n}EdgeRadius);");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;");
    // {n} = node prefix (qualified with P. for MSL struct access); {c} = context id.
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = fsdOctahedron(p" + ctx + ".xyz, P." + prefix + "Center, P." +
                 prefix + "Size, P." + prefix + "EdgeRadius);");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // Field-declaration order = Center (vec3) -> Size (scalar) -> EdgeRadius (scalar), matching
    // OctahedronSDF.cs reflection order on [GraphParam] fields. appendVec3Param/padForVec3 own the
    // vec3 alignment (Center at offset 0 -> no padding); Center(3)+Size(1)+EdgeRadius(1) = 5 floats.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "Size", size);
    appendScalarParam(floatParams, paramFields, prefix + "EdgeRadius", edgeRadius);
  }
};

NodeSpec octahedronSdfSpec() {
  NodeSpec s;
  s.type = "OctahedronSDF";
  s.title = "Octahedron SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0). Size, EdgeRadius = scalar Floats.
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  PortSpec sz; sz.id = "Size"; sz.name = "Size"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 0.5f; sz.minV = 0.0f; sz.maxV = 10.0f;
  PortSpec er; er.id = "EdgeRadius"; er.name = "EdgeRadius"; er.dataType = "Float"; er.isInput = true;
  er.def = 0.002f; er.minV = 0.0f; er.maxV = 10.0f;
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, sz, er, out};
  return s;
}

// Factory: build an OctahedronSDFNode for an instance. Center/Size/EdgeRadius default to the .t3
// values; a graph cook would override them from the node's params before assembly.
std::shared_ptr<FieldNode> makeOctahedronSdf(const std::string& shortId) {
  return std::make_shared<OctahedronSDFNode>(shortId);
}

const FieldOp g_octahedronSdfOp(octahedronSdfSpec(), makeOctahedronSdf);

}  // namespace
}  // namespace sw
