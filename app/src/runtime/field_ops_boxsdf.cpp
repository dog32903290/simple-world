// BoxSDF field op (rounded-box SDF leaf on the shader-graph island). TiXL authority:
// external/tixl/Operators/Lib/field/generate/sdf/BoxSDF.cs (+ .t3 defaults). Like SphereSDF this one
// leaf owns BOTH halves of one SDF op: the codegen NODE (BoxSDFNode below — addGlobals/preShaderCode/
// collectParams) AND the OP layer (NodeSpec for the Add menu/findSpec + a FieldNodeFactory so a graph
// walk can instantiate it by type name), registered via the FieldOp self-registration seam. The base
// machinery (FieldNode interface, assembleFieldMSL, param packing) in runtime/field_graph is FROZEN —
// so a new field op = this one .cpp + one CMakeLists line, no shared file edited.
//
//   BoxSDF.cs inputs:  Center (Vector3, [GraphParam]), Size (Vector3), UniformScale (float),
//                      EdgeRadius (float, [GraphParam]).
//   BoxSDF.t3 defaults: Center=(0,0,0), Size=(1,1,1), UniformScale=1.0, EdgeRadius=0.05.
//   AddDefinitions registers a GLOBAL helper `fRoundedRect` (de-duped under key "fRoundedRect").
//   GetPreShaderCode:
//     f{c}.w = fRoundedRect(p{c}.xyz, {n}Center, {n}CombinedScale, {n}EdgeRadius);
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//
// FORK (named, load-bearing): CombinedScale is DERIVED, not a raw param. TiXL's Update() computes
//   ShaderNode.AdditionalParameters[0].Value = Size * UniformScale / 2
// every frame and packs ONE combined `float3 CombinedScale` into the cbuffer (the preShaderCode reads
// {n}CombinedScale, NOT Size/UniformScale). So the host node exposes size(vec3)+uniformScale(float)+
// edgeRadius+center, but collectParams packs the PRE-MULTIPLIED CombinedScale = size*uniformScale/2
// (defaults -> (1,1,1)*1/2 = (0.5,0.5,0.5)). We do NOT pack Size/UniformScale separately — packing the
// single combined param is what keeps the emitted formula text byte-identical to TiXL's.
//
// PARAM ORDER + layout parity (TiXL cbuffer order = [GraphParam] fields then AdditionalParameters):
//   Center(vec3) -> [0..2], EdgeRadius(scalar) -> [3], CombinedScale(vec3) -> [4..6]. 7 floats total.
//   Center is 4-float aligned, EdgeRadius fills its 4th slot, CombinedScale starts a fresh 16B slot
//   (v.size()==4 -> padForVec3 adds 0 padding). FieldParams members:
//     packed_float3 Center; float EdgeRadius; packed_float3 CombinedScale;
//   (packed_float3 = HLSL-cbuffer-tight float3 layout in MSL — see field_graph.cpp appendVec3Param.)
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- BoxSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) -------------------

// Distance-to-rounded-box field leaf. Parity: BoxSDF.cs (AddDefinitions + GetPreShaderCode) +
// BoxSDF.t3 defaults. Host exposes center/size/uniformScale/edgeRadius; collectParams packs the
// derived CombinedScale = size*uniformScale/2 to match TiXL's single combined cbuffer param.
struct BoxSDFNode : FieldNode {
  float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
  float sizeX = 1.f, sizeY = 1.f, sizeZ = 1.f;
  float uniformScale = 1.f;
  float edgeRadius = 0.05f;

  explicit BoxSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix (same as SphereSDFNode).
    prefix = "BoxSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/BoxSDF.cs AddDefinitions:
    //   c.Globals["fRoundedRect"] = "float fRoundedRect(float3 p, float3 center, float3 size, float r){...}"
    // De-duped by key: a graph with two BoxSDF leaves emits the helper exactly once (std::map key).
    // The body is byte-identical to TiXL; float3/abs/length/max/min are common HLSL/MSL syntax.
    c.globals["fRoundedRect"] =
        "float fRoundedRect(float3 p, float3 center, float3 size, float r) {\n"
        "    float3 q = abs(p- center) - size + r;\n"
        "    return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r;\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY external/tixl/Operators/Lib/field/generate/sdf/BoxSDF.cs GetPreShaderCode:
    //   c.AppendCall($"f{c}.w = fRoundedRect(p{c}.xyz, {n}Center, {n}CombinedScale, {n}EdgeRadius);");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;");
    // {n} = node prefix; {c} = context id.
    //
    // HLSL->MSL FORK (named, identical to SphereSDF): TiXL's params live in a global HLSL cbuffer so the
    // snippet reads bare `{n}Center`. In MSL they live in the `constant FieldParams& P` argument, so
    // each param read is qualified `P.{n}...`. The distance MATH is byte-identical; only the
    // cbuffer-vs-struct access syntax differs. Second line uses float3(1.0) (MSL spelling of HLSL `1`).
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = fRoundedRect(p" + ctx + ".xyz, P." + prefix + "Center, P." + prefix +
                 "CombinedScale, P." + prefix + "EdgeRadius);");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // TiXL cbuffer order: [GraphParam] fields (Center, EdgeRadius) then AdditionalParameters
    // (CombinedScale). Center(3) -> [0..2]; EdgeRadius(1) -> [3]; CombinedScale(3) -> [4..6] = 7 floats.
    // CombinedScale is DERIVED (FORK): pack size*uniformScale/2, NOT Size/UniformScale separately — this
    // matches TiXL's Update() so the emitted formula reads one `CombinedScale` param. Let padForVec3
    // (inside appendVec3Param) own the alignment — never hand-roll pads.
    appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
    appendScalarParam(floatParams, paramFields, prefix + "EdgeRadius", edgeRadius);
    const float half = uniformScale * 0.5f;
    appendVec3Param(floatParams, paramFields, prefix + "CombinedScale", sizeX * half, sizeY * half,
                    sizeZ * half);
  }
};

NodeSpec boxSdfSpec() {
  NodeSpec s;
  s.type = "BoxSDF";
  s.title = "Box SDF";
  // Center = Vec3 head run (.x/.y/.z), default (0,0,0). Size = Vec3 head run, default (1,1,1).
  // UniformScale = scalar Float (default 1.0). EdgeRadius = scalar Float (default 0.05).
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
  PortSpec er; er.id = "EdgeRadius"; er.name = "EdgeRadius"; er.dataType = "Float"; er.isInput = true;
  er.def = 0.05f; er.minV = 0.0f; er.maxV = 10.0f;
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {cx, cy, cz, sx, sy, sz, us, er, out};
  return s;
}

// Factory: build a BoxSDFNode for an instance. center/size/uniformScale/edgeRadius default to the .t3
// values; a graph cook would override them from the node's params before assembly (the GPU golden uses
// the .t3 defaults directly).
std::shared_ptr<FieldNode> makeBoxSdf(const std::string& shortId) {
  return std::make_shared<BoxSDFNode>(shortId);
}

// PF-0c param-apply: project a RESOLVED param map onto a BoxSDFNode via setter-lambdas (NOT offsetof).
// Slot ids EQUAL the NodeSpec PortSpec.id (Center.x/.y/.z, Size.x/.y/.z, UniformScale, EdgeRadius). NOTE:
// the host members size*/uniformScale are set RAW — collectParams derives CombinedScale = size*scale/2
// (the TiXL Update() fork), so the apply leaves the derivation untouched. A missing key keeps the member's
// ctor .t3 default (applyFloatSlot's contract). Routed via the fieldConfigurers() sink.
void configureBoxSdf(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<BoxSDFNode*>(&node)) {
    applyFloatSlot(m, "Center.x", [&](float v) { n->centerX = v; });
    applyFloatSlot(m, "Center.y", [&](float v) { n->centerY = v; });
    applyFloatSlot(m, "Center.z", [&](float v) { n->centerZ = v; });
    applyFloatSlot(m, "Size.x", [&](float v) { n->sizeX = v; });
    applyFloatSlot(m, "Size.y", [&](float v) { n->sizeY = v; });
    applyFloatSlot(m, "Size.z", [&](float v) { n->sizeZ = v; });
    applyFloatSlot(m, "UniformScale", [&](float v) { n->uniformScale = v; });
    applyFloatSlot(m, "EdgeRadius", [&](float v) { n->edgeRadius = v; });
  }
}

const FieldOp g_boxSdfOp(boxSdfSpec(), makeBoxSdf, configureBoxSdf);

}  // namespace
}  // namespace sw
