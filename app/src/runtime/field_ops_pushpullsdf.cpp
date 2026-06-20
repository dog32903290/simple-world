// PushPullSDF — field ADJUST op that PUSHES / PULLS the wrapped SDF's distance: it adds an Amount to the
// distance f.w (optionally modulated per-point by an AmountField's red channel) and divides by
// (1 + StepScale). This op uses TiXL's CUSTOM-CODE collect (IGraphNodeOp.TryBuildCustomCode), NOT the
// standard pre/post path: the FIRST input (SdfField) is collected in the PARENT context (it stays the
// accumulator), and the OPTIONAL second input (AmountField) is collected in its OWN "amount" subcontext.
// The standard multi-input subcontext loop (which pushes a subcontext for EVERY input including the first)
// cannot express this asymmetry — hence the tryBuildCustomCode override (the field_graph base seam added
// for exactly this family, a 1:1 port of TiXL's mechanism).
//
// TiXL authority: external/tixl/Operators/Lib/field/adjust/PushPullSDF.cs (TryBuildCustomCode, :28-61):
//     inputField?.CollectEmbeddedShaderCode(c);            // SdfField -> PARENT context (no push)
//     c.AppendCall("{"); c.Indent();
//     if (amountField != null) {
//         c.PushContext(c.ContextIdStack.Count, "amount");  // subContextId = "<depth>amount"
//         amountField.CollectEmbeddedShaderCode(c);
//         c.PopContext();
//         c.AppendCall($"f{c}.w += f{subContextId}.r *{ShaderNode}Amount;");
//         c.AppendCall($"f{c}.w /= 1 + {ShaderNode}StepScale;");
//     } else {
//         c.AppendCall($"f{c}.w += {ShaderNode}Amount;");
//     }
//     c.Unindent(); c.AppendCall("}");
//   [GraphParam] InputSlot<float> Amount (.t3 default 0.0); [GraphParam] InputSlot<float> StepScale
//   (.t3 default 1.0). InputSlot<ShaderGraphNode> SdfField (inputs[0]); AmountField (inputs[1], optional).
//
// Branch: CUSTOM-CODE (tryBuildCustomCode). 1-or-2 inputs; the SdfField is the accumulator, AmountField
//   (when present) modulates the push amount.
//
// Forks vs PushPullSDF.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — {ShaderNode}Amount / {ShaderNode}StepScale -> P.<prefix>Amount /
//       P.<prefix>StepScale (sw frozen convention, combinesdf.cpp:288). A wrong prefix reads the wrong/0
//       member and the golden's push probe bites.
//   (2) CUSTOM-COLLECT via tryBuildCustomCode — the SdfField child is recursed through the PUBLIC
//       collectFieldCode (field_graph.h) in the PARENT context (stack unchanged), exactly like TiXL's
//       `inputField.CollectEmbeddedShaderCode(c)`. The AmountField child gets its own pushContext with
//       the literal suffix "amount" at depth = contextIdStack.size() (matching TiXL's
//       `c.ContextIdStack.Count`), so the subContextId is "<depth>amount" — e.g. "1amount" at the root.
//   (3) `f{sub}.r` — TiXL reads the amount field's RED channel via `.r`. MSL has no `.r` swizzle on a
//       float4 by default... actually MSL DOES support `.r/.g/.b/.a` on vectors (rgba accessors are
//       legal alongside xyzw). Kept verbatim as `.r` (== `.x`). No fork needed; pinned by the golden.
//   (4) `1 + {ShaderNode}StepScale` — the literal `1` is an int; in MSL `1 + float` promotes fine
//       (scalar int + float -> float). Kept verbatim. The whole RHS `f{c}.w / (1 + StepScale)` is a
//       scalar divide (no overload ambiguity). NodeSpec exposes only the two scalar [GraphParam]s; the
//       optional AmountField input is wired by the graph/golden, not a knob.
//   Test-only seam: configurePushPullSdf sets the REAL Amount/StepScale AND an injectBug (drop the push
//       line / drop the divide) so the golden's tooth bites the op's REAL emit. Production default off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, collectFieldCode, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- PushPullSDF codegen node (a FieldNode subclass; custom-collect adjust op) --------------------

struct PushPullSDFNode : FieldNode {
  float amount = 0.f;     // PushPullSDF.t3 default Amount = 0.0. Packed [GraphParam].
  float stepScale = 1.f;  // PushPullSDF.t3 default StepScale = 1.0. Packed [GraphParam].
  // test-only bug modes (configurePushPullSdf): 0 = none, 1 = drop the push (f.w += ...), 2 = drop the
  // divide (f.w /= 1+StepScale). Both corrupt the OP's REAL emit, not the golden's expected value.
  int injectBug = 0;

  explicit PushPullSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "PushPullSDF_" + shortId + "_";
  }

  bool tryBuildCustomCode(CodeAssembleCtx& c) const override {
    // PARITY PushPullSDF.cs:28-61. inputs[0]=SdfField (accumulator, parent context). inputs[1]=AmountField
    // (optional). No inputs -> behave like TiXL's early `Count == 0` return (true, emit nothing).
    if (inputs.empty()) return true;

    const FieldNode* inputField = inputs.size() > 0 ? inputs[0].get() : nullptr;
    const FieldNode* amountField = inputs.size() > 1 ? inputs[1].get() : nullptr;

    // SdfField -> PARENT context (fork (2)): recurse it with the stack UNCHANGED so it writes into f{c}.
    if (inputField) collectFieldCode(*inputField, c);

    c.appendCall("{");
    c.indentCount++;

    const std::string parent = c.ctx();
    if (amountField) {
      // AmountField -> its own "amount" subcontext at depth = stack size (TiXL c.ContextIdStack.Count).
      const int subIndex = static_cast<int>(c.contextIdStack.size());
      c.pushContext(subIndex, "amount");
      const std::string sub = c.ctx();
      collectFieldCode(*amountField, c);
      c.popContext();

      if (injectBug != 1) {
        c.appendCall("f" + parent + ".w += f" + sub + ".r * P." + prefix + "Amount;");
      }
      if (injectBug != 2) {
        c.appendCall("f" + parent + ".w /= 1 + P." + prefix + "StepScale;");
      }
    } else {
      if (injectBug != 1) {
        c.appendCall("f" + parent + ".w += P." + prefix + "Amount;");
      }
    }

    c.indentCount--;
    c.appendCall("}");
    return true;
  }

  // tryBuildCustomCode owns the whole emit; the standard pre/post path is never reached. preShaderCode is
  // pure-virtual in the base, so provide an empty override (never called once tryBuildCustomCode returns
  // true).
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order: Amount (PushPullSDF.cs:70-72) then StepScale
    // (PushPullSDF.cs:74-76). Two scalars. (collectAllParams walks inputs depth-first FIRST, so the
    // child fields' params precede these — same as every combiner.)
    appendScalarParam(floatParams, paramFields, prefix + "Amount", amount);
    appendScalarParam(floatParams, paramFields, prefix + "StepScale", stepScale);
  }
};

NodeSpec pushPullSdfSpec() {
  NodeSpec s;
  s.type = "PushPullSDF";
  s.title = "Push Pull SDF";
  // SdfField input (the accumulator); AmountField input (optional per-point modulation). dataType "Field".
  PortSpec sf; sf.id = "SdfField"; sf.name = "Sdf Field"; sf.dataType = "Field"; sf.isInput = true;
  PortSpec af; af.id = "AmountField"; af.name = "Amount Field"; af.dataType = "Field"; af.isInput = true;
  // Amount = push distance [GraphParam] float, .t3 default 0.0.
  PortSpec am; am.id = "Amount"; am.name = "Amount"; am.dataType = "Float"; am.isInput = true;
  am.def = 0.0f; am.minV = -10.0f; am.maxV = 10.0f;
  // StepScale = divide factor [GraphParam] float, .t3 default 1.0 (so default divide is by 2).
  PortSpec ss; ss.id = "StepScale"; ss.name = "Step Scale"; ss.dataType = "Float"; ss.isInput = true;
  ss.def = 1.0f; ss.minV = 0.0f; ss.maxV = 10.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {sf, af, am, ss, out};
  return s;
}

std::shared_ptr<FieldNode> makePushPullSdf(const std::string& shortId) {
  return std::make_shared<PushPullSDFNode>(shortId);
}

const FieldOp g_pushPullSdfOp(pushPullSdfSpec(), makePushPullSdf);

}  // namespace

// Param-cook + test seam: set Amount/StepScale (and a test-only injectBug: 0 none / 1 drop-push /
// 2 drop-divide) on a makeFieldNode("PushPullSDF",...) node. The leaf type is TU-private; this downcasts
// inside the owning TU. Production passes injectBug=0.
void configurePushPullSdf(FieldNode& node, float amount, float stepScale, int injectBug) {
  if (auto* n = dynamic_cast<PushPullSDFNode*>(&node)) {
    n->amount = amount;
    n->stepScale = stepScale;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
