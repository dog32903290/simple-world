// RepeatFieldLimit — single-input field MODIFIER (PRE-wrap): repeats the wrapped field along ONE axis,
// but only across a LIMITED range of cells (from index <Start> to <Stop>); points outside that range are
// clamped to the boundary cell instead of tiling forever. Drives the same half of the field_graph
// single-input wrap branch (field_graph.cpp:82-86) as Translate/RepeatAxis: it emits ONLY preShaderCode
// (executed BEFORE recursing the child) and has no post code.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/RepeatFieldLimit.cs
//   GetPreShaderCode(c, inputIndex) (RepeatFieldLimit.cs:61-65):
//     c.AppendCall($"pModLimited2(p{c}.{_axisCodes0[(int)_axis]}, {ShaderNode}Size, {ShaderNode}Start, {ShaderNode}Stop);");
//     _axisCodes0 = {"x","y","z"} (RepeatFieldLimit.cs:67-72), indexed by Axis enum {X,Y,Z}
//     (RepeatFieldLimit.cs:76-81). TiXL discards pModLimited2's `c` (cell-index) return.
//   AddDefinitions (RepeatFieldLimit.cs:36-58): always Globals["Common"] (carries the `mod` macro the
//     helper calls); then Globals["pModLimited2"] (helper body, RepeatFieldLimit.cs:40-57).
//   [GraphParam] order = .cs declaration order Size (RepeatFieldLimit.cs:89-91), Start
//     (RepeatFieldLimit.cs:93-95), Stop (RepeatFieldLimit.cs:97-99). InputSlot<int> Axis
//     (MappedType AxisTypes, RepeatFieldLimit.cs:86-87) — a COMPILE-TIME template selector (FlagCodeChanged
//     on change, RepeatFieldLimit.cs:24-31), NOT a packed uniform. One InputField; one Result.
//
// Branch: SINGLE-INPUT PRE-wrap (pre BEFORE child recurse; no post). Same shape as Translate/RepeatAxis.
//
// Forks vs RepeatFieldLimit.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literals `{ShaderNode}Size/Start/Stop`, where {ShaderNode}
//       interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen convention
//       (backward-traced from field_ops_combinesdf.cpp:288 / field_ops_translate.cpp:46 — prefix =
//       "<Type>_"+shortId+"_", accessed P.<prefix><Name>) reproduces EXACTLY those names -> emitted tokens
//       P.RepeatFieldLimit_<id>_Size / _Start / _Stop. NOT forward-assumed; a wrong prefix reads the
//       wrong/0 struct member and the golden's probes catch it.
//   (2) AXIS = compile-time Widget::Enum selector member `axis` (0/1/2 -> "x"/"y"/"z"), NOT packed — same
//       path CombineSDF's combineMethod / RepeatAxis Axis use. Indexing _axisCodes0 is reproduced by a
//       static const char* table here. NOTE: _axisCodes0 here is {"x","y","z"} (single-component swizzles),
//       NOT BendField's {"yzx","xzy","xyz"} 3-component perms — backward-traced from RepeatFieldLimit.cs:67.
//   (3) HLSL->MSL helper port (pModLimited2 body): TiXL's `inout float p` -> the frozen sw fork is
//       BY-VALUE + RETURN the folded float (Cut-94 fix, identical to RepeatAxis pMod1). The call site is a
//       SWIZZLE ASSIGNMENT (`p{c}.<axis> = pModLimited2(p{c}.<axis>, ...)`). MSL rejects binding a swizzle
//       (p.x) to a non-const reference, so `thread float& p` is illegal here; by-value+return reproduces
//       the HLSL inout copy-in/copy-out byte-identically. TiXL's optional `c` (cell-index) return is the
//       ONLY output we drop (the .cs discards it at the call site too) — we return the folded `p` instead.
//   (4) `mod` — pModLimited2's body calls `mod(...)`, which the Common include #defines as
//       `(x)-(y)*floor((x)/(y))` (RepeatFieldLimit.cs:38 always registers Globals["Common"]). Covered by
//       the Common global this node registers; no extra helper. pModLimited2 calls only that macro +
//       floor + arithmetic -> NO inter-helper call -> NO MSL forward-declaration needed (confirmed).
//   Test-only seam: configureRepeatFieldLimit sets the REAL Size/Start/Stop/axis AND an injectBug that
//       corrupts the OP'S REAL preShaderCode emit (e.g. swap Start/Stop arg order) so the golden's tooth
//       bites the op's actual emit, not an expected-value tautology. Production default injectBug=0.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- the always-on Common include (verbatim from ShaderGraphIncludes.cs, same string RepeatAxis uses) -
// RepeatFieldLimit.cs:38 registers this UNCONDITIONALLY. Key "Common" matches TiXL nameof so de-dup across
// a mixed graph (e.g. a RepeatAxis/CombineSDF that also registers "Common") is exact — identical string,
// one copy.
static const char* kCommon =
    "#ifndef PI\n"
    "#define PI 3.14159265\n"
    "#endif\n"
    "#ifndef TAU\n"
    "#define TAU (2*PI)\n"
    "#endif\n"
    "#ifndef PHI\n"
    "#define PHI (sqrt(5)*0.5 + 0.5)\n"
    "#endif\n"
    "\n"
    "#ifndef mod\n"
    "#define mod(x, y) ((x) - (y) * floor((x) / (y)))\n"
    "#endif";

// ---- pModLimited2 helper -------------------------------------------------------------------------
// Body byte-verbatim from RepeatFieldLimit.cs:40-57 with the frozen sw fork (fork (3)): TiXL's
// `inout float p` -> BY-VALUE + RETURN the folded float (Cut-94: a swizzle call site p.x can't bind to
// `thread float&`). The .cs returns the cell index `c`; sw drops that (the .cs call site discards it too)
// and returns the FOLDED point `p` instead, so the swizzle ASSIGNMENT call site gets the new coordinate.
// The fold/clamp math is byte-identical to the inout copy-in/copy-out. Calls only the `mod` macro (Common)
// + floor + arithmetic -> NO inter-helper call -> NO forward-decl needed (confirmed: the globals std::map
// holds exactly the keys "Common" and "pModLimited2").
static const char* kPModLimited2 =
    "// https://mercury.sexy/hg_sdf/\n"
    "// Repeat only a few times: from indices <start> to <stop> (similar to above, but more flexible)\n"
    "// MSL fork: inout float p -> by-value, returns the folded+clamped point (TiXL's optional cell-index\n"
    "// return is dropped; the .cs call site discards it too) so the swizzle call site is a legal lvalue\n"
    "// assignment. Math byte-identical to the HLSL inout copy-in/copy-out.\n"
    "float pModLimited2(float p, float size, float start, float stop) \n"
    "{\n"
    "    float halfsize = size*0.5;\n"
    "    float c = floor((p + halfsize)/size);\n"
    "    p = mod(p+halfsize, size) - halfsize;\n"
    "    if (c > stop) { //yes, this might not be the best thing numerically.\n"
    "\t    p += size*(c - stop);\n"
    "\t    c = stop;\n"
    "    }\n"
    "    if (c <start) {\n"
    "\t    p += size*(c - start);\n"
    "\t    c = start;\n"
    "    }\n"
    "    return p;\n"
    "}";

// Axis index -> swizzle component (RepeatFieldLimit.cs:67-72 _axisCodes0). Compile-time selector.
// NOTE single-component {"x","y","z"} (NOT BendField's 3-component perms).
static const char* kAxisCodes[] = {"x", "y", "z"};

// ---- RepeatFieldLimit codegen node (FieldNode subclass; single-input modifier — PRE-wrap path) -------

struct RepeatFieldLimitNode : FieldNode {
  float size = 1.f;   // RepeatFieldLimit.t3 default Size. [GraphParam], packed scalar.
  float start = 0.f;  // RepeatFieldLimit.t3 default Start (lowest kept cell index). [GraphParam], packed.
  float stop = 0.f;   // RepeatFieldLimit.t3 default Stop  (highest kept cell index). [GraphParam], packed.
  int axis = 0;       // RepeatFieldLimit.t3 default Axis = X (0). Compile-time selector, NOT packed.
  // test-only bug modes (configureRepeatFieldLimit): 0 = none, 1 = swap Start/Stop arg order (param-order
  // tooth), 2 = drop the pre line (no fold). Both corrupt the OP'S REAL preShaderCode emit, NOT the
  // expected golden value.
  int injectBug = 0;

  explicit RepeatFieldLimitNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "RepeatFieldLimit_" + shortId + "_";
  }

  const char* axisCode() const {
    int a = (axis >= 0 && axis < 3) ? axis : 0;
    return kAxisCodes[a];
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // RepeatFieldLimit.cs:38 — Common is ALWAYS registered (carries the `mod` macro pModLimited2 uses).
    c.globals["Common"] = kCommon;
    // RepeatFieldLimit.cs:40-57 — pModLimited2 always registered (the sole helper this op's pre line calls).
    c.globals["pModLimited2"] = kPModLimited2;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY RepeatFieldLimit.cs:61-65 GetPreShaderCode. {c} = context id (root ""); {ShaderNode}Size/
    // Start/Stop -> P.<prefix>Size/Start/Stop (fork (1)); axis swizzle from _axisCodes0[axis] (fork (2)).
    // The .cs passes the swizzle by inout and discards the return; sw's helper is by-value+return so the
    // call is a swizzle ASSIGNMENT (fork (3)). Emitted BEFORE the child recursion so the child samples the
    // folded/clamped point -> the shape repeats along <axis> only across cells [Start..Stop].
    const std::string ctx = c.ctx();
    if (injectBug == 2) return;  // drop the pre line -> no fold -> folded probe RED.
    const std::string swiz = "p" + ctx + "." + axisCode();
    const std::string startArg = "P." + prefix + "Start";
    const std::string stopArg = "P." + prefix + "Stop";
    // injectBug==1 SWAPS the Start/Stop args -> the cell-clamp range is inverted -> the +0.1 fold probe
    // reddens (a param-ORDER tooth biting the REAL emit, mirroring the collectParams depth-first order).
    const std::string& a3 = (injectBug == 1) ? stopArg : startArg;
    const std::string& a4 = (injectBug == 1) ? startArg : stopArg;
    c.appendCall(swiz + " = pModLimited2(" + swiz + ", P." + prefix + "Size, " + a3 + ", " + a4 + ");");
  }

  // Modifier: no post code (RepeatFieldLimit has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order: Size (RepeatFieldLimit.cs:89-91), Start
    // (RepeatFieldLimit.cs:93-95), Stop (RepeatFieldLimit.cs:97-99). axis is NOT a [GraphParam]
    // (compile-time selector, never packed). Three scalars -> floats[0], floats[1], floats[2].
    appendScalarParam(floatParams, paramFields, prefix + "Size", size);
    appendScalarParam(floatParams, paramFields, prefix + "Start", start);
    appendScalarParam(floatParams, paramFields, prefix + "Stop", stop);
  }
};

NodeSpec repeatFieldLimitSpec() {
  NodeSpec s;
  s.type = "RepeatFieldLimit";
  s.title = "Repeat Field Limit";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Axis = enum CODE SELECTOR (dropdown, Widget::Enum) storing the enum index. .t3 default 0 (X). NOT a
  // [GraphParam] (never packed); the node's `axis` int carries it at codegen time. Labels = AxisTypes
  // (RepeatFieldLimit.cs:76-81). Declared right after InputField, matching the .cs input order.
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 0.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // Size = the cell width [GraphParam] float, .t3 default 1.0. RepeatFieldLimit.cs:89-91.
  PortSpec sz; sz.id = "Size"; sz.name = "Size"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 1.0f; sz.minV = 0.001f; sz.maxV = 10.0f;
  // Start = lowest kept cell index [GraphParam] float, .t3 default 0. RepeatFieldLimit.cs:93-95.
  PortSpec st; st.id = "Start"; st.name = "Start"; st.dataType = "Float"; st.isInput = true;
  st.def = 0.0f; st.minV = -10.0f; st.maxV = 10.0f;
  // Stop = highest kept cell index [GraphParam] float, .t3 default 0. RepeatFieldLimit.cs:97-99.
  PortSpec sp; sp.id = "Stop"; sp.name = "Stop"; sp.dataType = "Float"; sp.isInput = true;
  sp.def = 0.0f; sp.minV = -10.0f; sp.maxV = 10.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, ax, sz, st, sp, out};
  return s;
}

std::shared_ptr<FieldNode> makeRepeatFieldLimit(const std::string& shortId) {
  return std::make_shared<RepeatFieldLimitNode>(shortId);
}

// PF-0c param-apply (WAVE 3): project a RESOLVED param map onto a RepeatFieldLimitNode via setter-lambdas
// (NOT offsetof). Slot ids EQUAL the NodeSpec PortSpec.id: Size / Start / Stop (packed [GraphParam]
// floats) + Axis (the compile-time swizzle code selector, applyIntSelSlot — switches the swizzle component
// "x"/"y"/"z" in the emitted MSL, NOT the float buffer). A missing key keeps the ctor .t3 default.
// injectBug is NOT a param (test-only via configureRepeatFieldLimit); production stays 0. Routed via
// fieldConfigurers().
void configureRepeatFieldLimitFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<RepeatFieldLimitNode*>(&node)) {
    applyFloatSlot(m, "Size", [&](float v) { n->size = v; });
    applyFloatSlot(m, "Start", [&](float v) { n->start = v; });
    applyFloatSlot(m, "Stop", [&](float v) { n->stop = v; });
    applyIntSelSlot(m, "Axis", [&](int v) { n->axis = v; });
  }
}

// slot ids = the SAME ids configureRepeatFieldLimitFromParams applies (Option B guard, can't drift).
const FieldOp g_repeatFieldLimitOp(repeatFieldLimitSpec(), makeRepeatFieldLimit,
                                   configureRepeatFieldLimitFromParams,
                                   {"Size", "Start", "Stop", "Axis"});

}  // namespace

// Param-cook + test seam (mirrors configureTranslate / configureRepeatAxis): set Size/Start/Stop/axis (and
// a test-only injectBug: 0 none / 1 swap-Start-Stop-arg-order / 2 drop-pre-line) on a
// makeFieldNode("RepeatFieldLimit",...) node. The leaf type is TU-private; this downcasts inside the
// owning TU. Production passes injectBug=0. No-op if `node` is not a RepeatFieldLimitNode (defensive).
void configureRepeatFieldLimit(FieldNode& node, float size, float start, float stop, int axis,
                               int injectBug) {
  if (auto* n = dynamic_cast<RepeatFieldLimitNode*>(&node)) {
    n->size = size;
    n->start = start;
    n->stop = stop;
    n->axis = axis;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
