// ReflectField — single-input field MODIFIER (PRE-wrap): reflects the sampling point across a plane
// BEFORE the wrapped field is evaluated. Like Translate, this op drives the PRE half of the single-input
// wrap branch (field_graph.cpp:82-86): it emits preShaderCode (executed BEFORE recursing the child), so
// the child samples the reflected point. The reflection mirrors the half-space on the negative side of
// the plane onto the positive side (hg_sdf pReflect), producing mirror symmetry of the wrapped shape.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/ReflectField.cs
//   GetPreShaderCode(c, inputIndex):
//     c.AppendCall($"pReflect(p{c}.xyz, normalize({ShaderNode}PlaneNormal), {ShaderNode}Offset);"); (:44)
//   AddDefinitions: c.Globals["pReflect"] = the hg_sdf pReflect helper (:28-40).
//   [GraphParam] InputSlot<Vector3> PlaneNormal; [GraphParam] InputSlot<float> Offset; one InputField;
//   one Slot<ShaderGraphNode> Result. Branch = single-input PRE-wrap (NO post code).
//
// Forks vs ReflectField.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literals `{ShaderNode}PlaneNormal` / `{ShaderNode}Offset`,
//       where {ShaderNode} interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_".
//       sw's frozen convention (backward-traced from field_ops_combinesdf.cpp:288 / translate.cpp:46 —
//       `prefix = "<Type>_" + shortId + "_"`, accessed `P.<prefix><Name>`) reproduces EXACTLY that name.
//       Emitted tokens: `P.ReflectField_<id>_PlaneNormal` / `P.ReflectField_<id>_Offset`. NOT a forward-
//       assumed literal; a wrong prefix would read the wrong/0 struct member and the golden catches it.
//   (2) HLSL->MSL: the helper signature `void pReflect(inout float3 p, ...)` -> `thread float3& p`
//       (HLSL inout -> MSL thread X&, only inside the helper). The `//return sgn(t);` line is COMMENTED
//       OUT in TiXL (ReflectField.cs:38) -> kept out here (no sgn helper, no return). NO inter-helper
//       call (pReflect calls only dot/builtins) -> NO MSL forward-decl prototype needed (fork-5 N/A).
//   (3) PlaneNormal = Vector3 packed via appendVec3Param (packed_float3, 16B-align rule) — same path
//       BoxSDF's Center / Translate's Translation use. Offset = scalar via appendScalarParam. Param
//       order matches collectParams emission (PlaneNormal head run, then Offset).
//   Test-only seam: configureReflectField sets the REAL PlaneNormal/Offset AND an injectBug that corrupts
//   the OP'S REAL preShaderCode emit (drop the pReflect call / wrong normal), so the golden's tooth bites
//   the op's emit, not an expected-value tautology. Production default off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// pReflect helper — byte-verbatim from ReflectField.cs:28-40 AddDefinitions, with the frozen MSL forks:
//   HLSL `inout float3 p` -> MSL `thread float3& p`. The `//return sgn(t);` stays commented (TiXL :38).
//   Body math (dot / the conditional p = p - 2t*planeNormal) is identical text. No inter-helper call.
static const char* kBodyReflect =
    "// https://mercury.sexy/hg_sdf/\n"
    "// Reflect space at a plane\n"
    "float3 pReflect(float3 p, float3 planeNormal, float offset) {\n"
    "\tfloat t = dot(p, planeNormal)+offset;\n"
    "\tif (t < 0) {\n"
    "\t\tp = p - (2*t)*planeNormal;\n"
    "\t}\n"
    "\t//return sgn(t);\n"
    "\treturn p;\n"
    "}";

// ---- ReflectField codegen node (a FieldNode subclass; single-input modifier — PRE-wrap path) -------

struct ReflectFieldNode : FieldNode {
  // ReflectField.t3 defaults: PlaneNormal = (0,0,0) (host normalize of 0 is UB-ish but TiXL leaves the
  // raw default; the golden always sets a real normal). Offset = 0. PlaneNormal packed [GraphParam].
  float nx = 0.f, ny = 0.f, nz = 0.f;
  float offset = 0.f;
  // test-only bug modes (configureReflectField): 0 = none, 1 = drop the pReflect call (no reflection),
  // 2 = corrupt the normal (read a zeroed normal -> reflection never triggers, like dropping it).
  int injectBug = 0;

  explicit ReflectFieldNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "ReflectField_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // ReflectField.cs:26-40 — register the single hg_sdf pReflect helper (key "pReflect", de-duped by
    // std::map). No secondary helper; pReflect calls only builtins -> no forward-decl prototype needed.
    c.globals["pReflect"] = kBodyReflect;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY ReflectField.cs:44 GetPreShaderCode:
    //   `pReflect(p{c}.xyz, normalize({ShaderNode}PlaneNormal), {ShaderNode}Offset);`
    // {c} = context id (root ""); {ShaderNode}X -> P.<prefix>X (fork (1)/(2)). Emitted BEFORE the child
    // recursion so the child samples the reflected point -> wrapped shape gains mirror symmetry.
    const std::string ctx = c.ctx();
    if (injectBug == 1) return;  // drop the pre line -> no reflection -> reflected-probe RED.
    // injectBug==2: read a zeroed normal token (corrupts the REAL emit's normal arg). normalize(0)=0 in
    // MSL -> t=offset, condition uses a degenerate normal -> reflection never moves p -> reflected RED.
    const std::string normalTok =
        (injectBug == 2) ? "float3(0.0)" : ("P." + prefix + "PlaneNormal");
    const std::string swiz = "p" + ctx + ".xyz";
    c.appendCall(swiz + " = pReflect(" + swiz + ", normalize(" + normalTok + "), P." + prefix +
                 "Offset);");
  }

  // Modifier: no post code (TiXL ReflectField has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // Two [GraphParam]s (ReflectField.cs:50-58): PlaneNormal (Vector3, packed_float3 via the frozen
    // helper which owns the 16B-align padding) then Offset (scalar). Emission order = declaration order.
    appendVec3Param(floatParams, paramFields, prefix + "PlaneNormal", nx, ny, nz);
    appendScalarParam(floatParams, paramFields, prefix + "Offset", offset);
  }
};

NodeSpec reflectFieldSpec() {
  NodeSpec s;
  s.type = "ReflectField";
  s.title = "Reflect Field";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // PlaneNormal = Vec3 head run (.x/.y/.z), [GraphParam], default (0,0,0). Same Widget::Vec/vecArity
  // shape BoxSDF's Center / Translate's Translation use (a 3-float vec drawn as one widget).
  PortSpec nx; nx.id = "PlaneNormal.x"; nx.name = "Plane Normal"; nx.dataType = "Float"; nx.isInput = true;
  nx.def = 0.0f; nx.minV = -1.0f; nx.maxV = 1.0f; nx.widget = Widget::Vec; nx.vecArity = 3;
  PortSpec ny; ny.id = "PlaneNormal.y"; ny.name = "Plane Normal.y"; ny.dataType = "Float"; ny.isInput = true;
  ny.def = 0.0f; ny.minV = -1.0f; ny.maxV = 1.0f;
  PortSpec nz; nz.id = "PlaneNormal.z"; nz.name = "Plane Normal.z"; nz.dataType = "Float"; nz.isInput = true;
  nz.def = 0.0f; nz.minV = -1.0f; nz.maxV = 1.0f;
  // Offset = scalar [GraphParam], default 0.
  PortSpec off; off.id = "Offset"; off.name = "Offset"; off.dataType = "Float"; off.isInput = true;
  off.def = 0.0f; off.minV = -10.0f; off.maxV = 10.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, nx, ny, nz, off, out};
  return s;
}

std::shared_ptr<FieldNode> makeReflectField(const std::string& shortId) {
  return std::make_shared<ReflectFieldNode>(shortId);
}

const FieldOp g_reflectFieldOp(reflectFieldSpec(), makeReflectField);

}  // namespace

// Param-cook + test seam (mirrors configureTranslate): set the PlaneNormal vector + Offset (and a
// test-only injectBug: 0 none / 1 drop-pre-line / 2 zeroed-normal) on a makeFieldNode("ReflectField",..)
// node. The leaf type is TU-private; this downcasts inside the owning TU. Production passes injectBug=0.
void configureReflectField(FieldNode& node, float nx, float ny, float nz, float offset, int injectBug) {
  if (auto* n = dynamic_cast<ReflectFieldNode*>(&node)) {
    n->nx = nx;
    n->ny = ny;
    n->nz = nz;
    n->offset = offset;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
