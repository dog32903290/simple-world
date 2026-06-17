// CappedTorusSDF field op (zero-shared-file leaf on the field self-registration seam). Like SphereSDF
// / OctahedronSDF / TorusSDF this single .cpp owns BOTH halves of one SDF op: the codegen NODE
// (CappedTorusSDFNode below) AND the OP layer (a NodeSpec for the Add menu / findSpec + a
// FieldNodeFactory so a graph walk can instantiate it by type name), registered via the file-scope
// FieldOp registrar. The base machinery (FieldNode interface, assembleFieldMSL, param packing) stays
// FROZEN in runtime/field_graph — adding a field op = this one .cpp + one CMakeLists line, no shared
// file edited.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/CappedTorusSDF.cs (+ .t3 defaults).
//   inputs ([GraphParam] reflection order): Center (Vector3), Fill (float), Radius (float),
//     Thickness (float). PLUS Axis (InputSlot<int>, MappedType=AxisTypes) — NOT a [GraphParam], it is
//     a CODE selector (see AXIS note).
//   .t3 defaults: Center = (0,0,0), Fill = 2.0, Radius = 2.0, Thickness = 0.5, Axis = 2 (Z) —
//     mirrored in the ctor.
//   AddDefinitions: registers Globals["fCappedTorus"] (the helper fn) — like Octahedron/Torus (a
//     GLOBAL helper via addGlobals), UNLIKE SphereSDF which is fully inline.
//   GetPreShaderCode (a = _axisCodes0[axis]):
//     f{c}.w = fCappedTorus(p{c}.<a> - {n}Center.<a>, {n}Fill, {n}Radius, {n}Thickness);
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//   ★ ARG-ORDER PARITY (load-bearing): the call passes Fill, Radius, Thickness in THAT order, mapping
//   to the helper's (size, ra, rb) parameters respectively. Verified byte-for-byte against the .cs.
//
// AXIS = COMPILE-TIME CODE SELECTOR (NOT a runtime uniform), the load-bearing seam of this leaf.
//   In TiXL `Axis` is an InputSlot<int> with MappedType=AxisTypes; it is *not* tagged [GraphParam], so
//   it is NEVER appended to the shader's float param buffer. Instead it selects a swizzle string at
//   codegen time: a = _axisCodes0[axis] is concatenated into the emitted MSL, changing the generated
//   source text (hence the srcHash / FlagCodeChanged path), not a packed float. We mirror this exactly:
//   the axis lives as an int member on the node (default 2 = Z from the .t3), set in the ctor, and is
//   looked up in preShaderCode to build the call string. It is DELIBERATELY absent from collectParams
//   — packing it would corrupt the 16-byte float layout (and the golden). The kAxisCodes table below
//   is copied VERBATIM from CappedTorusSDF.cs `_axisCodes0` (file-scope so it has no per-node dup).
//
// Param order parity (only the 4 [GraphParam] fields are packed): Center (vec3) -> Fill (scalar) ->
// Radius (scalar) -> Thickness (scalar), emitted in that declaration order by collectParams. Layout:
// Center=floats[0..2] (packed_float3, offset 0 so padForVec3 adds no padding), Fill=floats[3],
// Radius=floats[4], Thickness=floats[5] — 6 floats total, the same tight packed_float3 + trailing
// scalars layout the other SDF leaves use.
//
// HLSL->MSL forks honored: (1) the same packed_float3 alignment fork as the other SDF leaves (handled
// by appendVec3Param). (2) the cbuffer-vs-struct param access: TiXL reads bare `{n}Name` from a global
// cbuffer; MSL reads `P.{n}Name` from the `constant FieldParams& P` argument — we emit the `P.`
// prefix here. The helper body math (sin/cos/dot/length/sqrt/abs, all identical in MSL) and the `.xy`
// swizzle syntax are byte-identical to the .cs — NO math fork.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// VERBATIM copy of CappedTorusSDF.cs `_axisCodes0` (file-scope: index = AxisTypes value X=0,Y=1,Z=2).
// This is the compile-time swizzle the Axis selector emits into the generated MSL.
static const char* kAxisCodes[] = {"yzx", "xzy", "xyz"};

// ---- CappedTorusSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) -----------

// Distance-to-capped-torus field leaf. Parity: CappedTorusSDF.cs AddDefinitions + GetPreShaderCode +
// .t3 defaults (Center=(0,0,0), Fill=2.0, Radius=2.0, Thickness=0.5, Axis=2/Z). The four [GraphParam]
// floats are collected in field-declaration order (Center, Fill, Radius, Thickness); `axis` is a code
// selector (see the header AXIS note) held as an int member, NOT packed.
struct CappedTorusSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float fill = 2.0f;
  float radius = 2.0f;
  float thickness = 0.5f;
  // Axis code selector — .t3 default 2 (Z) -> kAxisCodes[2] = "xyz" (identity, clean emitted text).
  // Set in the ctor; the golden uses this default so no cook wiring is needed this batch.
  int axis = 2;

  explicit CappedTorusSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix (same shape as the
    // other SDF leaves' "<Type>_<shortId>_").
    prefix = "CappedTorusSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/CappedTorusSDF.cs AddDefinitions
    //   c.Globals["fCappedTorus"] = "float fCappedTorus(...) { ... }";
    // De-duped by key (std::map::operator[]): two CappedTorusSDF nodes -> the helper appears exactly
    // once (TiXL Globals semantics). Body byte-identical to the .cs (sin/cos/dot/length/sqrt/abs,
    // float2, the ?: ternary all exist identically in MSL — no math fork).
    c.globals["fCappedTorus"] =
        "float fCappedTorus(float3 p, float size, float ra, float rb) {\n"
        "    float an = 2.5 * (0.5 + 0.5 * (size * 1.1 + 3));\n"
        "    float2 sc = float2(sin(an),cos(an));\n"
        "    p.x = abs(p.x);\n"
        "    float k = (sc.y*p.x>sc.x*p.y) ? dot(p.xy,sc) : length(p.xy);\n"
        "    return sqrt(dot(p,p) + ra*ra - 2.0*ra*k ) - rb;\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY CappedTorusSDF.cs GetPreShaderCode:
    //   var a = _axisCodes0[(int)_axis];
    //   c.AppendCall($"f{c}.w = fCappedTorus(p{c}.{a} - {n}Center.{a}, {n}Fill, {n}Radius, {n}Thickness);");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // {n} = node prefix (qualified P. for MSL struct access); {c} = context id; a = swizzle code.
    // The swizzle `a` is applied to BOTH p{c} and Center (matching the .cs). Fill/Radius/Thickness are
    // scalar — no swizzle. ARG ORDER = Fill, Radius, Thickness -> helper (size, ra, rb).
    const std::string ctx = c.ctx();
    const std::string a = kAxisCodes[axis];  // compile-time swizzle selected by the Axis code member
    c.appendCall("f" + ctx + ".w = fCappedTorus(p" + ctx + "." + a + " - P." + prefix + "Center." + a +
                 ", P." + prefix + "Fill, P." + prefix + "Radius, P." + prefix + "Thickness);");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] declaration order = Center (vec3) -> Fill (scalar) -> Radius (scalar) ->
    // Thickness (scalar), matching CappedTorusSDF.cs reflection order. Axis is NOT packed (code
    // selector, see header AXIS note). appendVec3Param/padForVec3 own the vec3 alignment (Center at
    // offset 0 -> no padding); Center(3)+Fill(1)+Radius(1)+Thickness(1) = 6 floats.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "Fill", fill);
    appendScalarParam(floatParams, paramFields, prefix + "Radius", radius);
    appendScalarParam(floatParams, paramFields, prefix + "Thickness", thickness);
  }
};

NodeSpec cappedTorusSdfSpec() {
  NodeSpec s;
  s.type = "CappedTorusSDF";
  s.title = "Capped Torus SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0). Fill, Radius, Thickness = scalar Floats.
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  PortSpec fi; fi.id = "Fill"; fi.name = "Fill"; fi.dataType = "Float"; fi.isInput = true;
  fi.def = 2.0f; fi.minV = 0.0f; fi.maxV = 10.0f;
  PortSpec r; r.id = "Radius"; r.name = "Radius"; r.dataType = "Float"; r.isInput = true;
  r.def = 2.0f; r.minV = 0.0f; r.maxV = 10.0f;
  PortSpec th; th.id = "Thickness"; th.name = "Thickness"; th.dataType = "Float"; th.isInput = true;
  th.def = 0.5f; th.minV = 0.0f; th.maxV = 10.0f;
  // Axis = enum CODE SELECTOR (X/Y/Z). It is a Float port (storing the enum index) with
  // widget=Widget::Enum + labels — drawn as a dropdown like CompareInt's Mode. .t3 default 2 (Z).
  // It is NOT a [GraphParam] in TiXL (never packed); the node's `axis` int member carries it at
  // codegen time. (The factory could read params["Axis"] into node->axis; the golden uses the
  // ctor default 2 so no cook wiring is needed this batch.)
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 2.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, fi, r, th, ax, out};
  return s;
}

// Factory: build a CappedTorusSDFNode for an instance. Center/Fill/Radius/Thickness/Axis default to
// the .t3 values (baked in the ctor); a graph cook would override them from the node's params before
// assembly.
std::shared_ptr<FieldNode> makeCappedTorusSdf(const std::string& shortId) {
  return std::make_shared<CappedTorusSDFNode>(shortId);
}

const FieldOp g_cappedTorusSdfOp(cappedTorusSdfSpec(), makeCappedTorusSdf);

}  // namespace
}  // namespace sw
