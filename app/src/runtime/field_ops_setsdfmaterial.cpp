// SetSDFMaterial — field ADJUST op that paints a material color onto an SDF surface. When the carried
// field state `p.w` is in the range (0.5, 1.5) (the "inside-surface" band TiXL marks for material
// application), the node overwrites f.rgb with a Color parameter (optionally blended with a second
// ColorField's rgb). It is a pure field→field op: both inputs are ShaderGraphNode (field) and the
// output is ShaderGraphNode (field). The only non-field input is `Color` [GraphParam] (Vector4).
//
// TiXL authority: external/tixl/Operators/Lib/field/adjust/SetSDFMaterial.cs (TryBuildCustomCode)
//   Inputs: SdfField (inputs[0]), ColorField (inputs[1], optional), Color [GraphParam] (Vector4).
//   TryBuildCustomCode:
//     1. Collect SdfField in the PARENT context (inputs[0]?.CollectEmbeddedShaderCode(c)).
//     2. Emit `if(p{c}.w > 0.5 && p{c}.w < 1.5) {`; c.Indent().
//     3. If ColorField connected:
//          c.PushContext(subContextIndex, "albedo"); collect ColorField; c.PopContext();
//          c.AppendCall(`f{parent}.rgb = f{albedo}.rgb * {ShaderNode}Color.rgb;`);
//        Else:
//          c.AppendCall(`f{parent} = float4({ShaderNode}Color.rgb, f{parent}.w);`);
//     4. c.Unindent(); c.AppendCall("}").
//
// The {ShaderNode} token in TiXL expands to `ShaderNode.ToString()` = the node's unique id string,
// which in sw maps to `prefix` (the "TypeName_shortId_" convention).  All param accesses become
// `P.{prefix}Color` (sw frozen HLSL->MSL struct convention).
//
// Forks vs SetSDFMaterial.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — {ShaderNode}Color → P.<prefix>Color (sw frozen convention).
//       Wrong prefix reads the wrong/0 struct member → golden probe bites.
//   (2) HLSL `float4({ShaderNode}Color.rgb, f{parent}.w)` → MSL has the same float4(...) constructor
//       syntax. `.rgb` swizzle on float4 is valid in both HLSL and MSL. No fork.
//   (3) p{c}.w gate (0.5, 1.5): `p{c}.w` is the position's w channel. In TiXL the field state
//       carries material-band markers in p.w; the golden exercises the gate by checking that with
//       p.w == 1.0 (in-band) the color overwrites, and p.w == 0.0 (out-of-band) it does not.
//   (4) custom-collect via tryBuildCustomCode: SdfField is collected in the PARENT context (stack
//       unchanged, so it writes into f{parent}), then the optional ColorField is collected into its
//       own "albedo" subcontext (depth = contextIdStack.size() at the point of push), exactly as
//       TiXL does with c.ContextIdStack.Count.
//   (5) HLSL `f{parent}.rgb = f{albedo}.rgb * {ShaderNode}Color.rgb` — `.rgb * float3` is a
//       component-wise multiply (float3 * float3). MSL supports this identically. No fork.
//
// Vector4 packing: Color is a float4 [GraphParam]. TiXL AddVec4Parameter pads to a 16B boundary then
// pushes x,y,z,w and declares `float4 <prefix>Color`. sw uses appendVec4Param (same semantics).
// The golden's Color default is (1,1,1,1) from the .t3 file.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec4Param, collectFieldCode
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- SetSDFMaterial codegen node (a FieldNode subclass; custom-collect adjust op) -----------------

struct SetSDFMaterialNode : FieldNode {
  // [GraphParam] Color (Vector4). .t3 default = (1,1,1,1).
  float colorR = 1.f, colorG = 1.f, colorB = 1.f, colorA = 1.f;
  // test-only bug modes: 0=none, 1=invert the p.w gate (> becomes <=) so in-band pixels miss.
  int injectBug = 0;

  explicit SetSDFMaterialNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix.
    prefix = "SetSDFMaterial_" + shortId + "_";
  }

  bool tryBuildCustomCode(CodeAssembleCtx& c) const override {
    // PARITY SetSDFMaterial.cs TryBuildCustomCode.
    // inputs[0] = SdfField (accumulator, parent context). inputs[1] = ColorField (optional albedo).
    if (inputs.empty()) return true;

    const FieldNode* sdfField = inputs.size() > 0 ? inputs[0].get() : nullptr;
    const FieldNode* colorField = inputs.size() > 1 ? inputs[1].get() : nullptr;

    // 1. SdfField -> PARENT context (fork 4): recurse with stack unchanged so it writes into f{parent}.
    if (sdfField) collectFieldCode(*sdfField, c);

    const std::string parent = c.ctx();

    // 2. Emit the p.w gate: `if(p{c}.w > 0.5 && p{c}.w < 1.5) {`.
    // injectBug==1: invert to `<= 0.5 || >= 1.5` so in-band probes miss -> golden tooth RED.
    if (injectBug == 1) {
      c.appendCall("if(p" + parent + ".w <= 0.5 || p" + parent + ".w >= 1.5) {");
    } else {
      c.appendCall("if(p" + parent + ".w > 0.5 && p" + parent + ".w < 1.5) {");
    }
    c.indentCount++;

    if (colorField) {
      // 3a. ColorField -> "albedo" subcontext (fork 4: depth == contextIdStack.size() at push point).
      const int subIndex = static_cast<int>(c.contextIdStack.size());
      c.pushContext(subIndex, "albedo");
      const std::string sub = c.ctx();
      collectFieldCode(*colorField, c);
      c.popContext();
      // f{parent}.rgb = f{albedo}.rgb * P.<prefix>Color.rgb  (fork 5: .rgb*.rgb component-wise)
      c.appendCall("f" + parent + ".rgb = f" + sub + ".rgb * P." + prefix + "Color.rgb;");
    } else {
      // 3b. No ColorField: overwrite f{parent} entirely with Color (keep .w from the sdf pass).
      c.appendCall("f" + parent + " = float4(P." + prefix + "Color.rgb, f" + parent + ".w);");
    }

    c.indentCount--;
    c.appendCall("}");
    return true;
  }

  // tryBuildCustomCode owns the whole emit; preShaderCode is pure-virtual, provide empty override.
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] declaration order (SetSDFMaterial.cs): Color (Vector4) is the only GraphParam.
    // appendVec4Param pads to 16B boundary then pushes r,g,b,a, declares `float4 <prefix>Color`.
    appendVec4Param(floatParams, paramFields, prefix + "Color", colorR, colorG, colorB, colorA);
  }
};

NodeSpec setSDFMaterialSpec() {
  NodeSpec s;
  s.type = "SetSDFMaterial";
  s.title = "Set SDF Material";
  // SdfField input (Field, accumulator SDF that carries the p.w material band marker).
  PortSpec sf; sf.id = "SdfField";    sf.name = "Sdf Field";    sf.dataType = "Field"; sf.isInput = true;
  // ColorField input (Field, optional albedo source whose f.rgb modulates the Color param).
  PortSpec cf; cf.id = "ColorField";  cf.name = "Color Field";  cf.dataType = "Field"; cf.isInput = true;
  // Color [GraphParam] (float4, RGBA). .t3 default (1,1,1,1).
  PortSpec cr; cr.id = "Color.r"; cr.name = "Color"; cr.dataType = "Float"; cr.isInput = true;
  cr.def = 1.0f; cr.minV = 0.0f; cr.maxV = 1.0f; cr.widget = Widget::Vec; cr.vecArity = 4;
  PortSpec cg; cg.id = "Color.g"; cg.name = "Color.g"; cg.dataType = "Float"; cg.isInput = true;
  cg.def = 1.0f; cg.minV = 0.0f; cg.maxV = 1.0f;
  PortSpec cb; cb.id = "Color.b"; cb.name = "Color.b"; cb.dataType = "Float"; cb.isInput = true;
  cb.def = 1.0f; cb.minV = 0.0f; cb.maxV = 1.0f;
  PortSpec ca; ca.id = "Color.a"; ca.name = "Color.a"; ca.dataType = "Float"; ca.isInput = true;
  ca.def = 1.0f; ca.minV = 0.0f; ca.maxV = 1.0f;
  // Output: Field (ShaderGraphNode).
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {sf, cf, cr, cg, cb, ca, out};
  return s;
}

std::shared_ptr<FieldNode> makeSetSDFMaterial(const std::string& shortId) {
  return std::make_shared<SetSDFMaterialNode>(shortId);
}

void configureSetSDFMaterialOp(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<SetSDFMaterialNode*>(&node)) {
    applyFloatSlot(m, "Color.r", [&](float v) { n->colorR = v; });
    applyFloatSlot(m, "Color.g", [&](float v) { n->colorG = v; });
    applyFloatSlot(m, "Color.b", [&](float v) { n->colorB = v; });
    applyFloatSlot(m, "Color.a", [&](float v) { n->colorA = v; });
  }
}

// slot ids = the SAME ids configureSetSDFMaterialOp applies (Option B guard reads them).
const FieldOp g_setSDFMaterialOp(setSDFMaterialSpec(), makeSetSDFMaterial,
                                  configureSetSDFMaterialOp,
                                  {"Color.r", "Color.g", "Color.b", "Color.a"});

}  // namespace (anonymous)

// Param-cook + test seam (leaf type TU-private). Used by the golden (forward-declared there).
// Production passes injectBug=0 (color applied faithfully). injectBug=1 inverts the p.w gate so
// in-band pixels miss → golden tooth RED. Defined here outside anonymous ns: dynamic_cast back to
// the TU-private SetSDFMaterialNode is legal within the same TU.
void configureSetSDFMaterial(FieldNode& node, float r, float g, float b, float a, int injectBug) {
  if (auto* n = dynamic_cast<SetSDFMaterialNode*>(&node)) {
    n->colorR = r;
    n->colorG = g;
    n->colorB = b;
    n->colorA = a;
    n->injectBug = injectBug;
  }
}

}  // namespace sw
