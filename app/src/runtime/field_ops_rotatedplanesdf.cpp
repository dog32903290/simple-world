// RotatedPlaneSDF field op (zero-shared-file leaf on the field self-registration seam). Like SphereSDF
// / PlaneSDF this single .cpp owns BOTH halves of one SDF op: the codegen NODE (RotatedPlaneSDFNode
// below) AND the OP layer (a NodeSpec for the Add menu / findSpec + a FieldNodeFactory so a graph walk
// can instantiate it by type name), registered via the file-scope FieldOp registrar. The base
// machinery (FieldNode interface, assembleFieldMSL, param packing) stays FROZEN in runtime/field_graph
// — adding a field op = this one .cpp + one CMakeLists line, no shared file edited.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/RotatedPlaneSDF.cs (+ .t3 defaults).
//   inputs ([GraphParam] reflection order): Center (Vector3), Normal (Vector3). NO scalars, NO Axis
//   enum — UNLIKE PlaneSDF, the half-space orientation is an arbitrary Normal vector, not an
//   axis-aligned swizzle code, so there is NO compile-time code selector on this leaf.
//   .t3 defaults: Center = (0,0,0), Normal = (0,1,0). ★ Normal default MUST be (0,1,0): the snippet
//   feeds Normal through normalize(), and normalize((0,0,0)) is NaN — a zero Normal default would
//   poison every cooked texel. (0,1,0) -> normalize = (0,1,0) -> d = p.y, the clean +Y plane.
//   GetPreShaderCode (INLINE, no AddDefinitions / no global helper — like SphereSDF/PlaneSDF):
//     f{c}.w = dot(p{c}.xyz - {n}Center, normalize({n}Normal));
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//
// NO CODE SELECTOR: this leaf has no Axis/Sides enum. Both params are plain [GraphParam] vec3s packed
// into the float buffer; nothing on this node re-writes the emitted MSL text per-instance. (The Normal
// is normalized IN THE SHADER — we pack the RAW Normal, the normalize() lives in the call text. Packing
// a pre-normalized Normal would diverge from TiXL, which packs raw and normalizes in the snippet.)
//
// Param order parity (both [GraphParam] vec3s are packed): Center (vec3) -> Normal (vec3), emitted in
// that declaration order by collectParams. ★ TWO-CONSECUTIVE-VEC3 LAYOUT (confirmed against
// field_graph.cpp padForVec3): appendVec3Param("Center") on the empty buffer -> currentStart = 0 % 4 =
// 0 -> no padding -> Center = floats[0..2] (packed_float3, offset 0). The next appendVec3Param("Normal")
// -> currentStart = 3 % 4 = 3 -> requiredPadding = 1 -> ONE pad float at floats[3] -> Normal =
// floats[4..6]. Total 7 floats. Struct fields (in order): packed_float3 Center; float __padding4;
// packed_float3 Normal;. The pad is inserted AUTOMATICALLY by padForVec3 — we call appendVec3Param
// twice in order and never hand-roll the pad (the padding parity lives in ONE place in field_graph.cpp).
//
// HLSL->MSL forks honored: (1) the same packed_float3 alignment fork as the other SDF leaves (handled
// by appendVec3Param) — here it straddles two vec3s with the padForVec3 pad between them. (2) the
// cbuffer-vs-struct param access: TiXL reads bare `{n}Name` from a global cbuffer; MSL reads
// `P.{n}Name` from the `constant FieldParams& P` argument — we emit the `P.` prefix here. The distance
// MATH (dot of (p.xyz - Center) with normalize(Normal)) is byte-identical to the .cs — `dot`,
// `normalize`, `.xyz` are common HLSL/MSL syntax — NO math fork.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- RotatedPlaneSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) -----------

// Signed distance to an arbitrarily-oriented plane through Center with unit normal = normalize(Normal).
// Parity: RotatedPlaneSDF.cs GetPreShaderCode + .t3 defaults (Center=(0,0,0), Normal=(0,1,0)). Both
// params are [GraphParam] vec3s packed in declaration order (Center, Normal); there is NO code selector
// on this leaf (no Axis/Sides enum). Normal is normalized in the shader, packed RAW.
struct RotatedPlaneSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  // ★ Normal default (0,1,0) — NOT (0,0,0). normalize((0,0,0)) is NaN; (0,1,0) gives the +Y plane
  // (d = p.y), matching RotatedPlaneSDF.t3. Packed RAW; the normalize() is in the call text.
  float normalX = 0.f, normalY = 1.f, normalZ = 0.f;

  explicit RotatedPlaneSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix (same shape as the other
    // SDF leaves' "<Type>_<shortId>_").
    prefix = "RotatedPlaneSDF_" + shortId + "_";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/RotatedPlaneSDF.cs:40-43
    //   f{c}.w = dot(p{c}.xyz - {n}Center, normalize({n}Normal));
    //   f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;
    // {n} = node prefix (qualified P. for MSL struct access); {c} = context id. The normalize() is on
    // the RAW packed Normal — kept in the call text, NOT pre-applied to the packed value (TiXL packs
    // raw and normalizes in the snippet). `dot`/`normalize`/`.xyz` are byte-identical HLSL/MSL — the
    // only fork is the `P.` prefix on the two cbuffer-vs-struct param reads.
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = dot(p" + ctx + ".xyz - P." + prefix + "Center, normalize(P." +
                 prefix + "Normal));");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] declaration order = Center (vec3) -> Normal (vec3), matching RotatedPlaneSDF.cs
    // reflection order. TWO consecutive vec3s: appendVec3Param twice in order. padForVec3 inserts the
    // single pad float between them automatically (Center @ floats[0..2], pad @ floats[3], Normal @
    // floats[4..6]) — see the header layout note. NEVER hand-roll the pad.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendVec3Param(floatParams, paramFields, prefix + "Normal", normalX, normalY, normalZ);
  }
};

NodeSpec rotatedPlaneSdfSpec() {
  NodeSpec s;
  s.type = "RotatedPlaneSDF";
  s.title = "Rotated Plane SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0).
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  // Normal = Vec3 head run (.x/.y/.z), default (0,1,0). ★ The .y default is 1 (not 0) so normalize()
  // does not hit NaN — see header / node ctor. Normalized in the shader; the raw vec3 is packed.
  PortSpec nx; nx.id = "Normal.x"; nx.name = "Normal"; nx.dataType = "Float"; nx.isInput = true;
  nx.def = 0.0f; nx.minV = -10.0f; nx.maxV = 10.0f; nx.widget = Widget::Vec; nx.vecArity = 3;
  PortSpec ny; ny.id = "Normal.y"; ny.name = "Normal.y"; ny.dataType = "Float"; ny.isInput = true;
  ny.def = 1.0f; ny.minV = -10.0f; ny.maxV = 10.0f;
  PortSpec nz; nz.id = "Normal.z"; nz.name = "Normal.z"; nz.dataType = "Float"; nz.isInput = true;
  nz.def = 0.0f; nz.minV = -10.0f; nz.maxV = 10.0f;
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, nx, ny, nz, out};
  return s;
}

// Factory: build a RotatedPlaneSDFNode for an instance. Center/Normal default to the .t3 values (baked
// in the ctor); a graph cook would override them from the node's params before assembly (the golden
// uses the defaults directly — Center=(0,0,0), Normal=(0,1,0) -> d = p.y).
std::shared_ptr<FieldNode> makeRotatedPlaneSdf(const std::string& shortId) {
  return std::make_shared<RotatedPlaneSDFNode>(shortId);
}

// PF-0c param-apply: project a RESOLVED param map onto a RotatedPlaneSDFNode via setter-lambdas (NOT
// offsetof). Slot ids EQUAL the NodeSpec PortSpec.id (Center.x/.y/.z, Normal.x/.y/.z). Normal is packed RAW
// (the normalize() lives in the shader), so the apply sets the raw members untouched. A missing key keeps
// the member's ctor .t3 default (Normal default (0,1,0) preserved). Routed via the fieldConfigurers() table.
void configureRotatedPlaneSdf(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<RotatedPlaneSDFNode*>(&node)) {
    applyFloatSlot(m, "Center.x", [&](float v) { n->centerX = v; });
    applyFloatSlot(m, "Center.y", [&](float v) { n->centerY = v; });
    applyFloatSlot(m, "Center.z", [&](float v) { n->centerZ = v; });
    applyFloatSlot(m, "Normal.x", [&](float v) { n->normalX = v; });
    applyFloatSlot(m, "Normal.y", [&](float v) { n->normalY = v; });
    applyFloatSlot(m, "Normal.z", [&](float v) { n->normalZ = v; });
  }
}

// slot ids = the SAME ids configureRotatedPlaneSdf applies (Option B guard reads them, can't drift).
const FieldOp g_rotatedPlaneSdfOp(rotatedPlaneSdfSpec(), makeRotatedPlaneSdf, configureRotatedPlaneSdf,
                                  {"Center.x", "Center.y", "Center.z", "Normal.x", "Normal.y", "Normal.z"});

}  // namespace
}  // namespace sw
