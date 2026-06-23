// PrismSDF field op (zero-shared-file leaf on the field self-registration seam). Like SphereSDF /
// TorusSDF / OctahedronSDF this single .cpp owns BOTH halves of one SDF op: the codegen NODE
// (PrismSDFNode below) AND the OP layer (a NodeSpec for the Add menu / findSpec + a FieldNodeFactory
// so a graph walk can instantiate it by type name), registered via the file-scope FieldOp registrar.
// The base machinery (FieldNode interface, assembleFieldMSL, param packing) stays FROZEN in
// runtime/field_graph — adding a field op = this one .cpp + one CMakeLists line, no shared file edited.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/PrismSDF.cs (+ PrismSDF.t3 defaults).
//   inputs ([GraphParam] reflection order): Center (Vector3), Radius (float), Length (float),
//     EdgeRadius (float).  PLUS Sides (InputSlot<int>, MappedType=SidesType{_3,_6}) AND Axis
//     (InputSlot<int>, MappedType=AxisTypes{X,Y,Z}) — NEITHER is [GraphParam], see CODE-SELECTOR note.
//   .t3 defaults: Center=(0,0,0), Radius=1.0, Length=1.0, EdgeRadius=0.05, Sides=1 (=_6 -> hex),
//     Axis=1 (=Y) — mirrored in the ctor (sides member resolved to 6, axis member to 1).
//   AddDefinitions: registers Globals["fTriangularPrism"] (sides==3) OR Globals["fHexPrism"]
//     (sides==6) — exactly ONE helper, chosen by _sides — like Octahedron/Torus (GLOBAL helper via
//     addGlobals), UNLIKE SphereSDF which is fully inline. We emit ONLY the active branch's helper.
//   GetPreShaderCode (a = _axisCodes0[axis]; the *0.5 derived terms stay IN THE CALL TEXT — raw pack):
//     sides==3: f{c}.w = fTriangularPrism(p{c}.<a> - {n}Center.<a>, {n}Radius * 0.5, {n}Length * 0.5);
//     sides==6: f{c}.w = fHexPrism(p{c}.<a> - {n}Center.<a>, {n}Radius * 0.5, {n}Length * 0.5,
//                                  {n}EdgeRadius);
//     then: f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//
// ★ TWO COMPILE-TIME CODE SELECTORS (NOT runtime uniforms), the load-bearing seam of this leaf:
//   (1) Axis — selects the swizzle string a = _axisCodes0[axis] concatenated into the emitted MSL.
//   (2) Sides — selects BOTH the active global helper (fTriangularPrism vs fHexPrism) AND the call
//       line (triangular vs hex signature). In TiXL Update() compares axis/_sides and calls
//       ShaderNode.FlagCodeChanged() when either changes — they re-WRITE the emitted shader text, NOT
//       fed through the float param buffer. We mirror this: both live as int members (axis default 1,
//       sides default 6 from the resolved .t3 Sides=1=_6), set in the ctor, looked up in addGlobals /
//       preShaderCode to build the emitted text. BOTH are DELIBERATELY absent from collectParams —
//       packing either would corrupt the 16-byte float layout (and the golden). The kAxisCodes table
//       below is copied VERBATIM from PrismSDF.cs `_axisCodes0` (file-scope, no per-node duplication).
//
// Param order parity (only the 4 [GraphParam] fields are packed, in declaration order): Center (vec3)
// -> Radius (scalar) -> Length (scalar) -> EdgeRadius (scalar). ALL 4 are packed even in the sides==3
// branch (which does not USE EdgeRadius in its call) so the float layout is stable regardless of the
// Sides selector. Layout: Center=floats[0..2] (packed_float3, offset 0 so padForVec3 adds no padding),
// Radius=floats[3], Length=floats[4], EdgeRadius=floats[5] — 6 floats total, same tight packed_float3
// + trailing scalars layout the other SDF leaves use.
//
// HLSL->MSL forks honored: (1) the same packed_float3 alignment fork as the other SDF leaves (handled
// by appendVec3Param). (2) the cbuffer-vs-struct param access: TiXL reads bare `{n}Name` from a global
// cbuffer; MSL reads `P.{n}Name` from the `constant FieldParams& P` argument — we emit the `P.` prefix
// here. The helper bodies (abs/max/min/length/clamp/dot/sign/float2/float3 all exist identically in
// MSL) are byte-identical to the .cs — NO math fork. NOTE: PrismSDF.cs writes `{n}Radius *0.5` (no
// space) in the hex branch and `{n}Radius * 0.5` in the triangular branch; the spacing is cosmetic and
// does not affect parsed semantics — we normalize to `* 0.5` (identical math).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// VERBATIM copy of PrismSDF.cs `_axisCodes0` (file-scope: index = AxisTypes value X=0,Y=1,Z=2).
// This is the compile-time swizzle the Axis selector emits into the generated MSL.
static const char* kAxisCodes[] = {"yzx", "xzy", "xyz"};

// ---- PrismSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) -----------------

// Distance-to-prism field leaf. Parity: PrismSDF.cs AddDefinitions + GetPreShaderCode + PrismSDF.t3
// defaults (Center=(0,0,0), Radius=1.0, Length=1.0, EdgeRadius=0.05, Sides=_6, Axis=Y). The four
// [GraphParam] floats are collected in field-declaration order; `axis` and `sides` are code selectors
// (see header) held as int members, NOT packed.
struct PrismSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float radius = 1.0f;
  float length = 1.0f;
  float edgeRadius = 0.05f;
  // Axis code selector — .t3 default 1 (Y) -> kAxisCodes[1] = "xzy". Set in the ctor; the golden uses
  // this default so no cook wiring is needed this batch.
  int axis = 1;
  // Sides code selector — .t3 default Sides=1 (=_6) -> 6 (the HEX branch = fHexPrism). The .cs maps
  // the enum (SidesType._3=0, _6=1) to the integer 3 or 6; we store the resolved integer (6 = hex).
  // A cook would map params["Sides"] (the enum index 0/1) to 3/6 before assembly; the golden uses 6.
  int sides = 6;

  explicit PrismSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix (same shape as the
    // other SDF leaves' "<Type>_<shortId>_").
    prefix = "PrismSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/PrismSDF.cs AddDefinitions:
    //   switch(_sides) { case 3: Globals["fTriangularPrism"] = ...; case 6: Globals["fHexPrism"] = ...; }
    // Emit ONLY the active branch's helper, keyed exactly as TiXL keys it (de-duped by std::map key).
    // Bodies byte-identical to the .cs (abs/max/min/length/clamp/dot/sign/float2/float3 all exist in
    // MSL — no math fork).
    if (sides == 3) {
      c.globals["fTriangularPrism"] =
          "float fTriangularPrism(float3 p, float r, float l) {\n"
          "    float3 q = abs(p);\n"
          "    return max(q.z-l,max(q.x*0.866025+p.y*0.5,-p.y)-r*0.5);\n"
          "}";
    } else {
      c.globals["fHexPrism"] =
          "float fHexPrism(float3 p, float r, float l, float round) {\n"
          "    const float3 k = float3(-0.8660254, 0.5, 0.57735);\n"
          "    p = abs(p);\n"
          "    p.xy -= 2.0*min(dot(k.xy, p.xy), 0.0)*k.xy;\n"
          "    float2 d = float2(length(p.xy-float2(clamp(p.x,-k.z * r, k.z * r), r))*sign(p.y - r),p.z - l);\n"
          "    return min(max(d.x,d.y),0.0) + length(max(d,0.0))-round;\n"
          "}";
    }
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY PrismSDF.cs GetPreShaderCode:
    //   var a = _axisCodes0[(int)_axis];
    //   switch(_sides) {
    //     case 3: f{c}.w = fTriangularPrism(p{c}.{a} - {n}Center.{a}, {n}Radius * 0.5, {n}Length * 0.5);
    //     case 6: f{c}.w = fHexPrism(p{c}.{a} - {n}Center.{a}, {n}Radius *0.5, {n}Length * 0.5,
    //                                {n}EdgeRadius);
    //   }
    //   f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
    // {n} = node prefix (qualified P. for MSL struct access); {c} = context id; a = swizzle code.
    // The swizzle `a` is applied to BOTH p{c} and Center (matching the .cs). The *0.5 derived terms
    // are emitted IN THE CALL TEXT — Radius/Length are packed RAW; the helper receives the halved
    // value computed in-shader (exactly as TiXL does).
    const std::string ctx = c.ctx();
    const std::string a = kAxisCodes[(axis < 0) ? 0 : (axis > 2 ? 2 : axis)];  // guarded swizzle
    if (sides == 3) {
      c.appendCall("f" + ctx + ".w = fTriangularPrism(p" + ctx + "." + a + " - P." + prefix +
                   "Center." + a + ", P." + prefix + "Radius * 0.5, P." + prefix + "Length * 0.5);");
    } else {
      c.appendCall("f" + ctx + ".w = fHexPrism(p" + ctx + "." + a + " - P." + prefix + "Center." + a +
                   ", P." + prefix + "Radius * 0.5, P." + prefix + "Length * 0.5, P." + prefix +
                   "EdgeRadius);");
    }
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] declaration order = Center (vec3) -> Radius -> Length -> EdgeRadius, matching
    // PrismSDF.cs reflection order. Axis/Sides are NOT packed (code selectors, see header). ALL 4
    // scalars are packed even in the sides==3 branch (keeps the layout stable across the Sides
    // selector). appendVec3Param/padForVec3 own the vec3 alignment (Center at offset 0 -> no padding);
    // Center(3) + Radius(1) + Length(1) + EdgeRadius(1) = 6 floats.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "Radius", radius);
    appendScalarParam(floatParams, paramFields, prefix + "Length", length);
    appendScalarParam(floatParams, paramFields, prefix + "EdgeRadius", edgeRadius);
  }
};

NodeSpec prismSdfSpec() {
  NodeSpec s;
  s.type = "PrismSDF";
  s.title = "Prism SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0). Radius, Length, EdgeRadius = scalar Floats.
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  PortSpec r; r.id = "Radius"; r.name = "Radius"; r.dataType = "Float"; r.isInput = true;
  r.def = 1.0f; r.minV = 0.0f; r.maxV = 10.0f;
  PortSpec ln; ln.id = "Length"; ln.name = "Length"; ln.dataType = "Float"; ln.isInput = true;
  ln.def = 1.0f; ln.minV = 0.0f; ln.maxV = 10.0f;
  PortSpec er; er.id = "EdgeRadius"; er.name = "EdgeRadius"; er.dataType = "Float"; er.isInput = true;
  er.def = 0.05f; er.minV = 0.0f; er.maxV = 10.0f;
  // Sides = enum CODE SELECTOR (SidesType {_3,_6}). It is a Float port (storing the enum index) with
  // widget=Widget::Enum + labels — drawn as a dropdown. .t3 default 1 (=_6 -> hex). NOT a [GraphParam]
  // in TiXL (never packed); the node's `sides` int member carries it at codegen time (resolved 0->3,
  // 1->6). Labels are the enum's display names ("3"/"6").
  PortSpec sd; sd.id = "Sides"; sd.name = "Sides"; sd.dataType = "Float"; sd.isInput = true;
  sd.def = 1.0f; sd.minV = 0.0f; sd.maxV = 1.0f; sd.widget = Widget::Enum;
  sd.labels = {"3", "6"};
  // Axis = enum CODE SELECTOR (AxisTypes {X,Y,Z}). Widget::Enum + 3 labels, default index 1 (Y).
  // NOT packed (it changes the emitted MSL swizzle, not a uniform); the cook would set
  // PrismSDFNode::axis from this port before assembly. min0 max2 spans the 3 enum values.
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 1.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, r, ln, er, sd, ax, out};
  return s;
}

// Factory: build a PrismSDFNode for an instance. Center/Radius/Length/EdgeRadius/Sides/Axis default
// to the .t3 values (baked in the ctor); a graph cook would override them from the node's params
// before assembly (mapping the Sides enum index 0/1 to the resolved 3/6).
std::shared_ptr<FieldNode> makePrismSdf(const std::string& shortId) {
  return std::make_shared<PrismSDFNode>(shortId);
}

// PF-0c param-apply (WAVE 3): project a RESOLVED param map onto a PrismSDFNode via setter-lambdas (NOT
// offsetof). Slot ids EQUAL the NodeSpec PortSpec.id for the 4 packed [GraphParam] floats (Center.x/.y/.z,
// Radius, Length, EdgeRadius) + TWO compile-time code selectors: Axis (the swizzle, applyIntSelSlot) AND
// Sides (the helper switch fTriangularPrism vs fHexPrism). NOTE the Sides ENUM INDEX (0/1) maps to the
// RESOLVED helper integer 3/6 (the node stores the resolved count, mirroring TiXL Update()'s SidesType._3
// =0 -> 3, _6=1 -> 6), so its setter remaps idx -> 3/6 INSIDE the lambda (NOT a raw idx store). Both
// selectors switch the emitted MSL text, NOT the float buffer. A missing key keeps the ctor .t3 default.
// Routed via fieldConfigurers().
void configurePrismSdfFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<PrismSDFNode*>(&node)) {
    applyFloatSlot(m, "Center.x", [&](float v) { n->centerX = v; });
    applyFloatSlot(m, "Center.y", [&](float v) { n->centerY = v; });
    applyFloatSlot(m, "Center.z", [&](float v) { n->centerZ = v; });
    applyFloatSlot(m, "Radius", [&](float v) { n->radius = v; });
    applyFloatSlot(m, "Length", [&](float v) { n->length = v; });
    applyFloatSlot(m, "EdgeRadius", [&](float v) { n->edgeRadius = v; });
    applyIntSelSlot(m, "Axis", [&](int v) { n->axis = v; });
    applyIntSelSlot(m, "Sides", [&](int idx) { n->sides = (idx == 0) ? 3 : 6; });  // enum idx -> resolved 3/6
  }
}

// slot ids = the SAME ids configurePrismSdfFromParams applies (Option B guard, can't drift).
const FieldOp g_prismSdfOp(prismSdfSpec(), makePrismSdf, configurePrismSdfFromParams,
                           {"Center.x", "Center.y", "Center.z", "Radius", "Length", "EdgeRadius", "Axis",
                            "Sides"});

}  // namespace
}  // namespace sw
