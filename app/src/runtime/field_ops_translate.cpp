// Translate — single-input field MODIFIER (PRE-wrap): shifts the sampling point before the wrapped field
// is evaluated, moving the shape by +Translation. This op drives the OTHER half of the single-input wrap
// branch (field_graph.cpp:82-86): it emits preShaderCode (executed BEFORE recursing the child), where
// Invert/Absolute emit postShaderCode. Together the three bracket both halves of the wrap edge.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/Translate.cs
//   GetPreShaderCode(c, inputIndex): c.AppendCall($"p{c}.xyz -= {ShaderNode}Translation;");
//   [GraphParam] InputSlot<Vector3> Translation;  one InputField; one Slot<ShaderGraphNode> Result.
//   (ITransformable gizmo is editor-only; the codegen is just the pre line + the packed Vector3.)
//
// Forks vs Translate.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literal `{ShaderNode}Translation`, where {ShaderNode}
//       interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen convention
//       (backward-traced from field_ops_combinesdf.cpp:288 / field_ops_boxsdf.cpp:56 — `prefix =
//       "<Type>_" + shortId + "_"`, accessed `P.<prefix><Name>`) reproduces EXACTLY that name. So the
//       emitted token is `P.Translate_<id>_Translation` — functionally TiXL's `{ShaderNode}Translation`
//       with the MSL `P.` cbuffer-struct qualifier. NOT a forward-assumed literal; a wrong prefix would
//       read the wrong struct member (or 0, like the Radius-as-0 alignment fork) and the golden catches it.
//   (2) HLSL->MSL: bare cbuffer name `{n}Translation` -> `P.{prefix}Translation` (params live in the
//       `constant FieldParams& P` arg, identical to every SDF leaf). The `-=` math is identical text.
//   (3) Vector3 packed via appendVec3Param (packed_float3, 16B-align rule) — the SAME path BoxSDF's
//       Center/CombinedScale use. Translation is the only [GraphParam], so it lands at floats[0..2].
//   Test-only seam: configureTranslate sets the REAL Translation AND an injectBug (flip the sign / drop
//   the pre line) so the golden's tooth bites the op's REAL preShaderCode emit. Production default off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- Translate codegen node (a FieldNode subclass; single-input modifier — PRE-wrap path) ---------

struct TranslateNode : FieldNode {
  float tx = 0.f, ty = 0.f, tz = 0.f;  // Translate.t3 default Translation = (0,0,0). Packed [GraphParam].
  // test-only bug modes (configureTranslate): 0 = none, 1 = wrong sign (+=), 2 = drop the pre line.
  int injectBug = 0;

  explicit TranslateNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "Translate_" + shortId + "_";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY Translate.cs:33-36 GetPreShaderCode: `p{c}.xyz -= {ShaderNode}Translation;`. {c} = context
    // id (root ""); {ShaderNode}Translation -> P.<prefix>Translation (fork (1)/(2)). Emitted BEFORE the
    // child recursion so the child samples the shifted point -> shape moves by +Translation.
    const std::string ctx = c.ctx();
    if (injectBug == 2) return;  // drop the pre line -> no shift -> shifted-probe RED.
    const char* op = (injectBug == 1) ? " += " : " -= ";  // wrong sign -> shape moves the wrong way.
    c.appendCall("p" + ctx + ".xyz" + op + "P." + prefix + "Translation;");
  }

  // Modifier: no post code (TiXL Translate has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY Translation is a [GraphParam] (Translate.cs:41-43). packed_float3 via the frozen helper
    // (appendVec3Param owns the 16B-align padding). Sole param -> floats[0..2].
    appendVec3Param(floatParams, paramFields, prefix + "Translation", tx, ty, tz);
  }
};

NodeSpec translateSpec() {
  NodeSpec s;
  s.type = "Translate";
  s.title = "Translate";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Translation = Vec3 head run (.x/.y/.z), [GraphParam], default (0,0,0). Same Widget::Vec/vecArity
  // shape BoxSDF's Center uses (a 3-float vec drawn as one widget).
  PortSpec tx; tx.id = "Translation.x"; tx.name = "Translation"; tx.dataType = "Float"; tx.isInput = true;
  tx.def = 0.0f; tx.minV = -10.0f; tx.maxV = 10.0f; tx.widget = Widget::Vec; tx.vecArity = 3;
  PortSpec ty; ty.id = "Translation.y"; ty.name = "Translation.y"; ty.dataType = "Float"; ty.isInput = true;
  ty.def = 0.0f; ty.minV = -10.0f; ty.maxV = 10.0f;
  PortSpec tz; tz.id = "Translation.z"; tz.name = "Translation.z"; tz.dataType = "Float"; tz.isInput = true;
  tz.def = 0.0f; tz.minV = -10.0f; tz.maxV = 10.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, tx, ty, tz, out};
  return s;
}

std::shared_ptr<FieldNode> makeTranslate(const std::string& shortId) {
  return std::make_shared<TranslateNode>(shortId);
}

// PF-0c param-apply: project a RESOLVED param map onto a TranslateNode via setter-lambdas (NOT offsetof).
// Slot ids EQUAL the NodeSpec PortSpec.id (Translation.x/.y/.z). injectBug is NOT a param (test-only, set
// via the positional configureTranslate seam); production stays 0. A missing key keeps the member's ctor
// .t3 default. Routed via the fieldConfigurers() table.
void configureTranslateFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<TranslateNode*>(&node)) {
    applyFloatSlot(m, "Translation.x", [&](float v) { n->tx = v; });
    applyFloatSlot(m, "Translation.y", [&](float v) { n->ty = v; });
    applyFloatSlot(m, "Translation.z", [&](float v) { n->tz = v; });
  }
}

// slot ids = the SAME ids configureTranslateFromParams applies (Option B guard reads them, can't drift).
const FieldOp g_translateOp(translateSpec(), makeTranslate, configureTranslateFromParams,
                            {"Translation.x", "Translation.y", "Translation.z"});

}  // namespace

// Param-cook + test seam (mirrors configureCombineSdf): set the Translation vector (and a test-only
// injectBug: 0 none / 1 wrong-sign / 2 drop-pre-line) on a makeFieldNode("Translate",...) node. The
// leaf type is TU-private; this downcasts inside the owning TU. Production passes injectBug=0.
void configureTranslate(FieldNode& node, float tx, float ty, float tz, int injectBug) {
  if (auto* n = dynamic_cast<TranslateNode*>(&node)) {
    n->tx = tx;
    n->ty = ty;
    n->tz = tz;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
