// SdfToVector — field op that converts a wrapped SDF into a VECTOR field: f.xyz = the normalized
// gradient (surface-normal direction) of the child's signed distance, computed by a 4-sample tetrahedral
// finite difference; f.w = the child's raw distance at the center point. Single input (custom-collect).
//
// TiXL authority: external/tixl/Operators/Lib/field/use/SdfToVector.cs (IGraphNodeOp.TryBuildCustomCode,
//   :40-73). TiXL wraps the child's collected code in a `float {c}GetSdfDistance(float3 p3)` METHOD (via
//   c.PushMethod/c.PopMethod + c.Definitions.Append, SdfToVector.cs:46-61) so the child code is emitted
//   ONCE and CALLED at the 4 tetrahedral offsets + the center (SdfToVector.cs:65-72):
//     f.xyz = normalize( GetSdfDistance(p + ( h,-h,-h))*( 1,-1,-1)
//                      + GetSdfDistance(p + (-h, h,-h))*(-1, 1,-1)
//                      + GetSdfDistance(p + (-h,-h, h))*(-1,-1, 1)
//                      + GetSdfDistance(p + ( h, h, h))*( 1, 1, 1) );
//     f.w   = GetSdfDistance(p);
//   [GraphParam] InputSlot<float> LookUpDistance (the finite-difference step h; SdfToVector.cs:79-81).
//   InputSlot<ShaderGraphNode> InputField (inputs[0]; SdfToVector.cs:76-77).
//
// Branch: CUSTOM-CODE (tryBuildCustomCode). Single input (the SDF to differentiate).
//
// Forks vs SdfToVector.cs (named, load-bearing):
//   (1) NO WRAPPER METHOD (structural fork, honest & bounded) — TiXL's PushMethod/PopMethod captures the
//       child code into a file-scope `GetSdfDistance(float3)` so the 5 samples reuse one copy. sw's MSL
//       CANNOT reproduce that here: the child code references `P.<child>Radius` etc., and `P` is a
//       fragment-function ARGUMENT (constant FieldParams&), not an HLSL cbuffer GLOBAL — a file-scope
//       method (which would live in the /*{GLOBALS}*/ block, ABOVE the FieldParams struct declaration)
//       cannot name FieldParams nor receive P without a base-machinery + template change. Instead we
//       emit the child INLINE inside the calls stream, exactly the pattern RepeatFieldAtPoints uses
//       (field_ops_repeatfieldatpoints.cpp: save pStart, mutate p, `collectFieldCode` the child inline,
//       read f.w). The child code is emitted TWICE: once in a 4-iteration gradient loop, once for the
//       center f.w. Functionally IDENTICAL to TiXL's 5 method-calls (same math, same result); the only
//       cost is a larger shader (2 inline copies vs 1 method). NO new base seam, NO template edit.
//   (2) TETRAHEDRAL SIGN == OFFSET/h — TiXL's four (offset, signVector) pairs satisfy signVector ==
//       offset/h (e.g. offset (h,-h,-h) has sign (1,-1,-1)). So the gradient sum is
//       Σ sign[i] * GetSdfDistance(pStart + sign[i]*h), a 4-iteration loop over a const sign array.
//       Byte-equivalent to unrolling the 4 method-calls.
//   (3) PARAM-NAME PREFIX — {ShaderNode}LookUpDistance -> P.<prefix>LookUpDistance (sw frozen convention,
//       combinesdf.cpp:288). A wrong prefix reads the wrong/0 step -> a degenerate gradient -> the
//       golden's normal probe bites.
//   (4) normalize / float-math is identical text HLSL<->MSL (no inout, no swizzle-by-ref) -> no MSL fork
//       beyond the implicit float-math identity.
//   Test-only seam: configureSdfToVector sets the REAL LookUpDistance AND an injectBug that corrupts the
//       OP's REAL emit (drop the gradient normalize -> f.xyz stays the raw seed; or zero the step ->
//       degenerate gradient) so the golden's normal probe bites the op's real body. Production off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, collectFieldCode, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- SdfToVector codegen node (a FieldNode subclass; custom-collect single-input op) ---------------

struct SdfToVectorNode : FieldNode {
  float lookUpDistance = 0.01f;  // SdfToVector.t3 default LookUpDistance = 0.01. Packed [GraphParam].
  // test-only bug modes (configureSdfToVector): 0 = none, 1 = drop the normalize (f.xyz stays the raw
  // gradient sum, un-normalized -> different length), 2 = zero the step (all 4 samples at pStart ->
  // gradient sum is ~0 -> normalize of ~0 -> degenerate). Both corrupt the OP's REAL vector emit.
  int injectBug = 0;

  explicit SdfToVectorNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix.
    prefix = "SdfToVector_" + shortId + "_";
  }

  bool tryBuildCustomCode(CodeAssembleCtx& c) const override {
    // PARITY SdfToVector.cs:40-73. inputs[0] = InputField (the SDF to differentiate). No input -> match
    // TiXL's early `fields.Count == 0 -> return true` (emit nothing; f keeps its seed).
    if (inputs.empty() || !inputs[0]) return true;

    const std::string ctx = c.ctx();
    const std::string h = injectBug == 2 ? std::string("0.0")  // bug 2: zero the step -> degenerate.
                                         : ("P." + prefix + "LookUpDistance");

    // Snapshot the incoming sample point (the 5 evaluations all offset from it).
    c.appendCall("float3 pStart" + ctx + " = p" + ctx + ".xyz;");
    c.appendCall("float3 sdfGrad" + ctx + " = float3(0.0);");

    // Tetrahedral 4-sample gradient. sign[i] == offset/h (fork (2)); the same const array serves as both
    // the offset direction and the weight. Loop body recurses the child INLINE (fork (1)).
    c.appendCall("const float3 sdfvSign" + ctx +
                 "[4] = { float3(1,-1,-1), float3(-1,1,-1), float3(-1,-1,1), float3(1,1,1) };");
    c.appendCall("for (int i" + ctx + " = 0; i" + ctx + " < 4; i" + ctx + "++) {");
    c.indentCount++;
    c.appendCall("p" + ctx + ".xyz = pStart" + ctx + " + sdfvSign" + ctx + "[i" + ctx + "] * " + h + ";");
    collectFieldCode(*inputs[0], c);  // child writes f{c}.w = distance(p{c})
    c.appendCall("sdfGrad" + ctx + " += sdfvSign" + ctx + "[i" + ctx + "] * f" + ctx + ".w;");
    c.indentCount--;
    c.appendCall("}");

    // Center evaluation for f.w (the raw child distance at the un-offset point).
    c.appendCall("p" + ctx + ".xyz = pStart" + ctx + ";");
    collectFieldCode(*inputs[0], c);  // child writes f{c}.w = distance(pStart)

    // f.xyz = normalized gradient (the surface-normal direction). injectBug 1 drops the normalize.
    if (injectBug == 1) {
      c.appendCall("f" + ctx + ".xyz = sdfGrad" + ctx + ";");
    } else {
      c.appendCall("f" + ctx + ".xyz = normalize(sdfGrad" + ctx + ");");
    }
    // f{c}.w already holds the center distance from the last collectFieldCode -> matches
    // SdfToVector.cs:71 `f{c}.w = GetSdfDistance(p{c}.xyz);`. Nothing more to emit.
    return true;
  }

  // tryBuildCustomCode owns the whole emit; preShaderCode is pure-virtual in the base -> empty override
  // (never reached once tryBuildCustomCode returns true).
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // The only [GraphParam] is LookUpDistance (SdfToVector.cs:79-81). (collectAllParams walks inputs
    // depth-first FIRST, so the child field's params precede this — same as every combiner.)
    appendScalarParam(floatParams, paramFields, prefix + "LookUpDistance", lookUpDistance);
  }
};

NodeSpec sdfToVectorSpec() {
  NodeSpec s;
  s.type = "SdfToVector";
  s.title = "Sdf To Vector";
  // InputField (the SDF to differentiate). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // LookUpDistance = finite-difference step h [GraphParam] float, .t3 default 0.001 (SdfToVector.t3 Id
  // 2ea69efd).
  PortSpec ld; ld.id = "LookUpDistance"; ld.name = "Look Up Distance"; ld.dataType = "Float";
  ld.isInput = true; ld.def = 0.001f; ld.minV = 0.0001f; ld.maxV = 1.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, ld, out};
  return s;
}

std::shared_ptr<FieldNode> makeSdfToVector(const std::string& shortId) {
  return std::make_shared<SdfToVectorNode>(shortId);
}

// PF-0c param-apply: project a RESOLVED param map onto an SdfToVectorNode. Slot id EQUALS the NodeSpec
// PortSpec.id (LookUpDistance packed float). injectBug is NOT a param (test-only, via configureSdfToVector).
void configureSdfToVectorFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<SdfToVectorNode*>(&node)) {
    applyFloatSlot(m, "LookUpDistance", [&](float v) { n->lookUpDistance = v; });
  }
}

// slot ids = the SAME ids configureSdfToVectorFromParams applies (Option B guard reads them).
const FieldOp g_sdfToVectorOp(sdfToVectorSpec(), makeSdfToVector, configureSdfToVectorFromParams,
                              {"LookUpDistance"});

}  // namespace

// Param-cook + test seam: set the REAL LookUpDistance on a makeFieldNode("SdfToVector",...) node, plus a
// test-only injectBug (0 none / 1 drop-normalize / 2 zero-step). The leaf type is TU-private; this
// downcasts inside the owning TU. Production passes injectBug=0.
void configureSdfToVector(FieldNode& node, float lookUpDistance, int injectBug) {
  if (auto* n = dynamic_cast<SdfToVectorNode*>(&node)) {
    n->lookUpDistance = lookUpDistance;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
