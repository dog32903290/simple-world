// RepeatAxis — single-input field MODIFIER (PRE-wrap): repeats the wrapped field along ONE axis by
// folding the sampling point into a cell of width Size before the child is evaluated. Drives the same
// half of the field_graph single-input wrap branch (field_graph.cpp:82-86) as Translate: it emits ONLY
// preShaderCode (executed BEFORE recursing the child) and has no post code.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/RepeatAxis.cs
//   GetPreShaderCode(c, inputIndex) (RepeatAxis.cs:62-72):
//     _useMirror ? c.AppendCall($"pModMirror1(p{c}.{_axisCodes0[(int)_axis]}, {ShaderNode}Size);")
//                : c.AppendCall($"pMod1(p{c}.{_axisCodes0[(int)_axis]}, {ShaderNode}Size);");
//     _axisCodes0 = {"x","y","z"} (RepeatAxis.cs:74-79), indexed by Axis enum {X,Y,Z} (RepeatAxis.cs:84-89).
//   AddDefinitions (RepeatAxis.cs:38-60): always Globals["Common"]; then EITHER Globals["pModMirror1"]
//     (mirror body, RepeatAxis.cs:44-54) OR Globals["CommonHgSdf"] (which carries pMod1).
//   [GraphParam] InputSlot<float> Size (RepeatAxis.cs:94-96); InputSlot<int> Axis (MappedType AxisTypes,
//     RepeatAxis.cs:98-99); InputSlot<bool> Mirror (RepeatAxis.cs:101-102). One InputField; one Result.
//   The Axis/Mirror enums drive FlagCodeChanged (RepeatAxis.cs:25-31) — they are COMPILE-TIME template
//   selectors, not runtime uniforms. Only Size is packed.
//
// Branch: SINGLE-INPUT PRE-wrap (pre BEFORE child recurse; no post). Same shape as Translate.
//
// Forks vs RepeatAxis.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literal `{ShaderNode}Size`, where {ShaderNode} interpolates
//       to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen convention (backward-
//       traced from field_ops_combinesdf.cpp:288 / field_ops_translate.cpp:46 — prefix = "<Type>_"+
//       shortId+"_", accessed P.<prefix><Name>) reproduces EXACTLY that name -> emitted token is
//       `P.RepeatAxis_<id>_Size`. NOT a forward-assumed literal; a wrong prefix reads the wrong/0
//       struct member and the golden's center probe catches it.
//   (2) AXIS = compile-time Widget::Enum selector member `axis` (0/1/2 -> "x"/"y"/"z"), NOT packed —
//       same path CombineSDF's combineMethod / Torus Axis use. Indexing _axisCodes0 is reproduced by a
//       static const char* table here.
//   (3) MIRROR = compile-time bool selector member `mirror`. Off -> pMod1 (CommonHgSdf); On ->
//       pModMirror1 (its own global). Both helpers are clean (no inter-helper calls -> NO forward-decl
//       needed; confirmed below), so BOTH enum values ship faithfully.
//   (4) HLSL->MSL: pMod1's `inout float p` and pModMirror1's `inout float p` -> `thread float& p`
//       (the only MSL fork inside the helper bodies; math text identical). pMod1 is reused VERBATIM from
//       the CommonHgSdf block already vetted in field_ops_combinesdf.cpp (`thread float& p` form);
//       pModMirror1 is transcribed from RepeatAxis.cs:47-53 with the same inout->thread& fork.
//   (5) `mod` — pModMirror1's body calls `mod(...)`, which the Common include #defines as
//       `(x)-(y)*floor((x)/(y))` (RepeatAxis.cs:40 always registers Globals["Common"]). pMod1's body
//       (inside CommonHgSdf) also uses that mod macro. Both are covered by the Common/CommonHgSdf globals
//       this node registers, so no extra helper.
//   Test-only seam: configureRepeatAxis sets the REAL Size/axis/mirror AND an injectBug (corrupt the REAL
//       preShaderCode emit) so the golden's tooth bites the op's actual emit. Production default off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- the always-on Common include (verbatim from ShaderGraphIncludes.cs) --------------------------
// RepeatAxis.cs:40 registers this UNCONDITIONALLY. Key "Common" matches TiXL nameof so de-dup across a
// mixed graph (e.g. a CombineSDF that also registers "Common") is exact — identical string, one copy.
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

// ---- CommonHgSdf (the Mirror=Off path's helper home) ---------------------------------------------
// RepeatAxis.cs:58 registers Globals["CommonHgSdf"] when NOT mirroring. This block carries pMod1
// (the only helper the no-mirror path calls). Byte-verbatim from the SAME ShaderGraphIncludes.cs block
// already vetted in field_ops_combinesdf.cpp (kCommonHgSdf) — including the inout->thread& MSL fork on
// pMod1/pR/pR45. Key "CommonHgSdf" matches TiXL nameof -> de-dups with CombineSDF's copy.
static const char* kCommonHgSdf =
    "\n"
    "// Sign function that doesn't return 0\n"
    "float sgn(float x) {\n"
    "\treturn (x<0)?-1:1;\n"
    "}\n"
    "\n"
    "float2 sgn(float2 v) {\n"
    "\treturn float2((v.x<0)?-1:1, (v.y<0)?-1:1);\n"
    "}\n"
    "\n"
    "float square (float x) {\n"
    "\treturn x*x;\n"
    "}\n"
    "\n"
    "float2 square (float2 x) {\n"
    "\treturn x*x;\n"
    "}\n"
    "\n"
    "float3 square (float3 x) {\n"
    "\treturn x*x;\n"
    "}\n"
    "\n"
    "float lengthSqr(float3 x) {\n"
    "\treturn dot(x, x);\n"
    "}\n"
    "\n"
    "\n"
    "// Maximum/minumum elements of a vector\n"
    "float vmax(float2 v) {\n"
    "\treturn max(v.x, v.y);\n"
    "}\n"
    "\n"
    "float vmax(float3 v) {\n"
    "\treturn max(max(v.x, v.y), v.z);\n"
    "}\n"
    "\n"
    "float vmax(float4 v) {\n"
    "\treturn max(max(v.x, v.y), max(v.z, v.w));\n"
    "}\n"
    "\n"
    "float vmin(float2 v) {\n"
    "\treturn min(v.x, v.y);\n"
    "}\n"
    "\n"
    "float vmin(float3 v) {\n"
    "\treturn min(min(v.x, v.y), v.z);\n"
    "}\n"
    "\n"
    "float vmin(float4 v) {\n"
    "\treturn min(min(v.x, v.y), min(v.z, v.w));\n"
    "}\n"
    "\n"
    "// Rotate around a coordinate axis (i.e. in a plane perpendicular to that axis) by angle <a>.\n"
    "// Read like this: R(p.xz, a) rotates \"x towards z\".\n"
    "// This is fast if <a> is a compile-time constant and slower (but still practical) if not.\n"
    "void pR(thread float2& p, float a) {\n"
    "\tp = cos(a)*p + sin(a)*float2(p.y, -p.x);\n"
    "}\n"
    "\n"
    "// Shortcut for 45-degrees rotation\n"
    "void pR45(thread float2& p) {\n"
    "\tp = (p + float2(p.y, -p.x))*sqrt(0.5);\n"
    "}\n"
    "\n"
    "// Repeat space along one axis. Use like this to repeat along the x axis:\n"
    "// <float cell = pMod1(p.x,5);> - using the return value is optional.\n"
    "// MSL fork: HLSL inout float p -> by-value float p, the FOLDED point is returned (the only output\n"
    "// our call sites consume; TiXL's optional cell return is dropped) so a SWIZZLE call site (p.x) can\n"
    "// be a legal lvalue assignment target (MSL rejects a swizzle bound to a non-const reference). The\n"
    "// fold math is byte-identical to the inout copy-in/copy-out.\n"
    "float pMod1(float p, float size) {\n"
    "\tfloat halfsize = size*0.5;\n"
    "\tfloat c = floor((p + halfsize)/size);\n"
    "\tp = mod(p + halfsize, size) - halfsize;\n"
    "\treturn p;\n"
    "}";

// ---- pModMirror1 (the Mirror=On path's helper) ---------------------------------------------------
// RepeatAxis.cs:44-54 registers Globals["pModMirror1"] when mirroring. Body byte-verbatim from
// RepeatAxis.cs:47-53 with the inout->thread& MSL fork (fork (4)). Calls only `mod` (Common macro) —
// NO inter-helper call -> NO MSL forward-declaration needed (confirmed: unlike CombineSDF's
// fOpDifference* which call fOpIntersection*, this helper is self-contained).
static const char* kPModMirror1 =
    "// https://mercury.sexy/hg_sdf/\n"
    "// Same, but mirror every second cell so they match at the boundaries\n"
    "// MSL fork: inout float p -> by-value, returns the folded+mirrored point (TiXL's optional cell\n"
    "// return dropped) so the swizzle call site is a legal lvalue assignment. Math byte-identical.\n"
    "float pModMirror1(float p, float size) {\n"
    "\tfloat halfsize = size*0.5;\n"
    "\tfloat c = floor((p + halfsize)/size);\n"
    "\tp = mod(p + halfsize,size) - halfsize;\n"
    "\tp *= mod(c, 2.0)*2 - 1;\n"
    "\treturn p;\n"
    "}";

// Axis index -> swizzle component (RepeatAxis.cs:74-79 _axisCodes0). Compile-time selector.
static const char* kAxisCodes[] = {"x", "y", "z"};

// ---- RepeatAxis codegen node (FieldNode subclass; single-input modifier — PRE-wrap path) ---------

struct RepeatAxisNode : FieldNode {
  float size = 1.f;   // RepeatAxis.t3 default Size. [GraphParam], packed scalar.
  int axis = 0;       // RepeatAxis.t3 default Axis = X (0). Compile-time selector, NOT packed.
  bool mirror = false;// RepeatAxis.t3 default Mirror = false. Compile-time selector, NOT packed.
  // test-only bug modes (configureRepeatAxis): 0 = none, 1 = wrong axis (force "y"), 2 = drop the pre
  // line (no fold). Both corrupt the OP'S REAL preShaderCode emit, not the expected golden value.
  int injectBug = 0;

  explicit RepeatAxisNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "RepeatAxis_" + shortId + "_";
  }

  const char* axisCode() const {
    int a = (axis >= 0 && axis < 3) ? axis : 0;
    return kAxisCodes[a];
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // RepeatAxis.cs:40 — Common is ALWAYS registered (carries the `mod` macro both helpers use).
    c.globals["Common"] = kCommon;
    // RepeatAxis.cs:42-59 — mirror chooses the helper home: pModMirror1 vs CommonHgSdf (pMod1).
    if (mirror) {
      c.globals["pModMirror1"] = kPModMirror1;  // RepeatAxis.cs:44-54
    } else {
      c.globals["CommonHgSdf"] = kCommonHgSdf;  // RepeatAxis.cs:58 (pMod1 lives here)
    }
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY RepeatAxis.cs:62-72 GetPreShaderCode. {c} = context id (root ""); {ShaderNode}Size ->
    // P.<prefix>Size (fork (1)); axis swizzle from _axisCodes0[axis] (fork (2)). Emitted BEFORE the
    // child recursion so the child samples the folded point -> the shape repeats along <axis>.
    const std::string ctx = c.ctx();
    if (injectBug == 2) return;  // drop the pre line -> no fold -> folded probe RED.
    // injectBug==1 forces the WRONG axis ("y") so a fold on the wrong component reddens the x-probe.
    const char* a = (injectBug == 1) ? "y" : axisCode();
    const char* fn = mirror ? "pModMirror1" : "pMod1";
    const std::string swiz = "p" + ctx + "." + a;
    c.appendCall(swiz + " = " + std::string(fn) + "(" + swiz + ", P." + prefix + "Size);");
  }

  // Modifier: no post code (RepeatAxis has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY Size is a [GraphParam] (RepeatAxis.cs:94-96). axis/mirror are compile-time code selectors
    // (NOT packed, like CombineSDF combineMethod) — packing them would corrupt the float layout.
    appendScalarParam(floatParams, paramFields, prefix + "Size", size);
  }
};

NodeSpec repeatAxisSpec() {
  NodeSpec s;
  s.type = "RepeatAxis";
  s.title = "Repeat Axis";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Size = the cell width [GraphParam] float, .t3 default 1.0.
  PortSpec sz; sz.id = "Size"; sz.name = "Size"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 1.0f; sz.minV = 0.001f; sz.maxV = 10.0f;
  // Axis = enum CODE SELECTOR (dropdown, Widget::Enum) storing the enum index. .t3 default 0 (X). NOT a
  // [GraphParam] (never packed); the node's `axis` int carries it at codegen time. Labels = AxisTypes
  // (RepeatAxis.cs:84-89).
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 0.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  // Mirror = bool code selector (drawn as a 2-value Enum Off/On), .t3 default 0 (Off). NOT packed; the
  // node's `mirror` bool carries it. Off -> pMod1; On -> pModMirror1.
  PortSpec mi; mi.id = "Mirror"; mi.name = "Mirror"; mi.dataType = "Float"; mi.isInput = true;
  mi.def = 0.0f; mi.minV = 0.0f; mi.maxV = 1.0f; mi.widget = Widget::Enum;
  mi.labels = {"Off", "On"};
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, sz, ax, mi, out};
  return s;
}

std::shared_ptr<FieldNode> makeRepeatAxis(const std::string& shortId) {
  return std::make_shared<RepeatAxisNode>(shortId);
}

const FieldOp g_repeatAxisOp(repeatAxisSpec(), makeRepeatAxis);

}  // namespace

// Param-cook + test seam (mirrors configureTranslate / configureCombineSdf): set Size/axis/mirror (and a
// test-only injectBug: 0 none / 1 wrong-axis / 2 drop-pre-line) on a makeFieldNode("RepeatAxis",...)
// node. The leaf type is TU-private; this downcasts inside the owning TU. Production passes injectBug=0.
void configureRepeatAxis(FieldNode& node, float size, int axis, bool mirror, int injectBug) {
  if (auto* n = dynamic_cast<RepeatAxisNode*>(&node)) {
    n->size = size;
    n->axis = axis;
    n->mirror = mirror;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
