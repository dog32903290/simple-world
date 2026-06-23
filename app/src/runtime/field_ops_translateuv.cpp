// TranslateUV — single-input field MODIFIER (POST-wrap): shifts the wrapped field's CARRIED
// LOCAL-SPACE / COLOR coordinate (f.xyz), NOT its distance (f.w). It drives the POST half of the
// field_graph single-input wrap branch (field_graph.cpp:82-86): it emits ONLY postShaderCode (executed
// AFTER recursing the child), like Invert/Absolute, and has no pre code. Where Translate shifts the
// SAMPLE point p (moving the shape), TranslateUV shifts the f.xyz the child already wrote — used to
// re-author the UV/local-space a downstream color/texture step reads.
//
// TiXL authority: external/tixl/Operators/Lib/field/adjust/TranslateUV.cs
//   GetPostShaderCode(c, inputIndex) (TranslateUV.cs:26-30):
//     c.AppendCall($"f{c}.xyz -= p.w < 0.5 ? {ShaderNode}Translation : 0;"); // save local space
//   [GraphParam] InputSlot<Vector3> Translation (TranslateUV.cs:35-37); one InputField; one
//   Slot<ShaderGraphNode> Result. NO AddDefinitions (no helper globals), NO pre code, NO enum selector.
//   The `p.w` test is the ROOT p.w (TiXL writes the literal `p`, NOT `p{c}`) — the field-eval mode flag
//   (0 in this template's GetField seed) so the branch is "only translate during field-eval, leave local
//   space untouched in the >=0.5 modes".
//
// Branch: SINGLE-INPUT POST-wrap (post AFTER child recurse; no pre). Same shape as Invert/Absolute.
//
// Forks vs TranslateUV.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literal `{ShaderNode}Translation`, where {ShaderNode}
//       interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen convention
//       (backward-traced from field_ops_combinesdf.cpp:288 / translate.cpp:46 — prefix = "<Type>_"+
//       shortId+"_", accessed P.<prefix><Name>) reproduces EXACTLY that name -> the emitted token is
//       P.TranslateUV_<id>_Translation. NOT a forward-assumed literal; a wrong prefix reads the wrong/0
//       struct member and the golden's f.xyz probe bites.
//   (2) HLSL->MSL TERNARY-LITERAL fork (load-bearing) — TiXL's HLSL false-branch literal `0` is a SCALAR;
//       HLSL implicitly broadcasts a scalar to float3 in `f.xyz -= cond ? Translation : 0`. MSL's `?:`
//       requires BOTH branches to be the SAME type (a scalar 0 and a packed_float3 do not unify), so the
//       bare `0` is ported to `float3(0.0)`. This is the ONLY change to the emitted line; the subtract,
//       the `p.w < 0.5` test, and the Translation token are byte-identical. (A `packed_float3` operand in
//       a `?:` against `float3(0.0)` is fine: the result is a float3 and `f.xyz -= float3` is legal.)
//   (3) Vector3 packed via appendVec3Param (packed_float3, 16B-align rule) — the SAME path Translate's
//       Translation / BoxSDF's Center use. Translation is the sole [GraphParam] -> floats[0..2].
//   (4) ROOT p.w fork — TiXL writes `p.w` (the ROOT context p, no `{c}` suffix); this is INTENTIONAL in
//       the .cs (a single context-less p test, "save local space"). Reproduced verbatim as `p.w` (NOT
//       `p{c}.w`). In this template `p.w` is the evalField arg p, seeded p.w=0 by the fragment, so the
//       branch is always TRUE (field-eval mode) — the subtract always applies. Kept literal for parity.
//   Test-only seam: configureTranslateUv sets the REAL Translation AND an injectBug (flip the sign / drop
//   the post line) so the golden's f.xyz tooth bites the op's REAL postShaderCode emit. Production off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- TranslateUV codegen node (a FieldNode subclass; single-input modifier — POST-wrap path) --------

struct TranslateUVNode : FieldNode {
  float tx = 0.f, ty = 0.f, tz = 0.f;  // TranslateUV.t3 default Translation = (0,0,0). Packed [GraphParam].
  // test-only bug modes (configureTranslateUv): 0 = none, 1 = wrong sign (+=), 2 = drop the post line.
  int injectBug = 0;

  explicit TranslateUVNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "TranslateUV_" + shortId + "_";
  }

  // Modifier: no pre code (TiXL TranslateUV has no GetPreShaderCode). FieldNode::preShaderCode is pure
  // virtual, so override it as an empty no-op (the post-only modifier pattern, like Invert/Absolute).
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY TranslateUV.cs:26-30 GetPostShaderCode:
    //   `f{c}.xyz -= p.w < 0.5 ? {ShaderNode}Translation : 0;`. {c} = context id (root ""); `p.w` is the
    //   ROOT p (fork (4)); the false-branch `0` -> `float3(0.0)` (fork (2)); {ShaderNode}Translation ->
    //   P.<prefix>Translation (fork (1)). Emitted AFTER the child recursion so it shifts the child's
    //   carried f.xyz (local space / color), NOT the distance f.w.
    const std::string ctx = c.ctx();
    if (injectBug == 2) return;  // drop the post line -> no f.xyz shift -> shifted probe RED.
    const char* op = (injectBug == 1) ? " += " : " -= ";  // wrong sign -> f.xyz shifts the wrong way.
    c.appendCall("f" + ctx + ".xyz" + op + "p.w < 0.5 ? P." + prefix + "Translation : float3(0.0);");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY Translation is a [GraphParam] (TranslateUV.cs:35-37). packed_float3 via the frozen helper
    // (appendVec3Param owns the 16B-align padding). Sole param -> floats[0..2].
    appendVec3Param(floatParams, paramFields, prefix + "Translation", tx, ty, tz);
  }
};

NodeSpec translateUvSpec() {
  NodeSpec s;
  s.type = "TranslateUV";
  s.title = "Translate UV";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Translation = Vec3 head run (.x/.y/.z), [GraphParam], default (0,0,0). Same Widget::Vec/vecArity
  // shape Translate's Translation uses (a 3-float vec drawn as one widget).
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

std::shared_ptr<FieldNode> makeTranslateUv(const std::string& shortId) {
  return std::make_shared<TranslateUVNode>(shortId);
}

// PF-0c param-apply: project a RESOLVED param map onto a TranslateUVNode via setter-lambdas (NOT offsetof).
// Slot ids EQUAL the NodeSpec PortSpec.id (Translation.x/.y/.z). injectBug is NOT a param (test-only, set
// via the positional configureTranslateUv seam); production stays 0. A missing key keeps the member's ctor
// .t3 default. Routed via the fieldConfigurers() table.
void configureTranslateUvFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<TranslateUVNode*>(&node)) {
    applyFloatSlot(m, "Translation.x", [&](float v) { n->tx = v; });
    applyFloatSlot(m, "Translation.y", [&](float v) { n->ty = v; });
    applyFloatSlot(m, "Translation.z", [&](float v) { n->tz = v; });
  }
}

// slot ids = the SAME ids configureTranslateUvFromParams applies (Option B guard reads them, can't drift).
const FieldOp g_translateUvOp(translateUvSpec(), makeTranslateUv, configureTranslateUvFromParams,
                              {"Translation.x", "Translation.y", "Translation.z"});

}  // namespace

// Param-cook + test seam (mirrors configureTranslate): set the Translation vector (and a test-only
// injectBug: 0 none / 1 wrong-sign / 2 drop-post-line) on a makeFieldNode("TranslateUV",...) node. The
// leaf type is TU-private; this downcasts inside the owning TU. Production passes injectBug=0.
void configureTranslateUv(FieldNode& node, float tx, float ty, float tz, int injectBug) {
  if (auto* n = dynamic_cast<TranslateUVNode*>(&node)) {
    n->tx = tx;
    n->ty = ty;
    n->tz = tz;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
