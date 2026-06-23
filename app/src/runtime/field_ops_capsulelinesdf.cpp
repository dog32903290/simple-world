// CapsuleLineSDF field op (zero-shared-file leaf, mirrors field_ops_spheresdf.cpp). TiXL authority:
// external/tixl/Operators/Lib/field/generate/sdf/CapsuleLineSDF.cs (+ .t3 defaults). This leaf owns
// BOTH halves of one SDF op: the codegen NODE (CapsuleLineSDFNode below — addGlobals / preShaderCode /
// collectParams) AND the OP layer (a NodeSpec for the Add menu / findSpec + a FieldNodeFactory for
// graph-walk instantiation), registered via the FieldOp self-registration seam. The codegen base
// machinery (FieldNode, assembleFieldMSL, param packing, GLOBALS-hook injection) stays in
// runtime/field_graph and is FROZEN — adding a field op = this one .cpp + one CMakeLists line.
//
//   CapsuleLineSDF.cs inputs (reflection order on [GraphParam] fields):
//     Center (Vector3), StartingPoint (Vector3), EndPoint (Vector3), Thickness (float).
//   CapsuleLineSDF.t3 defaults: Center=(0,0,0), StartingPoint=(0,0.5,0), EndPoint=(0,-0.5,0),
//     Thickness=0.125  (mirrored in CapsuleLineSDFNode's ctor).
//   GetPreShaderCode registers the reusable `fCapsule` helper into c.Globals["fCapsule"] (this is the
//   GLOBAL-helper path — UNLIKE SphereSDF which was inline. Verified wired: field_graph.cpp
//   collectEmbeddedShaderCode calls node.addGlobals on every node, and assembleFieldMSL collects
//   cac.globals into the /*{GLOBALS}*/ hook), then emits the distance call + the local-space line.
//
// Param order parity + 16B layout (Center, StartingPoint, EndPoint, Thickness) — THIS LEAF exercises
// padForVec3's pad-insertion path (SphereSDF did not). The packing helpers lay it out:
//     Center        -> floats[0..2]          (no pad: start offset 0)
//     [__padding]    -> floats[3]             (1 pad: StartingPoint would straddle 16B at offset 3)
//     StartingPoint -> floats[4..6]
//     [__padding]    -> floats[7]             (1 pad: EndPoint would straddle 16B at offset 7)
//     EndPoint      -> floats[8..10]
//     Thickness     -> floats[11]            (scalar, no pad). Total = 12 floats.
// This matches TiXL ShaderParamHandling (each float3 16B-aligned, scalar packs into the trailing
// slot). We do NOT hand-roll padding — appendVec3Param/padForVec3 emit the pads + matching
// "float __paddingN;" struct fields.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- CapsuleLineSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) ------------

// Distance-to-capsule (line-segment SDF) field leaf. Parity: CapsuleLineSDF.cs GetPreShaderCode +
// CapsuleLineSDF.t3 defaults. Params collected in field-declaration order (Center, StartingPoint,
// EndPoint, Thickness) exactly like TiXL reflection order on [GraphParam] fields.
struct CapsuleLineSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float startX = 0.f, startY = 0.5f, startZ = 0.f;
  float endX = 0.f, endY = -0.5f, endZ = 0.f;
  float thickness = 0.125f;

  explicit CapsuleLineSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix.
    prefix = "CapsuleLineSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/CapsuleLineSDF.cs:35-42
    //   c.Globals["fCapsule"] = "float fCapsule(...) { ... }";
    // De-duped by key "fCapsule" (std::map<key,code>): if two CapsuleLineSDF nodes share a graph the
    // helper appears ONCE in the assembled shader. The body is identical text in HLSL and MSL (float3,
    // dot, clamp, length are common syntax) — pure copy from the .cs, no fork here.
    c.globals["fCapsule"] =
        "float fCapsule(float3 p, float3 a, float3 b, float r) {\n"
        "    float3 pa = p - a;\n"
        "    float3 ba = b - a;\n"
        "    float h = clamp( dot(pa,ba)/dot(ba,ba), 0.0, 1.0 );\n"
        "    return length( pa - ba*h ) - r;\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY CapsuleLineSDF.cs:44-45
    //   c.AppendCall($"f{c}.w = fCapsule(p{c} - {n}Center, {n}StartingPoint, {n}EndPoint, {n}Thickness);");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // {n} = node prefix; {c} = context id.
    //
    // FORK #1 (HLSL->MSL param access, same as SphereSDF): params live in `constant FieldParams& P`,
    // so every read is qualified `P.{n}...`. The distance MATH is byte-identical.
    //
    // FORK #2 (named, load-bearing): TiXL writes `fCapsule(p{c} - {n}Center, ...)` passing
    // float4-minus-float3 — HLSL silently TRUNCATES the float4 `p{c}` to float3, but MSL REJECTS the
    // float4-float3 subtraction (and float4 -> float3 arg). We emit `p{c}.xyz - P.{n}Center` so the
    // subtraction is float3-float3 and the arg is a float3, reproducing HLSL's implicit truncation
    // explicitly. (`p.z`=SliceDepth=0 here, so dropping .w is faithful: the capsule is evaluated in
    // the xyz field space.)
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = fCapsule(p" + ctx + ".xyz - P." + prefix + "Center, P." + prefix +
                 "StartingPoint, P." + prefix + "EndPoint, P." + prefix + "Thickness);");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // Field-declaration order = Center, StartingPoint, EndPoint (vec3) then Thickness (scalar),
    // matching CapsuleLineSDF.cs reflection order on [GraphParam] fields. appendVec3Param/padForVec3
    // insert the two pad floats (at offsets 3 and 7) so each float3 stays 16B-aligned; Thickness packs
    // into the trailing scalar slot (offset 11). Total = 12 floats. NEVER hand-roll padding.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendVec3Param(floatParams, paramFields, prefix + "StartingPoint", startX, startY, startZ);
    appendVec3Param(floatParams, paramFields, prefix + "EndPoint", endX, endY, endZ);
    appendScalarParam(floatParams, paramFields, prefix + "Thickness", thickness);
  }
};

NodeSpec capsuleLineSdfSpec() {
  NodeSpec s;
  s.type = "CapsuleLineSDF";
  s.title = "Capsule Line SDF";
  // Three Vec3 head runs (Center / StartingPoint / EndPoint) each as .x/.y/.z, then scalar Thickness.
  // Defaults mirror CapsuleLineSDF.t3.
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;

  PortSpec sx; sx.id = "StartingPoint.x"; sx.name = "StartingPoint"; sx.dataType = "Float";
  sx.isInput = true; sx.def = 0.0f; sx.minV = -10.0f; sx.maxV = 10.0f;
  sx.widget = Widget::Vec; sx.vecArity = 3;
  PortSpec sy; sy.id = "StartingPoint.y"; sy.name = "StartingPoint.y"; sy.dataType = "Float";
  sy.isInput = true; sy.def = 0.5f; sy.minV = -10.0f; sy.maxV = 10.0f;
  PortSpec sz; sz.id = "StartingPoint.z"; sz.name = "StartingPoint.z"; sz.dataType = "Float";
  sz.isInput = true; sz.def = 0.0f; sz.minV = -10.0f; sz.maxV = 10.0f;

  PortSpec ex; ex.id = "EndPoint.x"; ex.name = "EndPoint"; ex.dataType = "Float"; ex.isInput = true;
  ex.def = 0.0f; ex.minV = -10.0f; ex.maxV = 10.0f; ex.widget = Widget::Vec; ex.vecArity = 3;
  PortSpec ey; ey.id = "EndPoint.y"; ey.name = "EndPoint.y"; ey.dataType = "Float"; ey.isInput = true;
  ey.def = -0.5f; ey.minV = -10.0f; ey.maxV = 10.0f;
  PortSpec ez; ez.id = "EndPoint.z"; ez.name = "EndPoint.z"; ez.dataType = "Float"; ez.isInput = true;
  ez.def = 0.0f; ez.minV = -10.0f; ez.maxV = 10.0f;

  PortSpec th; th.id = "Thickness"; th.name = "Thickness"; th.dataType = "Float"; th.isInput = true;
  th.def = 0.125f; th.minV = 0.0f; th.maxV = 10.0f;

  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, sx, sy, sz, ex, ey, ez, th, out};
  return s;
}

// Factory: build a CapsuleLineSDFNode for an instance. Params default to the .t3 values; a graph cook
// would override them from the node's params before assembly (the golden uses the .t3 defaults).
std::shared_ptr<FieldNode> makeCapsuleLineSdf(const std::string& shortId) {
  return std::make_shared<CapsuleLineSDFNode>(shortId);
}

// PF-0c param-apply (WAVE 2): project a RESOLVED param map onto a CapsuleLineSDFNode via setter-lambdas
// (NOT offsetof). Slot ids EQUAL the NodeSpec PortSpec.id (Center.x/.y/.z, StartingPoint.x/.y/.z,
// EndPoint.x/.y/.z, Thickness). A missing key keeps the member's ctor .t3 default. Routed via the
// fieldConfigurers() table.
void configureCapsuleLineSdfFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<CapsuleLineSDFNode*>(&node)) {
    applyFloatSlot(m, "Center.x", [&](float v) { n->centerX = v; });
    applyFloatSlot(m, "Center.y", [&](float v) { n->centerY = v; });
    applyFloatSlot(m, "Center.z", [&](float v) { n->centerZ = v; });
    applyFloatSlot(m, "StartingPoint.x", [&](float v) { n->startX = v; });
    applyFloatSlot(m, "StartingPoint.y", [&](float v) { n->startY = v; });
    applyFloatSlot(m, "StartingPoint.z", [&](float v) { n->startZ = v; });
    applyFloatSlot(m, "EndPoint.x", [&](float v) { n->endX = v; });
    applyFloatSlot(m, "EndPoint.y", [&](float v) { n->endY = v; });
    applyFloatSlot(m, "EndPoint.z", [&](float v) { n->endZ = v; });
    applyFloatSlot(m, "Thickness", [&](float v) { n->thickness = v; });
  }
}

// slot ids = the SAME ids configureCapsuleLineSdfFromParams applies (Option B guard reads them, can't drift).
const FieldOp g_capsuleLineSdfOp(capsuleLineSdfSpec(), makeCapsuleLineSdf,
                                 configureCapsuleLineSdfFromParams,
                                 {"Center.x", "Center.y", "Center.z", "StartingPoint.x",
                                  "StartingPoint.y", "StartingPoint.z", "EndPoint.x", "EndPoint.y",
                                  "EndPoint.z", "Thickness"});

}  // namespace
}  // namespace sw
