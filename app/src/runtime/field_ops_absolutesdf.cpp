// AbsoluteSDF — single-input field MODIFIER (post-wrap): takes the absolute value of the wrapped field's
// distance, turning a solid into an infinitely-thin shell at its surface (interior distance folds to
// positive). Drives the single-input pre/post wrap branch (field_graph.cpp:82-86) via postShaderCode.
//
// TiXL authority: external/tixl/Operators/Lib/field/adjust/AbsoluteSDF.cs
//   GetPostShaderCode(c, inputIndex): c.AppendCall($"f{c}.w = abs(f{c}.w);");
//   one InputSlot<ShaderGraphNode> InputField; one Slot<ShaderGraphNode> Result. No [GraphParam].
//
// Forks vs AbsoluteSDF.cs (named):
//   (none structural) — emit string is byte-verbatim `f{c}.w = abs(f{c}.w);`. No globals/params/pre.
//   `abs(float)` is identical text in HLSL and MSL.
//   Test-only seam: an `injectBug` flag (configureAbsoluteSdf) drops the abs() so the golden's tooth
//   bites the op's REAL postShaderCode emit (not the template). Production default false = byte-identical.
#include "runtime/graph.h"  // NodeSpec, PortSpec

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- AbsoluteSDF codegen node (a FieldNode subclass; single-input modifier — post-wrap path) ------

struct AbsoluteSDFNode : FieldNode {
  bool injectBug = false;  // test-only: drop abs() in the REAL emit path (configureAbsoluteSdf seam).

  explicit AbsoluteSDFNode(const std::string& shortId) {
    prefix = "AbsoluteSDF_" + shortId + "_";  // TiXL BuildNodeId convention (no param uses it here).
  }

  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY AbsoluteSDF.cs:25-28 GetPostShaderCode — byte-verbatim `f{c}.w = abs(f{c}.w);`. {c} =
    // context id (root "" — single-input wrap pushes no sub-context). After the child's distance write.
    const std::string ctx = c.ctx();
    if (injectBug) {
      // Tooth: drop the abs() -> interior distance stays negative -> golden's +|d| interior probe RED.
      c.appendCall("f" + ctx + ".w = (f" + ctx + ".w);");
      return;
    }
    c.appendCall("f" + ctx + ".w = abs(f" + ctx + ".w);");
  }

  void collectParams(std::vector<float>&, std::vector<std::string>&) const override {}
};

NodeSpec absoluteSdfSpec() {
  NodeSpec s;
  s.type = "AbsoluteSDF";
  s.title = "Absolute SDF";
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, out};
  return s;
}

std::shared_ptr<FieldNode> makeAbsoluteSdf(const std::string& shortId) {
  return std::make_shared<AbsoluteSDFNode>(shortId);
}

const FieldOp g_absoluteSdfOp(absoluteSdfSpec(), makeAbsoluteSdf);

}  // namespace

// Test seam (mirrors configureCombineSdf): flip injectBug on a makeFieldNode("AbsoluteSDF",...) node.
void configureAbsoluteSdf(FieldNode& node, bool injectBug) {
  if (auto* n = dynamic_cast<AbsoluteSDFNode*>(&node)) n->injectBug = injectBug;
}
}  // namespace sw
