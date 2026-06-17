// PlaneSDF field op (zero-shared-file leaf on the field self-registration seam). Like SphereSDF this
// single .cpp owns BOTH halves of one SDF op: the codegen NODE (PlaneSDFNode below) AND the OP layer
// (NodeSpec for the Add menu / findSpec + a FieldNodeFactory so a graph walk can instantiate it by
// type name), registered via the file-scope FieldOp registrar. The base machinery (FieldNode
// interface, assembleFieldMSL, param packing) stays FROZEN in runtime/field_graph — adding a field op
// = this one .cpp + one CMakeLists line, no shared file edited.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/PlaneSDF.cs
//   inputs: Center (Vector3, [GraphParam])  AND  Axis (int, MappedType=AxisTypes{X,Y,Z,NegX,NegY,NegZ}).
//   .t3 defaults: Center = (0,0,0), Axis = 1 (Y)  (mirrored in the ctor below).
//   GetPreShaderCode:
//     var a = _axisCodes0[(int)_axis];   // "x","y","z","x","y","z"
//     var sign = _axisSigns[(int)_axis];  // "","","","-","-","-"
//     c.AppendCall($"f{c}.w = {sign}(p{c}.{a} - {n}Center.{a});");
//     c.AppendCall($"f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;");
//   INLINE — no AddDefinitions / no global helper (like SphereSDF, unlike OctahedronSDF).
//
// ★ AXIS = COMPILE-TIME CODE SELECTOR, NOT A RUNTIME UNIFORM (load-bearing).
//   In TiXL PlaneSDF.Update() reads Axis and calls ShaderNode.FlagCodeChanged() when it changes — the
//   axis re-WRITES the emitted shader text (different swizzle / sign), it is NOT fed through the float
//   param buffer. So here the axis is held as an `int axis` member, set in the ctor to the .t3 default,
//   and looked up in preShaderCode to build the call string. It is NEVER passed to appendVec3Param /
//   appendScalarParam — that would corrupt the 16-byte float layout and the golden. collectParams
//   emits ONLY Center, so this is a VEC3-ONLY node: floats[0..2] = packed_float3 Center, no trailing
//   scalar, no padding (padForVec3 on an empty buffer adds 0 pad — verified in field_graph.cpp:122).
//
// HLSL->MSL forks honored: (1) the packed_float3 alignment fork (handled by appendVec3Param) — here it
//   degenerates to a lone packed_float3 with no following scalar to straddle a 16B slot. (2) the
//   cbuffer-vs-struct param access: TiXL reads bare `{n}Center` from a global cbuffer; MSL reads
//   `P.{n}Center` from the `constant FieldParams& P` argument — we emit the `P.` prefix here. The
//   distance MATH (single-axis difference + optional unary minus) is byte-identical to the .cs — single
//   swizzle (.x/.y/.z) and unary `-` are identical text in HLSL and MSL — NO math fork.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// Axis swizzle tables — copied VERBATIM from PlaneSDF.cs:54-72 (_axisCodes0 / _axisSigns). Index =
// AxisTypes enum value {X=0,Y=1,Z=2,NegX=3,NegY=4,NegZ=5}. The code (single-letter swizzle) selects
// WHICH axis is the plane normal; the sign ("" or "-") flips the half-space for the Neg* variants.
static const char* kAxisCodes[] = {"x", "y", "z", "x", "y", "z"};
static const char* kAxisSigns[] = {"", "", "", "-", "-", "-"};

// ---- PlaneSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) -----------------

// Signed distance to an axis-aligned plane through Center, normal = (optionally negated) selected axis.
// Parity: PlaneSDF.cs GetPreShaderCode + .t3 defaults (Center=(0,0,0), Axis=1=Y). `axis` is a code
// selector (compile-time text), NOT a float param — see file header.
struct PlaneSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  int axis = 1;  // .t3 default Axis = 1 (Y). Code selector (swizzle/sign), NOT a float param.

  explicit PlaneSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix (same shape as
    // SphereSDFNode's "SphereSDF_<shortId>_").
    prefix = "PlaneSDF_" + shortId + "_";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/PlaneSDF.cs:47-51
    //   var a = _axisCodes0[(int)_axis]; var sign = _axisSigns[(int)_axis];
    //   c.AppendCall($"f{c}.w = {sign}(p{c}.{a} - {n}Center.{a});");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;");
    // {n} = node prefix (qualified with P. for MSL struct access); {c} = context id.
    // The axis enum picks the swizzle (a) + sign (s) at codegen time — different axis => different MSL
    // text => different srcHash (matches TiXL FlagCodeChanged on axis change). Default Axis=1 -> a="y",
    // s="" -> f{c}.w = (p{c}.y - P.{n}Center.y).
    const int idx = (axis < 0) ? 0 : (axis > 5 ? 5 : axis);  // guard the table lookup
    const std::string a = kAxisCodes[idx];
    const std::string s = kAxisSigns[idx];
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = " + s + "(p" + ctx + "." + a + " - P." + prefix + "Center." + a +
                 ");");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY Center is a [GraphParam] float param (Axis is a code selector, never packed). VEC3-ONLY:
    // Center at offset 0 -> no padding -> floats[0..2] = packed_float3 Center, a lone struct member.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
  }
};

NodeSpec planeSdfSpec() {
  NodeSpec s;
  s.type = "PlaneSDF";
  s.title = "Plane SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0).
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  // Axis = enum code selector (MappedType=AxisTypes). Widget::Enum + 6 labels, default index 1 (Y).
  // NOT packed into the float buffer (it changes the emitted MSL, not a uniform); the cook would set
  // PlaneSDFNode::axis from this port before assembly. min0 max5 spans the 6 enum values.
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 1.0f; ax.minV = 0.0f; ax.maxV = 5.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z", "NegX", "NegY", "NegZ"};
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, ax, out};
  return s;
}

// Factory: build a PlaneSDFNode for an instance. Center/Axis default to the .t3 values; a graph cook
// would override them from the node's params before assembly (the golden uses the defaults directly).
std::shared_ptr<FieldNode> makePlaneSdf(const std::string& shortId) {
  return std::make_shared<PlaneSDFNode>(shortId);
}

const FieldOp g_planeSdfOp(planeSdfSpec(), makePlaneSdf);

}  // namespace
}  // namespace sw
