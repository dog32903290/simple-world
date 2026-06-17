// PyramidSDF field op (zero-shared-file leaf on the field self-registration seam). Like SphereSDF /
// OctahedronSDF / TorusSDF this single .cpp owns BOTH halves of one SDF op: the codegen NODE
// (PyramidSDFNode below) AND the OP layer (a NodeSpec for the Add menu / findSpec + a FieldNodeFactory
// so a graph walk can instantiate it by type name), registered via the file-scope FieldOp registrar.
// The base machinery (FieldNode interface, assembleFieldMSL, param packing) stays FROZEN in
// runtime/field_graph — adding a field op = this one .cpp + one CMakeLists line, no shared file edited.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/PyramidSDF.cs (+ PyramidSDF.t3).
//   inputs ([GraphParam] reflection / declaration order): Center (Vector3), Scale (Vector3),
//     UniformScale (float), Rounding (float).  PLUS Axis (InputSlot<int>, MappedType=AxisTypes{X,Y,Z})
//     — NOT a [GraphParam], a CODE SELECTOR (see AXIS note below).
//   .t3 defaults: Center=(0,0,0), Scale=(1,1,1), UniformScale=1.0, Rounding=0.05, Axis=1 (Y)
//     — mirrored in the ctor.
//   AddDefinitions: registers Globals["fPyramid"] (the helper fn) — a GLOBAL helper via addGlobals
//     (like OctahedronSDF / TorusSDF, unlike SphereSDF / PlaneSDF which are fully inline).
//   GetPreShaderCode (a = _axisCodes0[axis]):
//     f{c}.w = fPyramid(p{c}.<a>, {n}Center.<a>,
//                       {n}Scale.x*{n}UniformScale, {n}Scale.z*{n}UniformScale,
//                       {n}Scale.y*{n}UniformScale, {n}Rounding);
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//
// AXIS = COMPILE-TIME CODE SELECTOR (NOT a runtime uniform), the load-bearing seam of this leaf.
//   In TiXL `Axis` is an InputSlot<int> (MappedType=AxisTypes); it is *not* tagged [GraphParam], so it
//   is NEVER appended to the shader's float param buffer. PyramidSDF.Update() reads Axis and calls
//   ShaderNode.FlagCodeChanged() when it changes — the axis re-WRITES the emitted shader text (a
//   different swizzle), it is NOT fed through the float buffer. We mirror this exactly: the axis lives
//   as an int member on the node (default 1 = Y from the .t3), set in the ctor, and is looked up in
//   preShaderCode to build the call string (different axis => different MSL text => different srcHash).
//   It is DELIBERATELY absent from collectParams — packing it would corrupt the float layout (and the
//   golden). The kAxisCodes table below is copied VERBATIM from PyramidSDF.cs `_axisCodes0` (file-scope
//   so it has no per-node duplication).
//
// ★ TWO consecutive vec3 [GraphParam]s (Center, Scale): collectParams calls appendVec3Param twice in
//   declaration order. padForVec3 inserts the inter-vec3 pad automatically (do NOT hand-roll it):
//     appendVec3Param("Center"): buffer size 0, start%4==0 -> 0 pad -> Center = floats[0..2].
//     appendVec3Param("Scale") : buffer size 3, start%4==3 -> 1 pad float (floats[3]) -> Scale=[4..6].
//     appendScalarParam("UniformScale") -> floats[7].
//     appendScalarParam("Rounding")     -> floats[8].
//   Total 9 floats. Struct field order (FLOAT_PARAMS hook):
//     packed_float3 Center; float __padding4; packed_float3 Scale; float UniformScale; float Rounding;
//   (the pad name is emitted by appendVec3Param/padForVec3, not by us). This matches TiXL's HLSL
//   cbuffer where two consecutive float3s straddle 16-byte registers with one pad float between them.
//
// DERIVED TERMS stay in the CALL TEXT (packed raw): the helper takes halfWidth/halfDepth/halfHeight/ra,
//   but PyramidSDF.cs passes Scale.x*UniformScale (etc.) as the arguments — the multiply lives in the
//   emitted MSL, so we pack Scale + UniformScale RAW and write the `*` into the call string. Center is
//   passed as the helper's `center` ARG (the helper subtracts it INSIDE), swizzled by `a`, NOT in the
//   call expression (mirrors fPyramid(p{c}.<a>, {n}Center.<a>, ...)).
//
// HLSL->MSL forks honored: (1) the packed_float3 alignment fork (handled by appendVec3Param), here for
//   TWO vec3 head-runs with one pad between. (2) the cbuffer-vs-struct param access: TiXL reads bare
//   `{n}Name` from a global cbuffer; MSL reads `P.{n}Name` from the `constant FieldParams& P` argument
//   — we emit the `P.` prefix here. The helper body math (max/min/dot/clamp(vec,vec,vec)/sqrt, all
//   identical in MSL) and the call's `*` / swizzle are byte-identical to the .cs — NO math fork.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// VERBATIM copy of PyramidSDF.cs `_axisCodes0` (file-scope: index = AxisTypes value X=0,Y=1,Z=2).
// This is the compile-time swizzle the Axis selector emits into the generated MSL. DEFAULT Axis=1 (Y)
// -> a = "xyz" (identity, clean emitted text).
static const char* kAxisCodes[] = {"yxz", "xyz", "xzy"};

// ---- PyramidSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) ---------------

// Distance-to-pyramid field leaf. Parity: PyramidSDF.cs AddDefinitions + GetPreShaderCode +
// PyramidSDF.t3 defaults (Center=(0,0,0), Scale=(1,1,1), UniformScale=1.0, Rounding=0.05, Axis=1/Y).
// The four [GraphParam] floats (two vec3 + two scalar) are collected in field-declaration order
// (Center, Scale, UniformScale, Rounding); `axis` is a code selector (see header AXIS note) held as an
// int member, NOT packed.
struct PyramidSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float scaleX = 1.f, scaleY = 1.f, scaleZ = 1.f;
  float uniformScale = 1.0f;
  float rounding = 0.05f;
  // Axis code selector — .t3 default 1 (Y) -> kAxisCodes[1] = "xyz" (identity, clean emitted text).
  // Set in the ctor; the golden uses this default so no cook wiring is needed this batch.
  int axis = 1;

  explicit PyramidSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix (same shape as the
    // other SDF leaves' "<Type>_<shortId>_").
    prefix = "PyramidSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/PyramidSDF.cs GetPreShaderCode:
    //   c.Globals["fPyramid"] = "float fPyramid(...) { ... }";
    // De-duped by key (std::map::operator[]): two PyramidSDF nodes -> the helper appears exactly once
    // (TiXL Globals semantics). Body byte-identical to the .cs (max/min/dot/clamp(vec,vec,vec)/sqrt
    // all exist identically in MSL — no math fork). float3(0., 0., 0.) / float3(...) literals are
    // valid MSL.
    c.globals["fPyramid"] =
        "float fPyramid(float3 p, float3 center, float halfWidth, float halfDepth, float halfHeight, float ra) {\n"
        "p -= center;\n"
        "p.y += halfHeight;\n"
        "p.xz = abs(p.xz);\n"
        "float3 d1 = float3(max(p.x - halfWidth, 0.0), p.y, max(p.z - halfDepth, 0.0));\n"
        "float3 n1 = float3(0.0, halfDepth, 2.0 * halfHeight);\n"
        "float k1 = dot(n1, n1);\n"
        "float h1 = dot(p - float3(halfWidth, 0.0, halfDepth), n1) / k1;\n"
        "float3 n2 = float3(k1, 2.0 * halfHeight * halfWidth, -halfDepth * halfWidth);\n"
        "float m1 = dot(p - float3(halfWidth, 0.0, halfDepth), n2) / dot(n2, n2);\n"
        "float3 d2 = p - clamp(p - n1 * h1 - n2 * max(m1, 0.0), float3(0., 0., 0.), float3(halfWidth, 2.0 * halfHeight, halfDepth));\n"
        "float3 n3 = float3(2.0 * halfHeight, halfWidth, 0.0);\n"
        "float k2 = dot(n3, n3);\n"
        "float h2 = dot(p - float3(halfWidth, 0.0, halfDepth), n3) / k2;\n"
        "float3 n4 = float3(-halfWidth * halfDepth, 2.0 * halfHeight * halfDepth, k2);\n"
        "float m2 = dot(p - float3(halfWidth, 0.0, halfDepth), n4) / dot(n4, n4);\n"
        "float3 d3 = p - clamp(p - n3 * h2 - n4 * max(m2, 0.0), float3(0., 0., 0.), float3(halfWidth, 2.0 * halfHeight, halfDepth));\n"
        "float d = sqrt(min(min(dot(d1, d1), dot(d2, d2)), dot(d3, d3)));\n"
        "return (max(max(h1, h2), -p.y) < 0.0 ? -d : d) - ra;\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY PyramidSDF.cs GetPreShaderCode:
    //   var a = _axisCodes0[(int)_axis];
    //   c.AppendCall($"f{c}.w = fPyramid(p{c}.{a}, {n}Center.{a}, {n}Scale.x * {n}UniformScale,
    //                   {n}Scale.z * {n}UniformScale, {n}Scale.y * {n}UniformScale, {n}Rounding);");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // {n} = node prefix (qualified P. for MSL struct access); {c} = context id; a = swizzle code.
    // The swizzle `a` is applied to BOTH p{c} and Center (matching the .cs). Scale is remapped
    // x/z/y -> halfWidth/halfDepth/halfHeight, each multiplied by UniformScale IN THE CALL TEXT
    // (derived term stays in the emitted MSL; Scale + UniformScale are packed RAW). Center is passed
    // as the helper's center ARG (subtracted inside fPyramid), NOT subtracted in the call.
    const std::string ctx = c.ctx();
    const std::string a = kAxisCodes[axis];  // compile-time swizzle selected by the Axis code member
    c.appendCall("f" + ctx + ".w = fPyramid(p" + ctx + "." + a + ", P." + prefix + "Center." + a +
                 ", P." + prefix + "Scale.x * P." + prefix + "UniformScale" +
                 ", P." + prefix + "Scale.z * P." + prefix + "UniformScale" +
                 ", P." + prefix + "Scale.y * P." + prefix + "UniformScale" +
                 ", P." + prefix + "Rounding);");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] declaration order = Center (vec3) -> Scale (vec3) -> UniformScale (scalar) ->
    // Rounding (scalar), matching PyramidSDF.cs reflection order. Axis is NOT packed (code selector,
    // see header AXIS note). TWO consecutive vec3s: padForVec3 inserts the pad float between Center
    // and Scale automatically (do NOT hand-roll). Layout:
    //   Center=floats[0..2], pad=floats[3], Scale=floats[4..6], UniformScale=floats[7], Rounding[8].
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendVec3Param(floatParams, paramFields, prefix + "Scale", scaleX, scaleY, scaleZ);
    appendScalarParam(floatParams, paramFields, prefix + "UniformScale", uniformScale);
    appendScalarParam(floatParams, paramFields, prefix + "Rounding", rounding);
  }
};

NodeSpec pyramidSdfSpec() {
  NodeSpec s;
  s.type = "PyramidSDF";
  s.title = "Pyramid SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0).
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;
  // Scale = Vec3 head run (.x/.y/.z), default (1,1,1).
  PortSpec sx; sx.id = "Scale.x"; sx.name = "Scale"; sx.dataType = "Float"; sx.isInput = true;
  sx.def = 1.0f; sx.minV = 0.0f; sx.maxV = 10.0f; sx.widget = Widget::Vec; sx.vecArity = 3;
  PortSpec sy; sy.id = "Scale.y"; sy.name = "Scale.y"; sy.dataType = "Float"; sy.isInput = true;
  sy.def = 1.0f; sy.minV = 0.0f; sy.maxV = 10.0f;
  PortSpec sz; sz.id = "Scale.z"; sz.name = "Scale.z"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 1.0f; sz.minV = 0.0f; sz.maxV = 10.0f;
  // UniformScale, Rounding = scalar Floats.
  PortSpec us; us.id = "UniformScale"; us.name = "UniformScale"; us.dataType = "Float"; us.isInput = true;
  us.def = 1.0f; us.minV = 0.0f; us.maxV = 10.0f;
  PortSpec rn; rn.id = "Rounding"; rn.name = "Rounding"; rn.dataType = "Float"; rn.isInput = true;
  rn.def = 0.05f; rn.minV = 0.0f; rn.maxV = 10.0f;
  // Axis = enum CODE SELECTOR (X/Y/Z). It is a Float port (storing the enum index) with
  // widget=Widget::Enum + labels — drawn as a dropdown like CompareInt's Mode. .t3 default 1 (Y).
  // It is NOT a [GraphParam] in TiXL (never packed); the node's `axis` int member carries it at
  // codegen time. (The factory could read params["Axis"] into node->axis; the golden uses the ctor
  // default 1 so no cook wiring is needed this batch.)
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 1.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, sx, sy, sz, us, rn, ax, out};
  return s;
}

// Factory: build a PyramidSDFNode for an instance. Center/Scale/UniformScale/Rounding/Axis default to
// the .t3 values (baked in the ctor); a graph cook would override them from the node's params before
// assembly (the golden uses the defaults directly).
std::shared_ptr<FieldNode> makePyramidSdf(const std::string& shortId) {
  return std::make_shared<PyramidSDFNode>(shortId);
}

const FieldOp g_pyramidSdfOp(pyramidSdfSpec(), makePyramidSdf);

}  // namespace
}  // namespace sw
