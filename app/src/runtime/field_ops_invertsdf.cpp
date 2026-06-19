// InvertSDF — single-input field MODIFIER (post-wrap): flips the sign of the wrapped field's distance,
// turning solid into hollow (inside<->outside). FIRST op to drive the single-input pre/post wrap branch
// of the field_graph recursion (field_graph.cpp:82-86 else-branch): a node with exactly ONE input field
// emits preShaderCode, recurses the child, then emits postShaderCode. InvertSDF emits ONLY postShaderCode.
//
// TiXL authority: external/tixl/Operators/Lib/field/adjust/InvertSDF.cs
//   GetPostShaderCode(c, inputIndex): c.AppendCall($"f{c}.w *=-1;");
//   one InputSlot<ShaderGraphNode> InputField; one Slot<ShaderGraphNode> Result. No [GraphParam].
//
// Forks vs InvertSDF.cs (named):
//   (none structural) — emit string is byte-verbatim `f{c}.w *=-1;`. No globals, no params, no pre code.
//   Test-only seam: an `injectBug` flag on the node (set by configureInvertSdf) drops the *-1 so the
//   golden's tooth bites the op's REAL postShaderCode emit path (not the template) — production default
//   is false, so the shipped emit is byte-identical to TiXL.
#include "runtime/graph.h"  // NodeSpec, PortSpec

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- InvertSDF codegen node (a FieldNode subclass; single-input modifier — post-wrap path) --------

struct InvertSDFNode : FieldNode {
  bool injectBug = false;  // test-only: drop the *-1 in the REAL emit path (configureInvertSdf seam).

  explicit InvertSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix (none used here; kept
    // for parity with the convention so a future param would prefix consistently).
    prefix = "InvertSDF_" + shortId + "_";
  }

  // No globals, no params — a pure post-wrap distance sign flip.

  // Modifier: no pre code (the base seeds nothing extra for single-input; the child writes f{c}.w).
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY InvertSDF.cs:26-29 GetPostShaderCode — byte-verbatim `f{c}.w *=-1;`. {c} = context id
    // (root "" here; the single-input wrap does NOT push a sub-context). Emitted AFTER the child's
    // distance write, so it negates the wrapped field's distance.
    const std::string ctx = c.ctx();
    if (injectBug) {
      // Tooth: drop the negation -> field unchanged -> golden's -(d) probe goes RED. Lives in the op's
      // real postShaderCode emit (not the template) so the bite proves THIS emit string.
      c.appendCall("f" + ctx + ".w *=1;");
      return;
    }
    c.appendCall("f" + ctx + ".w *=-1;");
  }

  // No [GraphParam] — InvertSDF.cs has no packed params.
  void collectParams(std::vector<float>&, std::vector<std::string>&) const override {}
};

NodeSpec invertSdfSpec() {
  NodeSpec s;
  s.type = "InvertSDF";
  s.title = "Invert SDF";
  // One Field input (TiXL InputField). dataType "Field" blocks wiring into Float/Points/Texture2D.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, out};
  return s;
}

std::shared_ptr<FieldNode> makeInvertSdf(const std::string& shortId) {
  return std::make_shared<InvertSDFNode>(shortId);
}

const FieldOp g_invertSdfOp(invertSdfSpec(), makeInvertSdf);

}  // namespace

// Test seam: flip the injectBug flag on a node built via makeFieldNode("InvertSDF", ...). The leaf type
// is TU-private; this free function downcasts inside the owning TU (mirrors configureCombineSdf). No-op
// if `node` is not an InvertSDFNode. Production code never calls it (default injectBug=false).
void configureInvertSdf(FieldNode& node, bool injectBug) {
  if (auto* n = dynamic_cast<InvertSDFNode*>(&node)) n->injectBug = injectBug;
}
}  // namespace sw
