// TwistField — single-input field MODIFIER (PRE+POST wrap): twists the sampling point about an axis
// before the wrapped field is evaluated (pre), then scales the returned distance by StepFactor (post).
// Near-clone of BendField (same PRE+POST wrap path, field_graph.cpp:82-86: preShaderCode runs BEFORE the
// child recursion so the child samples the twisted point; postShaderCode runs AFTER so it scales the
// child's distance). The ONLY structural differences from BendField are inside the helper body and the
// param default (StepFactor .t3 default 0.9, not 1.0) — both byte-traced below.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/TwistField.cs
//   AddDefinitions (TwistField.cs:35-46): c.Globals["opTwist"] = opTwist(inout float3 p, float k){...}.
//       The twist rotates p.xy by an angle k*p.z (angle PROPORTIONAL TO p.z — the swizzled "twist axis").
//       NOTE this differs from BendField's opBend (which rotated p.xy by an angle k*p.x); the operative
//       difference between the two ops is exactly this axis-of-progression. Body byte-verbatim below.
//   GetPreShaderCode  (TwistField.cs:48-52): c.AppendCall($"opTwist(p{c}.{axi}, {ShaderNode}Amount);");
//       axi = _axisCodes[(int)_axis], the swizzle permutation chosen by the Axis enum (yzx/xzy/xyz).
//   GetPostShaderCode (TwistField.cs:54-57): c.AppendCall($"f{c}.w *= {ShaderNode}StepFactor; ");
//   [GraphParam] float Amount; [GraphParam] float StepFactor; InputSlot<int> Axis (MappedType enum, NOT
//   a [GraphParam] — a compile-time code selector like BendField Axis / CombineSDF CombineMethod).
//
// Forks vs TwistField.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes literals `{ShaderNode}Amount` / `{ShaderNode}StepFactor`, where
//       {ShaderNode} interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen
//       convention (backward-traced from field_ops_combinesdf.cpp:288 — prefix = "<Type>_"+shortId+"_",
//       accessed P.<prefix><Name>) reproduces that name EXACTLY. Emitted tokens:
//       P.TwistField_<id>_Amount / P.TwistField_<id>_StepFactor. NOT a forward-assumed literal; a wrong
//       prefix reads the wrong/0 struct member and the golden's StepFactor probe bites.
//   (2) HLSL->MSL helper port (opTwist body) — Cut-94 SWIZZLE FIX (MANDATORY): the .cs declares
//       `inout float3 p` and the op CALLS it with a SWIZZLE arg `p{c}.{perm}` (perm = yzx/xzy/xyz). MSL
//       rejects binding a swizzle to a non-const `thread float3&`. So the helper is written BY-VALUE +
//       RETURN and the call is a swizzle ASSIGNMENT — this reproduces HLSL inout copy-in/copy-out
//       byte-identically. `mul(m, p.xy)` -> `m * p.xy` (frozen fork). opTwist calls only cos/sin/float2x2
//       built-ins (no inter-helper call) -> it is the SOLE globals key -> NO MSL forward-declaration
//       needed (confirmed: the globals std::map holds exactly one key "opTwist").
//       The float2x2 constructor + the k /= (180/3.14157892) magic constant are byte-verbatim from the
//       .cs (TiXL's deg->rad with their literal pi 3.14157892 — kept exact, do NOT "fix" to 3.14159).
//   (3) Axis enum -> compile-time swizzle selector member (`_axis`), NOT packed (combinesdf.cpp precedent).
//       The _axisCodes table { "yzx","xzy","xyz" } is byte-verbatim from TwistField.cs:61-66; the emitted
//       swizzle is `p{c}.<perm>` (a 3-component swizzle the call ASSIGNS the helper's return into).
//   Test-only seam: configureTwistField sets the REAL Amount / StepFactor / Axis AND an injectBug that
//   corrupts the OP'S REAL post emit (drops the `*= StepFactor`), so the golden's StepFactor probe bites
//   the op's real postShaderCode, not an expected-value tautology. Production default injectBug=0.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// opTwist helper — byte-verbatim from TwistField.cs:38-44 with the Cut-94 swizzle fix + frozen forks:
//   HLSL `inout float3 p`  -> MSL BY-VALUE float3 p + `return p;`  (called with a SWIZZLE arg -> MSL
//       cannot bind a swizzle to `thread float3&`; by-value+return reproduces inout copy-in/copy-out).
//   HLSL `mul(m,p.xy)`     -> MSL `m * p.xy`                       (mul(matrix,vec) -> matrix*vec).
// No inter-helper call (only cos/sin/float2x2 built-ins) -> this is the SOLE globals key -> no forward
// prototype needed. The deg->rad constant 180/3.14157892 is the .cs's literal (their idiosyncratic pi
// spelling) — kept exact for byte parity, do NOT "fix" to 3.14159. The angle progresses along p.z (the
// swizzled twist axis), rotating p.xy — this is what makes it a TWIST, not a bend.
static const char* kBodyOpTwist =
    "float3 opTwist(float3 p, float k) {\n"
    "  k/= (180 / 3.14157892);\n"
    "    float c = cos(k * p.z);\n"
    "    float s = sin(k * p.z);\n"
    "    float2x2  m = float2x2(c,-s,s,c);\n"
    "    p = float3(m * p.xy, p.z);\n"
    "    return p;\n"
    "}";

// _axisCodes — byte-verbatim from TwistField.cs:61-66. Indexed by the Axis enum (X=0,Y=1,Z=2). The chosen
// permutation is swizzled into opTwist so the twist progresses about that axis. NOT packed (enum is a
// compile-time code selector, combinesdf.cpp precedent).
static const char* kAxisCodes[] = {"yzx", "xzy", "xyz"};
constexpr int kAxisCount = static_cast<int>(sizeof(kAxisCodes) / sizeof(kAxisCodes[0]));

// ---- TwistField codegen node (a FieldNode subclass; single-input modifier — PRE+POST wrap path) -------

struct TwistFieldNode : FieldNode {
  float amount = 0.f;        // TwistField.t3 default Amount = 0.0 -> twist is identity. Packed [GraphParam].
  float stepFactor = 0.9f;   // TwistField.t3 default StepFactor = 0.9 (NOT 1.0). Packed [GraphParam].
  int axis = 0;              // TwistField.t3 default Axis = 0 (X). Compile-time swizzle selector, NOT packed.
  // test-only bug mode (configureTwistField): 0 = none, 1 = drop the post `*= StepFactor` line.
  int injectBug = 0;

  explicit TwistFieldNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "TwistField_" + shortId + "_";
  }

  int axisIdx() const { return (axis >= 0 && axis < kAxisCount) ? axis : 0; }

  void addGlobals(CodeAssembleCtx& c) const override {
    // TwistField.cs:35-46 — register opTwist unconditionally (always-on; key matches TiXL "opTwist").
    c.globals["opTwist"] = kBodyOpTwist;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY TwistField.cs:48-52 GetPreShaderCode: `opTwist(p{c}.{axi}, {ShaderNode}Amount);`. {c} = ctx
    // id (root ""); {axi} = swizzle perm from the Axis enum; {ShaderNode}Amount -> P.<prefix>Amount
    // (fork (1)). Cut-94: the .cs's inout-on-swizzle becomes a swizzle ASSIGNMENT of the by-value return.
    // Emitted BEFORE the child recursion so the child samples the twisted point.
    const std::string ctx = c.ctx();
    const std::string swiz = "p" + ctx + "." + kAxisCodes[axisIdx()];
    c.appendCall(swiz + " = opTwist(" + swiz + ", P." + prefix + "Amount);");
  }

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY TwistField.cs:54-57 GetPostShaderCode: `f{c}.w *= {ShaderNode}StepFactor; ` (note the .cs's
    // trailing space — kept). Emitted AFTER the child recursion so it scales the child's distance.
    const std::string ctx = c.ctx();
    if (injectBug == 1) return;  // drop the post line -> no scale -> StepFactor!=1 probe RED.
    c.appendCall("f" + ctx + ".w *= P." + prefix + "StepFactor; ");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order: Amount (TwistField.cs:78-80) then StepFactor
    // (TwistField.cs:85-87). Axis is NOT a [GraphParam] (compile-time selector, never packed). Two scalars
    // -> floats[0], floats[1].
    appendScalarParam(floatParams, paramFields, prefix + "Amount", amount);
    appendScalarParam(floatParams, paramFields, prefix + "StepFactor", stepFactor);
  }
};

NodeSpec twistFieldSpec() {
  NodeSpec s;
  s.type = "TwistField";
  s.title = "Twist Field";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Amount = [GraphParam] float, .t3 default 0.0 (identity twist). TwistField.cs:78-80 / .t3.
  PortSpec am; am.id = "Amount"; am.name = "Amount"; am.dataType = "Float"; am.isInput = true;
  am.def = 0.0f; am.minV = -360.0f; am.maxV = 360.0f;
  // Axis = enum CODE SELECTOR (Widget::Enum dropdown) — a Float port storing the enum index, .t3 default
  // 0 (X). NOT a [GraphParam] (never packed); the node's `axis` int member carries it at codegen time.
  // Labels mirror TwistField.cs:68-73 (X,Y,Z) by index.
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 0.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // StepFactor = [GraphParam] float, .t3 default 0.9 (NOT 1.0). TwistField.cs:85-87 / .t3.
  PortSpec sf; sf.id = "StepFactor"; sf.name = "Step Factor"; sf.dataType = "Float"; sf.isInput = true;
  sf.def = 0.9f; sf.minV = 0.0f; sf.maxV = 2.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, am, ax, sf, out};
  return s;
}

std::shared_ptr<FieldNode> makeTwistField(const std::string& shortId) {
  return std::make_shared<TwistFieldNode>(shortId);
}

const FieldOp g_twistFieldOp(twistFieldSpec(), makeTwistField);

}  // namespace

// Param-cook + test seam (mirrors configureBendField / configureTranslate): set the REAL Amount /
// StepFactor / Axis on a makeFieldNode("TwistField",...) node, plus a test-only injectBug (0 none /
// 1 drop-post-StepFactor) that corrupts the OP's REAL postShaderCode emit. The leaf type is TU-private;
// this downcasts inside the owning TU. Production passes injectBug=0. No-op if `node` is not a
// TwistFieldNode (defensive; the caller passes this op's factory output).
void configureTwistField(FieldNode& node, float amount, float stepFactor, int axis, int injectBug) {
  if (auto* n = dynamic_cast<TwistFieldNode*>(&node)) {
    n->amount = amount;
    n->stepFactor = stepFactor;
    n->axis = axis;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
