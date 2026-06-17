// BoxFrameSDF field op (field self-registration seam leaf — a Phase-C SDF fan-out on the proven
// shader-graph island). TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/BoxFrameSDF.cs
// (+ .t3 defaults). This single leaf owns BOTH halves of one SDF op: the codegen NODE
// (BoxFrameSDFNode below — addGlobals / preShaderCode / collectParams, exercised by
// --selftest-field-render via the FieldOp factory) AND the OP layer (a NodeSpec so it appears in the
// Add menu / findSpec, plus a FieldNodeFactory so a graph walk can instantiate it by type name),
// registered via the FieldOp self-registration seam. The codegen base machinery (FieldNode interface,
// assembleFieldMSL, param packing) lives in runtime/field_graph and is FROZEN — so adding a field op =
// this one .cpp + one CMakeLists line, no shared file edited.
//
//   BoxFrameSDF.cs inputs:  Center (Vector3, [GraphParam]), Size (Vector3), UniformScale (float),
//                           Thickness (float, [GraphParam]).
//   BoxFrameSDF.t3 defaults: Center=(0,0,0), Size=(1,1,1), UniformScale=1.0, Thickness=0.05.
//   DERIVED (BoxFrameSDF.cs Update): CombinedScale = Size * UniformScale / 2 = (0.5,0.5,0.5) at
//                                    defaults. It is an AdditionalParameter ("float3 CombinedScale"),
//                                    NOT a raw input — so the shader reads CombinedScale (the box
//                                    half-extents b), never Size/UniformScale directly.
//   AddDefinitions: registers the reusable helper `fBoxFrame` into c.Globals["fBoxFrame"].
//   GetPreShaderCode:
//     f{c}.w = fBoxFrame(p{c}.xyz, {n}Center, {n}CombinedScale, {n}Thickness);
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//
// GLOBALS-HELPER FORK vs SphereSDF (load-bearing for this op): SphereSDF's distance was inline in the
// call. BoxFrameSDF emits a reusable GLOBAL helper via addGlobals() — this is the first field leaf to
// exercise the GLOBALS hook path (field_graph.cpp: collectEmbeddedShaderCode calls addGlobals, and
// assembleFieldMSL injects cac.globals into /*{GLOBALS}*/). The helper body is byte-identical MSL to
// TiXL's HLSL (length/max/min/float3 are common syntax); de-dup is by the map key "fBoxFrame".
//
// HLSL->MSL access FORK (same as SphereSDF): TiXL reads params as bare cbuffer names {n}Center; MSL
// params live inside `constant FieldParams& P`, so each read is qualified `P.{n}...`. (The fBoxFrame
// helper itself takes plain float3/float args — only the CALL SITE qualifies the param reads.)
//
// PARAM ORDER + layout parity: only the two [GraphParam] fields (Center, Thickness) plus the derived
// CombinedScale become shader params (Size/UniformScale are consumed CPU-side into CombinedScale).
// Order: Center (vec3) -> Thickness (scalar) -> CombinedScale (vec3). Center occupies floats[0..2],
// Thickness floats[3] (one tight 16B slot, no pad). CombinedScale starts at floats[4] (slot start, no
// pad). Total 7 floats. (padForVec3 handles all alignment — never hand-rolled here.)
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- BoxFrameSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) --------------

// Distance-to-box-frame field leaf. Parity: BoxFrameSDF.cs AddDefinitions + GetPreShaderCode +
// BoxFrameSDF.t3 defaults. The hollow-box-frame SDF (Inigo Quilez's fBoxFrame). Params held in the
// shape the shader reads them: Center, Thickness (the frame edge thickness e), and the DERIVED
// CombinedScale (the box half-extents b = Size*UniformScale/2).
struct BoxFrameSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float thickness = 0.05f;
  // Derived (BoxFrameSDF.cs Update: Size*UniformScale/2). At .t3 defaults (Size=(1,1,1),
  // UniformScale=1) this is (0.5,0.5,0.5). A graph-cook would recompute it from Size/UniformScale
  // before assembly; the factory/golden uses the .t3 default directly.
  float scaleX = 0.5f, scaleY = 0.5f, scaleZ = 0.5f;

  explicit BoxFrameSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix.
    prefix = "BoxFrameSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/BoxFrameSDF.cs:AddDefinitions
    //   c.Globals["fBoxFrame"] = """ ... """;  (the reusable hollow-box-frame distance helper)
    // De-duped by the map key "fBoxFrame": N BoxFrameSDF nodes emit ONE copy. The body is
    // byte-identical MSL to TiXL's HLSL — length/max/min/float3 are common HLSL/MSL syntax, and the
    // helper reads only its plain float3/float args (no cbuffer access), so no `P.` qualification is
    // needed inside it (unlike the call site).
    c.globals["fBoxFrame"] =
        "float fBoxFrame(float3 p, float3 center, float3 b, float e) {\n"
        "     p = abs(p-center  )-b;\n"
        "     float3 q = abs(p+e)-e;\n"
        "return min(min(\n"
        "    length(max(float3(p.x,q.y,q.z),0.0))+min(max(p.x,max(q.y,q.z)),0.0),\n"
        "    length(max(float3(q.x,p.y,q.z),0.0))+min(max(q.x,max(p.y,q.z)),0.0)),\n"
        "    length(max(float3(q.x,q.y,p.z),0.0))+min(max(q.x,max(q.y,p.z)),0.0));\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/BoxFrameSDF.cs:GetPreShaderCode
    //   c.AppendCall($"f{c}.w = fBoxFrame(p{c}.xyz, {n}Center, {n}CombinedScale, {n}Thickness);");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // HLSL->MSL FORK: each param read is qualified with `P.` (params live in `constant FieldParams& P`
    // in MSL, vs a global cbuffer in HLSL). The fBoxFrame call MATH is byte-identical; only the
    // param-access syntax differs. Arg order matches the helper signature (center, b=CombinedScale,
    // e=Thickness).
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = fBoxFrame(p" + ctx + ".xyz, P." + prefix + "Center, P." + prefix +
                 "CombinedScale, P." + prefix + "Thickness);");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // Param order (the order the shader struct + buffer are packed): Center (vec3) -> Thickness
    // (scalar) -> CombinedScale (vec3). Center(3)+Thickness(1) = one tight 16B slot [0..3]; then
    // CombinedScale starts at index 4 (a slot boundary, padForVec3 adds no pad there). Total 7 floats.
    // Size/UniformScale are NOT params: BoxFrameSDF.cs folds them into CombinedScale CPU-side.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "Thickness", thickness);
    appendVec3Param(floatParams, paramFields, prefix + "CombinedScale", scaleX, scaleY, scaleZ);
  }
};

NodeSpec boxFrameSdfSpec() {
  NodeSpec s;
  s.type = "BoxFrameSDF";
  s.title = "Box Frame SDF";
  // Inputs mirror BoxFrameSDF.cs / .t3 (Center, Size, UniformScale, Thickness). CombinedScale is
  // derived, not an input port. Center/Size are Vec3 head-runs (.x/.y/.z); UniformScale/Thickness
  // scalar Floats.
  PortSpec cx; cx.id = "Center.x"; cx.name = "Center"; cx.dataType = "Float"; cx.isInput = true;
  cx.def = 0.0f; cx.minV = -10.0f; cx.maxV = 10.0f; cx.widget = Widget::Vec; cx.vecArity = 3;
  PortSpec cy; cy.id = "Center.y"; cy.name = "Center.y"; cy.dataType = "Float"; cy.isInput = true;
  cy.def = 0.0f; cy.minV = -10.0f; cy.maxV = 10.0f;
  PortSpec cz; cz.id = "Center.z"; cz.name = "Center.z"; cz.dataType = "Float"; cz.isInput = true;
  cz.def = 0.0f; cz.minV = -10.0f; cz.maxV = 10.0f;

  PortSpec sx; sx.id = "Size.x"; sx.name = "Size"; sx.dataType = "Float"; sx.isInput = true;
  sx.def = 1.0f; sx.minV = 0.0f; sx.maxV = 10.0f; sx.widget = Widget::Vec; sx.vecArity = 3;
  PortSpec sy; sy.id = "Size.y"; sy.name = "Size.y"; sy.dataType = "Float"; sy.isInput = true;
  sy.def = 1.0f; sy.minV = 0.0f; sy.maxV = 10.0f;
  PortSpec sz; sz.id = "Size.z"; sz.name = "Size.z"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 1.0f; sz.minV = 0.0f; sz.maxV = 10.0f;

  PortSpec us; us.id = "UniformScale"; us.name = "UniformScale"; us.dataType = "Float"; us.isInput = true;
  us.def = 1.0f; us.minV = 0.0f; us.maxV = 10.0f;
  PortSpec th; th.id = "Thickness"; th.name = "Thickness"; th.dataType = "Float"; th.isInput = true;
  th.def = 0.05f; th.minV = 0.0f; th.maxV = 10.0f;

  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, sx, sy, sz, us, th, out};
  return s;
}

// Factory: build a BoxFrameSDFNode for an instance. Center/Thickness/CombinedScale default to the
// .t3-derived values; a graph cook would override them (recomputing CombinedScale from Size*UniformScale/2)
// before assembly. The render golden uses the defaults directly.
std::shared_ptr<FieldNode> makeBoxFrameSdf(const std::string& shortId) {
  return std::make_shared<BoxFrameSDFNode>(shortId);
}

const FieldOp g_boxFrameSdfOp(boxFrameSdfSpec(), makeBoxFrameSdf);

}  // namespace
}  // namespace sw
