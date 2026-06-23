// CylinderSDF field op (zero-shared-file leaf on the field self-registration seam). Like OctahedronSDF
// this single .cpp owns BOTH halves of one SDF op: the codegen NODE (CylinderSDFNode below, an
// addGlobals helper + vec3 + trailing scalars) AND the OP layer (a NodeSpec for the Add menu / findSpec
// + a FieldNodeFactory so a graph walk can instantiate it by type name), registered via the file-scope
// FieldOp registrar. The base machinery (FieldNode interface, assembleFieldMSL, param packing) stays
// FROZEN in runtime/field_graph — adding a field op = this one .cpp + one CMakeLists line.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/CylinderSDF.cs
//   inputs (reflection / [GraphParam] order): Center (Vector3), Radius (float), Height (float),
//     Rounding (float).  A FIFTH input, Axis (InputSlot<int>, MappedType=AxisTypes), is NOT a
//     [GraphParam] — it is a COMPILE-TIME CODE SELECTOR (see AXIS ENUM below), never packed.
//   .t3 defaults: Center=(0,0,0), Radius=0.5, Height=1.0, Rounding=0.05, Axis=1 (Y).
//   AddDefinitions: registers Globals["fRoundedCyl"] (the helper fn) — like OctahedronSDF, emitted via
//     addGlobals (the field_graph globals path is wired: collectEmbeddedShaderCode calls node.addGlobals,
//     and assembleFieldMSL injects ctx.globals into the /*{GLOBALS}*/ hook).
//   GetPreShaderCode (CylinderSDF.cs:53-56), a = _axisCodes0[_axis]:
//     f{c}.w = fRoundedCyl(p{c}.{a}, {n}Center.{a}, {n}Radius *0.5, {n}Rounding, {n}Height *0.5 );
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//
// AXIS ENUM (leaf-local, NOT a runtime uniform): TiXL's Axis InputSlot<int> drives a string lookup
//   _axisCodes0[axis] that emits a DIFFERENT swizzle into the MSL text (hence changes srcHash). It is
//   a compile-time CODE SELECTOR, not a shader param — it MUST NOT be passed to appendScalarParam (that
//   would corrupt the 16-byte float layout + the golden). We hold it as `int axis` on the node, set to
//   the .t3 default (1=Y) in the ctor, and concatenate kAxisCodes[axis] into the call string in
//   preShaderCode. The Inspector exposes it as a Widget::Enum Float port (labels {X,Y,Z}, def 1) for
//   authoring parity, but the cook does not feed it into the param buffer this batch (golden uses the
//   default). Axis swizzle table copied VERBATIM from CylinderSDF.cs:63-68.
//
// NAMED FORK (fork-cylinder-raw-radius-height, opposite of BoxSDF): TiXL multiplies Radius and Height
//   by 0.5 *inside the shader call text* (`{n}Radius *0.5`, `{n}Height *0.5`) — there is NO
//   AdditionalParameter pre-multiply. So we pack RAW Radius and Height and emit the `*0.5` in the call
//   TEXT, NOT at pack time. (BoxSDF pre-multiplies at pack; CylinderSDF does not — honored here.)
//
// HLSL->MSL forks honored: same packed_float3 alignment fork as OctahedronSDF (handled by
//   appendVec3Param) + the cbuffer-vs-struct param access (`P.` prefix on every param read). The
//   helper body math (length/abs/min/max, all identical in MSL) is byte-identical to the .cs — and the
//   "- 2.0*ra+rb" term is copied VERBATIM (C precedence = (-2*ra)+rb; do NOT simplify).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// Axis swizzle table — copied VERBATIM from CylinderSDF.cs:63-68 (_axisCodes0). index = X,Y,Z.
// Leaf-local file-scope: a compile-time CODE SELECTOR, NOT a runtime uniform. preShaderCode looks up
// kAxisCodes[axis] and concatenates it into the emitted MSL (so a different Axis -> different srcHash).
static const char* kAxisCodes[] = {"yxz", "xyz", "xzy"};

// ---- CylinderSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) --------------

// Distance-to-rounded-cylinder field leaf. Parity: CylinderSDF.cs AddDefinitions + GetPreShaderCode +
// .t3 defaults (Center=(0,0,0), Radius=0.5, Height=1.0, Rounding=0.05, Axis=1 Y). The [GraphParam]
// floats are collected in reflection order (Center, Radius, Height, Rounding); Axis is held as an int
// member (NOT packed) and selects the swizzle text.
struct CylinderSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float radius = 0.5f;
  float height = 1.0f;
  float rounding = 0.05f;
  int axis = 1;  // .t3 default Axis=1 (Y) -> kAxisCodes[1] = "xyz". CODE SELECTOR, never packed.

  explicit CylinderSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix.
    prefix = "CylinderSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/CylinderSDF.cs:47-52
    //   c.Globals["fRoundedCyl"] = "float fRoundedCyl(...) { ... }";
    // De-duped by key (std::map::operator[]). Body byte-identical to the .cs — length/abs/min/max all
    // exist identically in MSL (no math fork). The "- 2.0*ra+rb" is VERBATIM: C precedence groups it
    // as (-2.0*ra)+rb (TiXL's HLSL evaluates the same), so do NOT rewrite to -(2.0*ra+rb).
    c.globals["fRoundedCyl"] =
        "float fRoundedCyl(float3 p, float3 center, float ra, float rb, float h) {\n"
        "    float2 d = float2(length (p.xz - center.xz) - 2.0*ra+rb, abs(p.y-center.y) - h);\n"
        "    return min(max(d.x,d.y),0.0) + length(max(d,0.0)) - rb;\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY CylinderSDF.cs:53-56 (a = _axisCodes0[(int)_axis]):
    //   c.AppendCall($"f{c}.w = fRoundedCyl(p{c}.{a}, {n}Center.{a}, {n}Radius *0.5, {n}Rounding, {n}Height *0.5 );");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // {n} = node prefix (qualified `P.` for MSL struct access); {c} = context id; {a} = axis swizzle.
    // FORK fork-cylinder-raw-radius-height: Radius/Height packed RAW; the *0.5 is emitted in the call
    // TEXT here (TiXL multiplies in the shader, no AdditionalParameter pre-multiply).
    const std::string ctx = c.ctx();
    const char* a = kAxisCodes[axis];
    c.appendCall("f" + ctx + ".w = fRoundedCyl(p" + ctx + "." + a + ", P." + prefix + "Center." + a +
                 ", P." + prefix + "Radius *0.5, P." + prefix + "Rounding, P." + prefix +
                 "Height *0.5 );");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] reflection order = Center (vec3) -> Radius -> Height -> Rounding. Axis is NOT a
    // [GraphParam] and is NEVER appended (it is a compile-time code selector, see header). Layout:
    // Center at offset 0 (no padding) [0..2], Radius [3], Height [4], Rounding [5] = 6 floats total.
    // Args are passed BY NAME in the call, so struct-declaration order vs call-argument order may
    // differ harmlessly (call is fRoundedCyl(.., Radius, Rounding, Height); pack is Radius,Height,
    // Rounding) — each reads `P.<prefix><name>` so there is no positional coupling.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "Radius", radius);
    appendScalarParam(floatParams, paramFields, prefix + "Height", height);
    appendScalarParam(floatParams, paramFields, prefix + "Rounding", rounding);
  }
};

NodeSpec cylinderSdfSpec() {
  NodeSpec s;
  s.type = "CylinderSDF";
  s.title = "Cylinder SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0). Radius/Height/Rounding = scalar Floats.
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  PortSpec r; r.id = "Radius"; r.name = "Radius"; r.dataType = "Float"; r.isInput = true;
  r.def = 0.5f; r.minV = 0.0f; r.maxV = 10.0f;
  PortSpec h; h.id = "Height"; h.name = "Height"; h.dataType = "Float"; h.isInput = true;
  h.def = 1.0f; h.minV = 0.0f; h.maxV = 10.0f;
  PortSpec rn; rn.id = "Rounding"; rn.name = "Rounding"; rn.dataType = "Float"; rn.isInput = true;
  rn.def = 0.05f; rn.minV = 0.0f; rn.maxV = 10.0f;
  // Axis: Widget::Enum CODE SELECTOR (labels X/Y/Z, def 1=Y). It is exposed for authoring parity but is
  // NOT a packed shader param — it changes the emitted MSL swizzle (srcHash), not a uniform. Cook does
  // not feed it into the float buffer this batch (golden uses the default); see header.
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 1.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, r, h, rn, ax, out};
  return s;
}

// Factory: build a CylinderSDFNode for an instance. Center/Radius/Height/Rounding/Axis default to the
// .t3 values; a graph cook would override the [GraphParam] floats from the node's params before
// assembly (Axis re-selects the swizzle text — a code change, not a param upload).
std::shared_ptr<FieldNode> makeCylinderSdf(const std::string& shortId) {
  return std::make_shared<CylinderSDFNode>(shortId);
}

// PF-0c param-apply (WAVE 3): project a RESOLVED param map onto a CylinderSDFNode via setter-lambdas (NOT
// offsetof). Slot ids EQUAL the NodeSpec PortSpec.id for the 4 packed [GraphParam] floats (Center.x/.y/.z,
// Radius, Height, Rounding) + Axis (the compile-time swizzle code selector, applyIntSelSlot — switches the
// emitted swizzle text, NOT the float buffer). A missing key keeps the member's ctor .t3 default. Routed
// via fieldConfigurers().
void configureCylinderSdfFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<CylinderSDFNode*>(&node)) {
    applyFloatSlot(m, "Center.x", [&](float v) { n->centerX = v; });
    applyFloatSlot(m, "Center.y", [&](float v) { n->centerY = v; });
    applyFloatSlot(m, "Center.z", [&](float v) { n->centerZ = v; });
    applyFloatSlot(m, "Radius", [&](float v) { n->radius = v; });
    applyFloatSlot(m, "Height", [&](float v) { n->height = v; });
    applyFloatSlot(m, "Rounding", [&](float v) { n->rounding = v; });
    applyIntSelSlot(m, "Axis", [&](int v) { n->axis = v; });
  }
}

// slot ids = the SAME ids configureCylinderSdfFromParams applies (Option B guard, can't drift).
const FieldOp g_cylinderSdfOp(cylinderSdfSpec(), makeCylinderSdf, configureCylinderSdfFromParams,
                              {"Center.x", "Center.y", "Center.z", "Radius", "Height", "Rounding",
                               "Axis"});

}  // namespace
}  // namespace sw
