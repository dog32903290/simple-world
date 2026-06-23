// RepeatField3 — single-input field MODIFIER (PRE-wrap): folds the sampling point into a repeating cell
// of size Size BEFORE the wrapped field is evaluated, tiling the shape infinitely along x/y/z. Drives the
// SAME single-input wrap branch as Translate (field_graph.cpp:82-86): emits preShaderCode (executed
// BEFORE recursing the child), so the child samples the folded point. No post code (a modifier).
//
// TiXL authority: external/tixl/Operators/Lib/field/space/RepeatField3.cs
//   AddDefinitions(c):  c.Globals["Common"] = ShaderGraphIncludes.Common;            (.cs:28)
//                       c.Globals["pMod3"]  = """ void pMod3(inout float3 p, float3 size){...} """  (.cs:30-35)
//   GetPreShaderCode(c, i): c.AppendCall($"pMod3(p{c}.xyz, {ShaderNode}Size);");      (.cs:38-41)
//   [GraphParam] InputSlot<Vector3> Size;  one InputField; one Slot<ShaderGraphNode> Result.
//
// pMod3 CALLS the `mod` macro, which lives in the `Common` block (ShaderGraphIncludes.Common:22-24,
// `#define mod(x,y) ((x)-(y)*floor((x)/(y))`). So BOTH globals MUST register — `Common` is the macro
// definition pMod3 depends on. `mod` is a #define (textual macro), not a function, so there is NO
// inter-helper FUNCTION call: no MSL forward-declaration is needed (fork-5 only applies when one helper
// fn calls another helper fn; a macro expands inline). Confirmed: pMod3 is the only helper fn and it
// calls only the mod macro -> no forward-decl.
//
// Forks vs RepeatField3.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literal `{ShaderNode}Size`, where {ShaderNode} interpolates
//       to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen convention (backward-
//       traced from field_ops_combinesdf.cpp:288 / field_ops_translate.cpp:46 — `prefix = "<Type>_" +
//       shortId + "_"`, accessed `P.<prefix><Name>`) reproduces EXACTLY that name. Emitted token is
//       `P.RepeatField3_<id>_Size` — TiXL's `{ShaderNode}Size` with the MSL `P.` cbuffer qualifier. NOT
//       a forward-assumed literal; a wrong prefix reads the wrong/0 struct member and the golden catches it.
//   (2) HLSL->MSL helper signature — `void pMod3(inout float3 p, float3 size)` -> `void pMod3(thread
//       float3& p, float3 size)` (inout -> thread X&, only inside the helper). The body math
//       `p = mod(p + size*0.5, size) - size*0.5;` is byte-identical text (mod is the macro from Common).
//   (3) Vector3 Size packed via appendVec3Param (packed_float3, 16B-align) — the SAME path Translate's
//       Translation / BoxSDF's Center use. Size is the only [GraphParam], so it lands at floats[0..2].
//   Test-only seam: configureRepeatField3 sets the REAL Size AND an injectBug (drop the Size division in
//   the helper / drop the pre line) so the golden's tooth bites the op's REAL emit path. Production off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// `Common` block — byte-verbatim from ShaderGraphIncludes.Common (the macro block pMod3 depends on,
// esp. the `mod` macro at lines 22-24). Same text combinesdf.cpp's kCommon carries; duplicated here so
// this leaf TU is self-contained (de-duped at assembly time by the std::map["Common"] key — if CombineSDF
// is also in the graph, identical content collapses to one emission).
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

// pMod3 helper — body byte-verbatim from RepeatField3.cs:30-35 with fork (2): HLSL `inout float3 p` ->
// MSL `thread float3& p`. Comment line `// https://mercury.sexy/hg_sdf/` preserved (it's part of the
// registered string). Calls only the `mod` macro (from Common) -> no inter-helper fn call -> no
// forward-decl. injectBug=1 corruption (drop Size division) is gated into the REAL emit below, NOT here.
static const char* kPMod3 =
    "// https://mercury.sexy/hg_sdf/\n"
    "float3 pMod3(float3 p, float3 size) {\n"
    "p = mod(p + size*0.5, size) - size*0.5;\n"
    "return p;\n"
    "}";

// Bugged pMod3 body (injectBug=1): drops the `/ size` inside mod's denominator effect by removing the
// modulo entirely -> p passes through UNFOLDED -> the +0.5-cell probe no longer wraps to 0 -> RED.
// (Corrupts the REAL helper the op registers; the expected golden value is never touched.)
static const char* kPMod3Bug =
    "// https://mercury.sexy/hg_sdf/\n"
    "float3 pMod3(float3 p, float3 size) {\n"
    "p = p;\n"
    "return p;\n"
    "}";

// ---- RepeatField3 codegen node (a FieldNode subclass; single-input modifier — PRE-wrap path) --------

struct RepeatField3Node : FieldNode {
  float sx = 1.f, sy = 1.f, sz = 1.f;  // RepeatField3.t3 default Size. Packed [GraphParam].
  // test-only bug modes (configureRepeatField3): 0 = none, 1 = corrupt pMod3 (drop the fold), 2 = drop
  // the pre line entirely.
  int injectBug = 0;

  explicit RepeatField3Node(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "RepeatField3_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // RepeatField3.cs:28,30 — two globals (keys match TiXL nameof, de-duped by std::map). `Common`
    // carries the `mod` macro pMod3 expands. injectBug=1 swaps in the bugged pMod3 body (REAL emit).
    c.globals["Common"] = kCommon;
    c.globals["pMod3"] = (injectBug == 1) ? kPMod3Bug : kPMod3;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY RepeatField3.cs:38-41 GetPreShaderCode: `pMod3(p{c}.xyz, {ShaderNode}Size);`. {c} = context
    // id (root ""); {ShaderNode}Size -> P.<prefix>Size (fork (1)). Emitted BEFORE the child recursion so
    // the child samples the folded (tiled) point.
    const std::string ctx = c.ctx();
    if (injectBug == 2) return;  // drop the pre line -> no fold -> shifted-cell probe RED.
    const std::string swiz = "p" + ctx + ".xyz";
    c.appendCall(swiz + " = pMod3(" + swiz + ", P." + prefix + "Size);");
  }

  // Modifier: no post code (TiXL RepeatField3 has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY Size is a [GraphParam] (RepeatField3.cs:47-49). packed_float3 via the frozen helper
    // (appendVec3Param owns the 16B-align padding). Sole param -> floats[0..2].
    appendVec3Param(floatParams, paramFields, prefix + "Size", sx, sy, sz);
  }
};

NodeSpec repeatField3Spec() {
  NodeSpec s;
  s.type = "RepeatField3";
  s.title = "Repeat Field 3";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Size = Vec3 head run (.x/.y/.z), [GraphParam], default (1,1,1). Same Widget::Vec/vecArity shape
  // Translate's Translation / BoxSDF's Center use (a 3-float vec drawn as one widget).
  PortSpec sxp; sxp.id = "Size.x"; sxp.name = "Size"; sxp.dataType = "Float"; sxp.isInput = true;
  sxp.def = 1.0f; sxp.minV = 0.001f; sxp.maxV = 10.0f; sxp.widget = Widget::Vec; sxp.vecArity = 3;
  PortSpec syp; syp.id = "Size.y"; syp.name = "Size.y"; syp.dataType = "Float"; syp.isInput = true;
  syp.def = 1.0f; syp.minV = 0.001f; syp.maxV = 10.0f;
  PortSpec szp; szp.id = "Size.z"; szp.name = "Size.z"; szp.dataType = "Float"; szp.isInput = true;
  szp.def = 1.0f; szp.minV = 0.001f; szp.maxV = 10.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, sxp, syp, szp, out};
  return s;
}

std::shared_ptr<FieldNode> makeRepeatField3(const std::string& shortId) {
  return std::make_shared<RepeatField3Node>(shortId);
}

// PF-0c param-apply: project a RESOLVED param map onto a RepeatField3Node via setter-lambdas (NOT offsetof).
// Slot ids EQUAL the NodeSpec PortSpec.id (Size.x/.y/.z). injectBug is NOT a param (test-only, set via the
// positional configureRepeatField3 seam); production stays 0. A missing key keeps the member's ctor .t3
// default. Routed via the fieldConfigurers() table.
void configureRepeatField3FromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<RepeatField3Node*>(&node)) {
    applyFloatSlot(m, "Size.x", [&](float v) { n->sx = v; });
    applyFloatSlot(m, "Size.y", [&](float v) { n->sy = v; });
    applyFloatSlot(m, "Size.z", [&](float v) { n->sz = v; });
  }
}

// slot ids = the SAME ids configureRepeatField3FromParams applies (Option B guard reads them, can't drift).
const FieldOp g_repeatField3Op(repeatField3Spec(), makeRepeatField3, configureRepeatField3FromParams,
                               {"Size.x", "Size.y", "Size.z"});

}  // namespace

// Param-cook + test seam owned by this TU (leaf type TU-private). Sets the Size vector and a test-only
// injectBug: 0 none / 1 corrupt-pMod3-fold / 2 drop-pre-line. Mirrors configureTranslate. The downcast
// runs inside the owning TU. Production passes injectBug=0.
void configureRepeatField3(FieldNode& node, float sx, float sy, float sz, int injectBug) {
  if (auto* n = dynamic_cast<RepeatField3Node*>(&node)) {
    n->sx = sx;
    n->sy = sy;
    n->sz = sz;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
