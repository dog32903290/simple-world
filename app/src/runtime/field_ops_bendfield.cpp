// BendField — single-input field MODIFIER (PRE+POST wrap): bends the sampling point about an axis
// before the wrapped field is evaluated (pre), then scales the returned distance by StepFactor (post).
// This op exercises BOTH halves of the field_graph single-input wrap branch (field_graph.cpp:82-86):
// preShaderCode runs BEFORE recursing the child (so the child samples the bent point), postShaderCode
// runs AFTER (so it scales the child's distance). Translate is pre-only, Invert/Absolute are post-only;
// BendField is the first single-input op to drive both edges of the same wrap.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/BendField.cs
//   AddDefinitions (BendField.cs:36-47): c.Globals["opBend"] = opBend(inout float3 p, float k){...}
//   GetPreShaderCode  (BendField.cs:48-52): c.AppendCall($"opBend(p{c}.{axi}, {ShaderNode}Amount);");
//       axi = _axisCodes0[(int)_axis], the swizzle permutation chosen by the Axis enum (yzx/xzy/xyz).
//   GetPostShaderCode (BendField.cs:54-57): c.AppendCall($"f{c}.w *= {ShaderNode}StepFactor; ");
//   [GraphParam] float Amount; [GraphParam] float StepFactor; InputSlot<int> Axis (MappedType enum, NOT
//   a [GraphParam] — a compile-time code selector like CombineSDF's CombineMethod / Torus Axis).
//
// Forks vs BendField.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes literals `{ShaderNode}Amount` / `{ShaderNode}StepFactor`, where
//       {ShaderNode} interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen
//       convention (backward-traced from field_ops_combinesdf.cpp:288 / field_ops_translate.cpp:46 —
//       `prefix = "<Type>_" + shortId + "_"`, accessed `P.<prefix><Name>`) reproduces that name EXACTLY.
//       Emitted tokens: P.BendField_<id>_Amount / P.BendField_<id>_StepFactor. NOT a forward-assumed
//       literal; a wrong prefix reads the wrong/0 struct member and the golden's StepFactor probe bites.
//   (2) HLSL->MSL helper port (opBend body): `inout float3 p` -> `thread float3& p` (frozen fork: HLSL
//       inout -> MSL thread X& ONLY inside helpers). `mul(m, p.xy)` -> `m * p.xy` (frozen fork). No lerp /
//       no inter-helper call inside opBend (it calls only built-ins cos/sin/float2x2/float3) -> NO MSL
//       forward-declaration needed (confirmed: the globals std::map holds exactly one key "opBend").
//       The float2x2 constructor + the k /= (180/3.14157892) magic constant are byte-verbatim from the
//       .cs (the constant is TiXL's deg->rad with their literal pi 3.14157892, NOT 3.14159 — kept exact).
//   (3) Axis enum -> compile-time swizzle selector member (`_axis`), NOT packed (combinesdf.cpp:284
//       precedent). The _axisCodes0 table { "yzx","xzy","xyz" } is byte-verbatim from BendField.cs:59-64;
//       the emitted swizzle is `p{c}.<perm>` (a 3-component swizzle passed by-ref into opBend's thread&).
//   Test-only seam: configureBendField sets the REAL Amount / StepFactor / Axis AND an injectBug that
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

// opBend helper — byte-verbatim from BendField.cs:38-46 with the two frozen MSL forks applied:
//   HLSL `inout float3 p`  -> MSL `thread float3& p`   (inout -> thread& inside helpers)
//   HLSL `mul(m,p.xy)`     -> MSL `m * p.xy`           (mul(matrix,vec) -> matrix*vec)
// No inter-helper call (only cos/sin/float2x2 built-ins) -> this is the SOLE globals key -> no forward
// prototype needed (unlike combinesdf.cpp:216 fork-5). The deg->rad constant 180/3.14157892 is the .cs's
// literal (their idiosyncratic pi spelling) — kept exact for byte parity, do NOT "fix" to 3.14159.
static const char* kBodyOpBend =
    "float3 opBend(float3 p, float k) {\n"
    "    k/= (180 / 3.14157892);\n"
    "    float c = cos(k * p.x);\n"
    "    float s = sin(k * p.x);\n"
    "    float2x2  m = float2x2(c, -s, s, c);\n"
    "    p = float3(m * p.xy, p.z);\n"
    "    return p;\n"
    "}";

// _axisCodes0 — byte-verbatim from BendField.cs:59-64. Indexed by the Axis enum (X=0,Y=1,Z=2). The
// chosen permutation is swizzled into opBend so the bend acts about that axis. NOT packed (enum is a
// compile-time code selector, combinesdf.cpp:284 precedent).
static const char* kAxisCodes[] = {"yzx", "xzy", "xyz"};
constexpr int kAxisCount = static_cast<int>(sizeof(kAxisCodes) / sizeof(kAxisCodes[0]));

// ---- BendField codegen node (a FieldNode subclass; single-input modifier — PRE+POST wrap path) ------

struct BendFieldNode : FieldNode {
  float amount = 0.f;      // BendField.t3 default Amount = 0 -> bend is identity. Packed [GraphParam].
  float stepFactor = 1.f;  // BendField.t3 default StepFactor = 1 -> identity scale. Packed [GraphParam].
  int axis = 0;            // BendField.t3 default Axis = 0 (X). Compile-time swizzle selector, NOT packed.
  // test-only bug mode (configureBendField): 0 = none, 1 = drop the post `*= StepFactor` line.
  int injectBug = 0;

  explicit BendFieldNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "BendField_" + shortId + "_";
  }

  int axisIdx() const { return (axis >= 0 && axis < kAxisCount) ? axis : 0; }

  void addGlobals(CodeAssembleCtx& c) const override {
    // BendField.cs:36-47 — register opBend unconditionally (always-on; key matches TiXL "opBend").
    c.globals["opBend"] = kBodyOpBend;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY BendField.cs:48-52 GetPreShaderCode: `opBend(p{c}.{axi}, {ShaderNode}Amount);`. {c} = ctx
    // id (root ""); {axi} = swizzle perm from the Axis enum; {ShaderNode}Amount -> P.<prefix>Amount
    // (forks (1)). Emitted BEFORE the child recursion so the child samples the bent point.
    const std::string ctx = c.ctx();
    const std::string swiz = "p" + ctx + "." + kAxisCodes[axisIdx()];
    c.appendCall(swiz + " = opBend(" + swiz + ", P." + prefix + "Amount);");
  }

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY BendField.cs:54-57 GetPostShaderCode: `f{c}.w *= {ShaderNode}StepFactor; ` (note the .cs's
    // trailing space — kept). Emitted AFTER the child recursion so it scales the child's distance.
    const std::string ctx = c.ctx();
    if (injectBug == 1) return;  // drop the post line -> no scale -> StepFactor!=1 probe RED.
    c.appendCall("f" + ctx + ".w *= P." + prefix + "StepFactor; ");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order: Amount (BendField.cs:78-80) then StepFactor
    // (BendField.cs:85-87). Axis is NOT a [GraphParam] (compile-time selector, never packed). Two scalars
    // -> floats[0], floats[1].
    appendScalarParam(floatParams, paramFields, prefix + "Amount", amount);
    appendScalarParam(floatParams, paramFields, prefix + "StepFactor", stepFactor);
  }
};

NodeSpec bendFieldSpec() {
  NodeSpec s;
  s.type = "BendField";
  s.title = "Bend Field";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Amount = [GraphParam] float, .t3 default 0 (identity bend). BendField.cs:78-80.
  PortSpec am; am.id = "Amount"; am.name = "Amount"; am.dataType = "Float"; am.isInput = true;
  am.def = 0.0f; am.minV = -360.0f; am.maxV = 360.0f;
  // Axis = enum CODE SELECTOR (Widget::Enum dropdown) — a Float port storing the enum index, .t3 default
  // 0 (X). NOT a [GraphParam] (never packed); the node's `axis` int member carries it at codegen time.
  // Labels mirror BendField.cs:68-73 (X,Y,Z) by index.
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 0.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // StepFactor = [GraphParam] float, .t3 default 1 (identity distance scale). BendField.cs:85-87.
  PortSpec sf; sf.id = "StepFactor"; sf.name = "Step Factor"; sf.dataType = "Float"; sf.isInput = true;
  sf.def = 1.0f; sf.minV = 0.0f; sf.maxV = 2.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, am, ax, sf, out};
  return s;
}

std::shared_ptr<FieldNode> makeBendField(const std::string& shortId) {
  return std::make_shared<BendFieldNode>(shortId);
}

// PF-0c param-apply (WAVE 3): project a RESOLVED param map onto a BendFieldNode via setter-lambdas (NOT
// offsetof). Slot ids EQUAL the NodeSpec PortSpec.id: Amount / StepFactor (packed [GraphParam] floats) +
// Axis (the compile-time swizzle selector, applyIntSelSlot (int)(v+0.5f) — switches the emitted swizzle
// text p{c}.<perm>, NOT the float buffer). A missing key leaves the member at its ctor .t3 default.
// injectBug is NOT a param (test-only via configureBendField); production stays 0. Routed via
// fieldConfigurers().
void configureBendFieldFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<BendFieldNode*>(&node)) {
    applyFloatSlot(m, "Amount", [&](float v) { n->amount = v; });
    applyFloatSlot(m, "StepFactor", [&](float v) { n->stepFactor = v; });
    applyIntSelSlot(m, "Axis", [&](int v) { n->axis = v; });
  }
}

// slot ids = the SAME ids configureBendFieldFromParams applies (Option B guard, can't drift).
const FieldOp g_bendFieldOp(bendFieldSpec(), makeBendField, configureBendFieldFromParams,
                            {"Amount", "StepFactor", "Axis"});

}  // namespace

// Param-cook + test seam (mirrors configureTranslate / configureCombineSdf): set the REAL Amount /
// StepFactor / Axis on a makeFieldNode("BendField",...) node, plus a test-only injectBug (0 none /
// 1 drop-post-StepFactor) that corrupts the OP's REAL postShaderCode emit. The leaf type is TU-private;
// this downcasts inside the owning TU. Production passes injectBug=0. No-op if `node` is not a
// BendFieldNode (defensive; the caller passes this op's factory output).
void configureBendField(FieldNode& node, float amount, float stepFactor, int axis, int injectBug) {
  if (auto* n = dynamic_cast<BendFieldNode*>(&node)) {
    n->amount = amount;
    n->stepFactor = stepFactor;
    n->axis = axis;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
