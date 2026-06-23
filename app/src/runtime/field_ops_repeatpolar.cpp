// RepeatPolar — single-input field MODIFIER (PRE-wrap): repeats the wrapped field RADIALLY (polar
// folding) about one axis BEFORE the child is evaluated, by folding the sampling point's two
// perpendicular components into one angular wedge of 2*PI/Repetitions. Drives the same half of the
// field_graph single-input wrap branch (field_graph.cpp:82-86) as RepeatAxis/Translate: it emits ONLY
// preShaderCode (executed BEFORE recursing the child) and has no post code.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/RepeatPolar.cs
//   GetPreShaderCode(c, inputIndex) (RepeatPolar.cs:78-88):
//     _useMirror ? c.AppendCall($"pModPolarMirror(p{c}.{_axisCodes0[(int)_axis]}, {ShaderNode}Repetitions, {ShaderNode}Offset);")
//                : c.AppendCall($"pModPolar(p{c}.{_axisCodes0[(int)_axis]}, {ShaderNode}Repetitions, {ShaderNode}Offset);");
//     _axisCodes0 = { "zy", "zx", "yx" } (RepeatPolar.cs:91-96), indexed by Axis enum {X,Y,Z}
//     (RepeatPolar.cs:101-106). The swizzle is a TWO-component swizzle (the plane perpendicular to the
//     chosen axis), passed BY-REF into the helper's float2 param.
//   AddDefinitions (RepeatPolar.cs:38-76): always Globals["Common"]; then EITHER Globals["pModPolar"]
//     (no-mirror body, RepeatPolar.cs:64-74) OR Globals["pModPolarMirror"] (mirror body,
//     RepeatPolar.cs:44-60).
//   [GraphParam] InputSlot<float> Repetitions (RepeatPolar.cs:114-116); [GraphParam] InputSlot<float>
//     Offset (RepeatPolar.cs:118-120). InputSlot<int> Axis (MappedType AxisTypes, RepeatPolar.cs:111-112);
//     InputSlot<bool> Mirror (RepeatPolar.cs:122-123). One InputField; one Result. Axis/Mirror drive
//     FlagCodeChanged (RepeatPolar.cs:27-33) — COMPILE-TIME template selectors, NOT runtime uniforms.
//     Only Repetitions + Offset are packed.
//
// Branch: SINGLE-INPUT PRE-wrap (pre BEFORE child recurse; no post). Same shape as RepeatAxis.
//
// Forks vs RepeatPolar.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literals `{ShaderNode}Repetitions` / `{ShaderNode}Offset`,
//       where {ShaderNode} interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's
//       frozen convention (backward-traced from field_ops_combinesdf.cpp:288 / repeataxis.cpp:189 —
//       prefix = "<Type>_"+shortId+"_", accessed P.<prefix><Name>) reproduces EXACTLY that name -> the
//       emitted tokens are P.RepeatPolar_<id>_Repetitions / P.RepeatPolar_<id>_Offset. NOT a
//       forward-assumed literal; a wrong prefix reads the wrong/0 struct member and the golden bites.
//   (2) AXIS = compile-time Widget::Enum selector member `axis` (0/1/2 -> "zy"/"zx"/"yx"), NOT packed —
//       same path RepeatAxis's axis / CombineSDF's combineMethod use. The _axisCodes0 TWO-component
//       table is reproduced byte-verbatim here.
//   (3) MIRROR = compile-time bool selector member `mirror`. Off -> pModPolar; On -> pModPolarMirror.
//       Both helpers are self-contained (call only atan2/length/cos/sin/floor + the `mod` macro from
//       Common; NO inter-helper call) -> NO MSL forward-decl needed -> both enum values ship faithfully.
//   (4) HLSL->MSL helper port — the ★CUT-94 SWIZZLE FIX. TiXL's helper is
//       `void pModPolar(inout float2 p, ...)` and it is CALLED WITH A SWIZZLE ARG (p{c}.zy / .zx / .yx).
//       MSL cannot bind a swizzle to a non-const `thread float2&` (the BendField inout->thread& fork
//       would FAIL to compile on these swizzle call sites — exactly the RotateField precedent). So the
//       helper is ported BY-VALUE + RETURN and each call is a swizzle ASSIGNMENT
//       (`p{c}.<swiz> = pModPolar(p{c}.<swiz>, ...)`) — reproducing HLSL inout copy-in/copy-out
//       byte-identically (the fold math is deterministic). The body math (atan2 / length / cos / sin /
//       mod) is identical text. HLSL `atan2` -> MSL `atan2` (same name/semantics). The mirror body's
//       `if (mod(c,2.0) >= 1.0) a = -a;` is kept verbatim.
//   (5) `mod` — both helper bodies call `mod(...)`, which the Common include #defines as
//       `(x)-(y)*floor((x)/(y))` (RepeatPolar.cs:40 always registers Globals["Common"]). Covered by the
//       Common global this node registers; no extra helper.
//   Test-only seam: configureRepeatPolar sets the REAL Repetitions/Offset/axis/mirror AND an injectBug
//       (corrupt the REAL preShaderCode emit) so the golden's tooth bites the op's actual emit.
//       Production default off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- the always-on Common include (verbatim from ShaderGraphIncludes.cs) --------------------------
// RepeatPolar.cs:40 registers this UNCONDITIONALLY (carries PI + the `mod` macro both helpers use). Key
// "Common" matches TiXL nameof -> de-dup with any other node that registers "Common" (RepeatAxis /
// CombineSDF / StairCombineSDF) is exact (identical string, one copy).
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

// ---- pModPolar (the Mirror=Off path's helper) ----------------------------------------------------
// RepeatPolar.cs:64-74. Body byte-verbatim EXCEPT the ★CUT-94 swizzle fix (fork (4)): TiXL's
// `void pModPolar(inout float2 p, ...)` -> `float2 pModPolar(float2 p, ...) { ...; return p; }` so the
// swizzle call site (p{c}.zy etc.) is a legal lvalue assignment. The angular-fold math is identical
// text (atan2 / floor / mod / cos / sin). Calls only built-ins + the Common `mod` macro -> SOLE helper,
// no forward-decl.
static const char* kPModPolar =
    "// https://mercury.sexy/hg_sdf/\n"
    "// MSL fork: inout float2 p -> by-value + return (swizzle call site is a legal lvalue). Math identical.\n"
    "float2 pModPolar(float2 p, float repetitions, float offset) {\n"
    "    float angle = 2*PI/repetitions;\n"
    "    float a = atan2(p.y, p.x) + angle/2. +  offset / (180 *PI);\n"
    "    float r = length(p);\n"
    "    float c = floor(a/angle);\n"
    "    a = mod(a,angle) - angle/2.;\n"
    "    p = float2(cos(a), sin(a))*r;\n"
    "    return p;\n"
    "}";

// ---- pModPolarMirror (the Mirror=On path's helper) -----------------------------------------------
// RepeatPolar.cs:44-60. Body byte-verbatim EXCEPT the ★CUT-94 swizzle fix (fork (4)). The mirror branch
// `if (mod(c, 2.0) >= 1.0) a = -a;` flips every second wedge. Calls only built-ins + Common `mod` ->
// SOLE helper, no forward-decl.
static const char* kPModPolarMirror =
    "// https://mercury.sexy/hg_sdf/\n"
    "// MSL fork: inout float2 p -> by-value + return (swizzle call site is a legal lvalue). Math identical.\n"
    "float2 pModPolarMirror(float2 p, float repetitions, float offset) {\n"
    "    float angle = 2.0 * PI / repetitions;\n"
    "    float a = atan2(p.y, p.x) + angle / 2.0 + offset / (180.0 * PI);\n"
    "    float r = length(p);\n"
    "    float c = floor(a / angle);\n"
    "    a = mod(a, angle) - angle / 2.0;\n"
    "    \n"
    "    // Flip every second repetition by mirroring the angle\n"
    "    if (mod(c, 2.0) >= 1.0) {\n"
    "        a = -a;\n"
    "    }\n"
    "    \n"
    "    p = float2(cos(a), sin(a)) * r;\n"
    "    return p;\n"
    "}";

// Axis index -> TWO-component swizzle (RepeatPolar.cs:91-96 _axisCodes0). Compile-time selector. The
// swizzle picks the plane PERPENDICULAR to the chosen axis (X -> zy, Y -> zx, Z -> yx).
static const char* kAxisCodes[] = {"zy", "zx", "yx"};
constexpr int kAxisCount = static_cast<int>(sizeof(kAxisCodes) / sizeof(kAxisCodes[0]));

// ---- RepeatPolar codegen node (FieldNode subclass; single-input modifier — PRE-wrap path) ---------

struct RepeatPolarNode : FieldNode {
  float repetitions = 8.f;  // RepeatPolar.t3 default Repetitions = 8. [GraphParam], packed scalar.
  float offset = 0.f;       // RepeatPolar.t3 default Offset = 0. [GraphParam], packed scalar.
  int axis = 0;             // RepeatPolar.t3 default Axis = X (0). Compile-time selector, NOT packed.
  bool mirror = false;      // RepeatPolar.t3 default Mirror = false. Compile-time selector, NOT packed.
  // test-only bug modes (configureRepeatPolar): 0 = none, 1 = wrong axis (force "xy"), 2 = drop the pre
  // line (no polar fold). Both corrupt the OP'S REAL preShaderCode emit, not the expected golden value.
  int injectBug = 0;

  explicit RepeatPolarNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "RepeatPolar_" + shortId + "_";
  }

  const char* axisCode() const {
    int a = (axis >= 0 && axis < kAxisCount) ? axis : 0;
    return kAxisCodes[a];
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // RepeatPolar.cs:40 — Common is ALWAYS registered (carries PI + the `mod` macro both helpers use).
    c.globals["Common"] = kCommon;
    // RepeatPolar.cs:42-75 — mirror chooses the helper: pModPolar vs pModPolarMirror.
    if (mirror) {
      c.globals["pModPolarMirror"] = kPModPolarMirror;  // RepeatPolar.cs:44-60
    } else {
      c.globals["pModPolar"] = kPModPolar;  // RepeatPolar.cs:64-74
    }
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY RepeatPolar.cs:78-88 GetPreShaderCode. {c} = context id (root ""); the two-component swizzle
    // from _axisCodes0[axis] (fork (2)); {ShaderNode}Repetitions/Offset -> P.<prefix>Repetitions/Offset
    // (fork (1)). Emitted as a swizzle ASSIGNMENT (fork (4) by-value+return) BEFORE the child recursion
    // so the child samples the polar-folded point -> the shape repeats radially.
    const std::string ctx = c.ctx();
    if (injectBug == 2) return;  // drop the pre line -> no polar fold -> folded probe RED.
    // injectBug==1 forces the WRONG swizzle ("xy") so a fold on the wrong plane reddens the probe.
    const char* a = (injectBug == 1) ? "xy" : axisCode();
    const char* fn = mirror ? "pModPolarMirror" : "pModPolar";
    const std::string swiz = "p" + ctx + "." + a;
    c.appendCall(swiz + " = " + std::string(fn) + "(" + swiz + ", P." + prefix + "Repetitions, P." +
                 prefix + "Offset);");
  }

  // Modifier: no post code (RepeatPolar has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order: Repetitions (RepeatPolar.cs:114-116) then Offset
    // (RepeatPolar.cs:118-120). axis/mirror are compile-time code selectors (NOT packed). Two scalars
    // -> floats[0], floats[1].
    appendScalarParam(floatParams, paramFields, prefix + "Repetitions", repetitions);
    appendScalarParam(floatParams, paramFields, prefix + "Offset", offset);
  }
};

NodeSpec repeatPolarSpec() {
  NodeSpec s;
  s.type = "RepeatPolar";
  s.title = "Repeat Polar";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Repetitions = wedge count [GraphParam] float, .t3 default 8.0.
  PortSpec rep; rep.id = "Repetitions"; rep.name = "Repetitions"; rep.dataType = "Float"; rep.isInput = true;
  rep.def = 8.0f; rep.minV = 1.0f; rep.maxV = 64.0f;
  // Offset = angular offset (degrees-scaled per the .cs `offset/(180*PI)`) [GraphParam] float, .t3 0.0.
  PortSpec off; off.id = "Offset"; off.name = "Offset"; off.dataType = "Float"; off.isInput = true;
  off.def = 0.0f; off.minV = -180.0f; off.maxV = 180.0f;
  // Axis = enum CODE SELECTOR (Widget::Enum dropdown) storing the enum index. .t3 default 0 (X). NOT a
  // [GraphParam] (never packed); the node's `axis` int carries it at codegen time. Labels = AxisTypes
  // (RepeatPolar.cs:101-106).
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 0.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // Mirror = bool code selector (drawn as a 2-value Enum Off/On), .t3 default 0 (Off). NOT packed; the
  // node's `mirror` bool carries it. Off -> pModPolar; On -> pModPolarMirror.
  PortSpec mi; mi.id = "Mirror"; mi.name = "Mirror"; mi.dataType = "Float"; mi.isInput = true;
  mi.def = 0.0f; mi.minV = 0.0f; mi.maxV = 1.0f; mi.widget = Widget::Enum;
  mi.labels = {"Off", "On"};
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, rep, off, ax, mi, out};
  return s;
}

std::shared_ptr<FieldNode> makeRepeatPolar(const std::string& shortId) {
  return std::make_shared<RepeatPolarNode>(shortId);
}

// PF-0c param-apply (WAVE 3): project a RESOLVED param map onto a RepeatPolarNode via setter-lambdas (NOT
// offsetof). Slot ids EQUAL the NodeSpec PortSpec.id: Repetitions / Offset (packed [GraphParam] floats) +
// TWO compile-time code selectors — Axis (applyIntSelSlot, switches the two-component swizzle
// "zy"/"zx"/"yx") and Mirror (applyBoolSelSlot, switches the helper pModPolar vs pModPolarMirror + fn
// name). Both switch the emitted MSL text, NOT the float buffer. A missing key keeps the ctor .t3 default.
// injectBug is NOT a param (test-only via configureRepeatPolar); production stays 0. Routed via
// fieldConfigurers().
void configureRepeatPolarFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<RepeatPolarNode*>(&node)) {
    applyFloatSlot(m, "Repetitions", [&](float v) { n->repetitions = v; });
    applyFloatSlot(m, "Offset", [&](float v) { n->offset = v; });
    applyIntSelSlot(m, "Axis", [&](int v) { n->axis = v; });
    applyBoolSelSlot(m, "Mirror", [&](bool v) { n->mirror = v; });
  }
}

// slot ids = the SAME ids configureRepeatPolarFromParams applies (Option B guard, can't drift).
const FieldOp g_repeatPolarOp(repeatPolarSpec(), makeRepeatPolar, configureRepeatPolarFromParams,
                              {"Repetitions", "Offset", "Axis", "Mirror"});

}  // namespace

// Param-cook + test seam (mirrors configureRepeatAxis): set Repetitions/Offset/axis/mirror (and a
// test-only injectBug: 0 none / 1 wrong-swizzle / 2 drop-pre-line) on a makeFieldNode("RepeatPolar",...)
// node. The leaf type is TU-private; this downcasts inside the owning TU. Production passes injectBug=0.
void configureRepeatPolar(FieldNode& node, float repetitions, float offset, int axis, bool mirror,
                          int injectBug) {
  if (auto* n = dynamic_cast<RepeatPolarNode*>(&node)) {
    n->repetitions = repetitions;
    n->offset = offset;
    n->axis = axis;
    n->mirror = mirror;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
