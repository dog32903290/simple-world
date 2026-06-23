// CombineFieldColor — multi-input fold combiner that blends the WHOLE field value (float4, color+w)
// of each child into the accumulator by a CombineMethod {Mix,Add,Multiply}. Unlike CombineSDF (which
// folds only the .w distance and color-blends .rgb separately), CombineFieldColor combines the entire
// float4 — so the fold IS observable in the golden's f.w readback.
//
// TiXL authority: external/tixl/Operators/Lib/field/combine/CombineFieldColor.cs (GetPostShaderCode,
// lines 38-76). It is a combiner on the MULTI-INPUT path: postShaderCode (== TiXL GetPostShaderCode)
// folds each just-recursed child into the parent context.
//
//   CombineFieldColor.cs:41-45  — <2 connected inputs -> just a skip comment.
//   CombineFieldColor.cs:52-56  — inputIndex==0: keep initial value  `f{parent} = f{sub};`
//   CombineFieldColor.cs:60-74  — inputIndex>0 by CombineMethods:
//       Mix      (cs:62-63): `f{parent} = lerp(f{parent},  f{sub}, {ShaderNode}K);`  (fork: lerp->mix)
//       Add      (cs:66-67): `f{parent} = f{parent} +  f{sub};`
//       Multiply (cs:70-71): `f{parent} = f{parent} * f{sub};`
//
// Params: K = scalar [GraphParam] (CombineFieldColor.cs:92-94). CombineMethod = enum {Mix,Add,Multiply}
// (CombineFieldColor.cs:82-87, 96-97) — a compile-time code SELECTOR (mirrors TiXL FlagCodeChanged on
// the InputSlot<int>), NOT packed (same discipline as CombineSDF's combineMethod / Torus Axis).
//
// NO globals (CombineFieldColor.cs registers no AddDefinitions / helper fns — the fold is pure inline
// lerp/+/*). NO inter-helper call -> NO forward-decl needed (confirmed: there are no helpers at all).
//
// Forks vs CombineFieldColor.cs:
//   (1) HLSL lerp -> MSL mix (Mix branch). No other HLSL intrinsics in this op.
//   (2) {ShaderNode}K  ({prefix}+K) — backward-traced from CombineSDF.cpp:288/330: prefix is
//       "<Type>_"+shortId+"_", K accessed P.<prefix>K. NOT TiXL's literal {ShaderNode} token.
//   (3) TiXL InputFields is a variadic MultiInputSlot; the base codegen handles N children, the UI here
//       exposes a fixed 2 (Input A / Input B) — same fork as CombineSDF (3).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- combine-method data table (ARCHITECTURE rule 7) ---------------------------------------------
// One row per CombineMethods value (index == enum value == CombineFieldColor.cs:82-87). Each row's
// `call` builds the RHS of `f{parent} = <here>;` for inputIndex>0 from the parent/sub field names and
// the K param expr. Operates on the WHOLE float4 (no .w/.rgb split) — verbatim from cs:62-71.
struct CombineMethod {
  const char* name;  // enum label (matches enum value name)
  bool usesK;        // does the fold reference K? (only Mix does)
  // RHS builder: a = f{parent}, b = f{sub}, k = P.<prefix>K.
  std::string (*call)(const std::string& a, const std::string& b, const std::string& k);
};

// Each builder returns the RHS of `f{parent} = <here>;` — verbatim from CombineFieldColor.cs:62-71.
std::string cMix(const std::string& a, const std::string& b, const std::string& k) {
  // cs:63 `lerp(f{parent},  f{sub}, {ShaderNode}K)` — fork (1) lerp->mix; cs's double-space after the
  // first comma is cosmetic (whitespace-insensitive in MSL); single space kept here.
  return "mix(" + a + ", " + b + ", " + k + ")";
}
std::string cAdd(const std::string& a, const std::string& b, const std::string&) {
  return a + " + " + b;  // cs:67 `f{parent} +  f{sub}`
}
std::string cMultiply(const std::string& a, const std::string& b, const std::string&) {
  return a + " * " + b;  // cs:71 `f{parent} * f{sub}`
}

// kMethods index == CombineMethods value (CombineFieldColor.cs:82-87).
static const CombineMethod kMethods[] = {
    /* 0 Mix      */ {"Mix", true, cMix},
    /* 1 Add      */ {"Add", false, cAdd},
    /* 2 Multiply */ {"Multiply", false, cMultiply},
};
constexpr int kMethodCount = static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0]));

// ---- CombineFieldColor codegen node (a FieldNode subclass; combiner — multi-input path) ----------

struct CombineFieldColorNode : FieldNode {
  float k = 0.f;          // CombineFieldColor.t3 default K. Packed [GraphParam].
  int combineMethod = 0;  // default = 0 (Mix). Compile-time selector, NOT packed.
  int injectBug = 0;      // TEST-ONLY: 1 = corrupt the REAL emit (swap the fold to Multiply). Production
                          // is always 0. Gated like field_ops_translate.cpp's injectBug; never wired by
                          // a cook — only the golden's configure overload sets it.

  explicit CombineFieldColorNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix.
    prefix = "CombineFieldColor_" + shortId + "_";
  }

  const CombineMethod& method() const {
    // injectBug==1 (TEST-ONLY): force the Multiply fold regardless of the selected method — corrupts the
    // REAL postShaderCode emit (the .call builder), NOT the golden's expected value. With the golden's
    // Add case, the field flips from (dA+dB) to (dA*dB) -> probes RED. (kMethods[2] == Multiply.)
    if (injectBug == 1) return kMethods[2];
    int m = (combineMethod >= 0 && combineMethod < kMethodCount) ? combineMethod : 0;
    return kMethods[m];
  }

  // NO globals (CombineFieldColor.cs has no AddDefinitions / helper fns).

  // Combiner: no pre code (the base seeds p<sub>/f<sub> from the parent before this).
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void postShaderCode(CodeAssembleCtx& c, int inputIndex) const override {
    // PARITY CombineFieldColor.cs:38-76 (GetPostShaderCode).
    //   <2 connected inputs -> just a skip comment (cs:41-45).
    if (c.contextIdStack.size() < 2) {
      c.appendCall("// skipping combine with single or no input...");
      return;
    }
    // contextId = parent (^2), subContextId = the just-recursed child (^1 = top).
    const std::string parent = c.contextIdStack[c.contextIdStack.size() - 2];
    const std::string sub = c.ctx();

    if (inputIndex == 0) {
      // Keep the first child's field as the accumulator (cs:52-56).
      c.appendCall("f" + parent + " = f" + sub + ";");
      return;
    }

    // i>0: fold the WHOLE float4 by the selected method (cs:60-74). K access qualified P. for the MSL
    // struct (HLSL read a bare {ShaderNode}K cbuffer name).
    const std::string kExpr = "P." + prefix + "K";
    const CombineMethod& md = method();
    const std::string rhs = md.call("f" + parent, "f" + sub, kExpr);
    c.appendCall("f" + parent + " = " + rhs + ";");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY K is a [GraphParam] (CombineFieldColor.cs:92-94). combineMethod is a compile-time selector
    // (NOT packed, like CombineSDF combineMethod) — packing it would corrupt the float layout.
    appendScalarParam(floatParams, paramFields, prefix + "K", k);
  }
};

NodeSpec combineFieldColorSpec() {
  NodeSpec s;
  s.type = "CombineFieldColor";
  s.title = "Combine Field Color";
  // Two Field inputs (fork (3): TiXL is a variadic MultiInputSlot; the base codegen handles N, the UI
  // exposes a fixed 2). dataType "Field" keeps them from wiring into Float/Points/Texture2D.
  PortSpec inA; inA.id = "InputA"; inA.name = "Input A"; inA.dataType = "Field"; inA.isInput = true;
  PortSpec inB; inB.id = "InputB"; inB.name = "Input B"; inB.dataType = "Field"; inB.isInput = true;
  // K = the Mix blend factor [GraphParam] float, .t3 default 0.0.
  PortSpec kp; kp.id = "K"; kp.name = "K"; kp.dataType = "Float"; kp.isInput = true;
  kp.def = 0.0f; kp.minV = 0.0f; kp.maxV = 1.0f;
  // CombineMethod = enum CODE SELECTOR (dropdown, Widget::Enum) — a Float port storing the enum index,
  // .t3 default 0 (Mix). NOT a [GraphParam] (never packed); the node's `combineMethod` int member
  // carries it at codegen time. Labels mirror CombineFieldColor.cs:82-87 by index.
  PortSpec cm; cm.id = "CombineMethod"; cm.name = "Combine Method"; cm.dataType = "Float";
  cm.isInput = true; cm.def = 0.0f; cm.minV = 0.0f; cm.maxV = 2.0f; cm.widget = Widget::Enum;
  cm.labels = {"Mix", "Add", "Multiply"};
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {inA, inB, kp, cm, out};
  return s;
}

// Factory: build a CombineFieldColorNode for an instance. K/combineMethod default to the .t3 values; a
// graph cook would override them from the node's params and WIRE inputs[] from the connected children.
// The factory does NOT wire inputs (the golden / cook builds the subtree explicitly).
std::shared_ptr<FieldNode> makeCombineFieldColor(const std::string& shortId) {
  return std::make_shared<CombineFieldColorNode>(shortId);
}

// PF-0c param-apply (WAVE 3): project a RESOLVED param map onto a CombineFieldColorNode via setter-lambdas
// (NOT offsetof). Slot ids EQUAL the NodeSpec PortSpec.id: K (the only packed [GraphParam] float) +
// CombineMethod (the compile-time fold selector, applyIntSelSlot — switches the emitted fold expr
// mix/+/* in the post line, NOT the float buffer). A missing key keeps the member's ctor .t3 default.
// injectBug is NOT a param (test-only via configureCombineFieldColorBug); production stays 0. Routed via
// fieldConfigurers().
void configureCombineFieldColorFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<CombineFieldColorNode*>(&node)) {
    applyFloatSlot(m, "K", [&](float v) { n->k = v; });
    applyIntSelSlot(m, "CombineMethod", [&](int v) { n->combineMethod = v; });
  }
}

// slot ids = the SAME ids configureCombineFieldColorFromParams applies (Option B guard, can't drift).
const FieldOp g_combineFieldColorOp(combineFieldColorSpec(), makeCombineFieldColor,
                                    configureCombineFieldColorFromParams, {"K", "CombineMethod"});

}  // namespace

// Param-cook seam (the hook the factory comment anticipates): set the fold params on a node built via
// makeFieldNode("CombineFieldColor", ...). The leaf type is TU-private; this free function downcasts
// inside the owning TU so callers (a graph-cook walk; the GPU golden) can override K / combineMethod
// without the type leaking. K is a packed [GraphParam]; combineMethod is the compile-time selector.
// No-op if `node` is not a CombineFieldColorNode (defensive).
void configureCombineFieldColor(FieldNode& node, float k, int combineMethod) {
  if (auto* n = dynamic_cast<CombineFieldColorNode*>(&node)) {
    n->k = k;
    n->combineMethod = combineMethod;
    n->injectBug = 0;  // production path: never corrupt the emit.
  }
}

// TEST-ONLY overload: same as above but threads an injectBug flag into the node's REAL emit (method()).
// Forward-declared by the golden; not part of the cook API. injectBug=1 forces the Multiply fold.
void configureCombineFieldColorBug(FieldNode& node, float k, int combineMethod, int injectBug) {
  if (auto* n = dynamic_cast<CombineFieldColorNode*>(&node)) {
    n->k = k;
    n->combineMethod = combineMethod;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
