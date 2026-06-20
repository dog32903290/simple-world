// RotateAxis — single-input field MODIFIER (PRE-wrap): rotates the sampling point around a coordinate
// axis (in the plane perpendicular to that axis) BEFORE the wrapped field is evaluated. Like Translate /
// ReflectField, this op drives the PRE half of the single-input wrap branch (field_graph.cpp:82-86): it
// emits preShaderCode (executed BEFORE recursing the child), so the child samples the rotated point ->
// the wrapped shape appears rotated about the chosen axis by -Rotation (the point is rotated, so the
// shape counter-rotates, exactly like Translate moves +Translation by shifting p by -Translation).
//
// TiXL authority: external/tixl/Operators/Lib/field/space/RotateAxis.cs
//   AddDefinitions (RotateAxis.cs:39-46): c.Globals["pRotateAxis"] = void pRotateAxis(inout float2 p,
//       float a){ p = cos(a)*p + sin(a) * float2(p.y, -p.x); }   (a planar 2D rotation of the swizzled
//       axis-pair; "R(p.xz,a) rotates x towards z").
//   GetPreShaderCode (RotateAxis.cs:49-53): axi = _axisCodes0[(int)_axis]; then
//       c.AppendCall($"pRotateAxis(p{c}.{axi}, {ShaderNode}Rotation / 180 * 3.141578);");
//       axi = the swizzle permutation chosen by the Axis enum (zy/zx/yx), {c} = ctx id (root "").
//   _axisCodes0 (RotateAxis.cs:56-61) = { "zy", "zx", "yx" } indexed by AxisTypes {X=0,Y=1,Z=2}.
//   [GraphParam] float Rotation (RotateAxis.cs:75-77); InputSlot<int> Axis (MappedType enum, NOT a
//   [GraphParam] — a compile-time code selector like BendField/CombineSDF/Torus Axis); one InputField;
//   one Slot<ShaderGraphNode> Result. Branch = single-input PRE-wrap (NO post code).
//
// Forks vs RotateAxis.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literal `{ShaderNode}Rotation`, where {ShaderNode}
//       interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen convention
//       (backward-traced from field_ops_combinesdf.cpp:288 / translate.cpp:46 — `prefix = "<Type>_" +
//       shortId + "_"`, accessed `P.<prefix><Name>`) reproduces EXACTLY that name. Emitted token:
//       `P.RotateAxis_<id>_Rotation`. NOT a forward-assumed literal; a wrong prefix reads the wrong/0
//       struct member (-> angle 0 -> identity rotation) and the golden's rotated probe bites.
//   (2) DEG->RAD CONSTANT — TiXL's literal is `/ 180 * 3.141578` (their idiosyncratic pi spelling, NOT
//       3.14159 — kept byte-exact for parity, do NOT "fix"). At Rotation=90 this is 1.570789, whose cos
//       is ~7.3e-6 (not exactly 0); the host golden uses the SAME constant so the values match to 1e-5.
//   (3) HLSL->MSL helper port — the CUT-94 SWIZZLE FIX is MANDATORY here. The .cs helper is
//       `void pRotateAxis(inout float2 p, float a)` and the CALL passes a SWIZZLE (p.zy / p.zx / p.yx).
//       MSL rejects binding a swizzle to a non-const `thread float2&`, so the helper is ported BY-VALUE +
//       RETURN and the call is a swizzle-ASSIGNMENT — this reproduces HLSL's inout copy-in/copy-out
//       byte-identically. Helper body (cos(a)*p + sin(a)*float2(p.y,-p.x)) is verbatim. No inter-helper
//       call (only cos/sin builtins) -> this is the SOLE globals key -> no MSL forward prototype needed.
//   (4) Axis enum -> compile-time swizzle selector member (`axis`), NOT packed (bendfield.cpp /
//       combinesdf.cpp:284 precedent). The _axisCodes0 table { "zy","zx","yx" } is byte-verbatim from
//       RotateAxis.cs:56-61; the emitted swizzle is `p{c}.<perm>` (a 2-component swizzle assigned from
//       the by-value helper's return).
//   Test-only seam: configureRotateAxis sets the REAL Rotation / Axis AND an injectBug that corrupts the
//   OP'S REAL preShaderCode emit (drop the pre line -> no rotation), so the golden's rotated tooth bites
//   the op's emit, not an expected-value tautology. Production default injectBug=0.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// pRotateAxis helper — ported from RotateAxis.cs:39-46 AddDefinitions with the CUT-94 swizzle fix:
//   HLSL `void pRotateAxis(inout float2 p, float a)` -> MSL `float2 pRotateAxis(float2 p, float a)`
//   BY-VALUE + RETURN (the call site passes a swizzle p.zy/p.zx/p.yx; MSL cannot bind a swizzle to a
//   non-const thread float2& ref, so the inout copy-in/copy-out is reproduced via value + return +
//   swizzle-assignment). Body math is byte-verbatim. No inter-helper call (only cos/sin) -> SOLE globals
//   key -> no forward prototype needed.
// NOTE (shared-key merge, Cut-95): RotateAxis and RotateField BOTH emit a helper FUNCTION named
// pRotateAxis. The field codegen de-dups globals by std::map KEY and concatenates each body verbatim
// (field_graph.cpp:213-216) with NO function-name de-dup. If the two ops used DIFFERENT keys, a graph
// containing BOTH would emit the pRotateAxis function TWICE -> MSL "redefinition of function" compile
// error (silent breakage in a mixed graph). TiXL itself has this latent shape (RotateAxis.cs key
// "pRotateAxis" vs RotateField.cs key "pRotateXYZ", same fn name) but both .cs bodies are BYTE-IDENTICAL
// (incl. all 3 comment lines + math). Resolution: BOTH ops register under the SAME key "pRotateAxis"
// with the BYTE-IDENTICAL body below -> de-dup collapses the mixed graph to ONE definition (compiles),
// and each op in ISOLATION emits byte-identical shader text vs its TiXL authority (the map KEY is a
// codegen-internal de-dup identity, NEVER emitted into the shader). The 3 comment lines are restored
// verbatim from RotateAxis.cs:40-42 (this leaf previously dropped the 3rd line — that was a parity miss).
static const char* kBodyRotateAxis =
    "// Rotate around a coordinate axis (i.e. in a plane perpendicular to that axis) by angle <a>.\n"
    "// Read like this: R(p.xz, a) rotates \"x towards z\".\n"
    "// This is fast if <a> is a compile-time constant and slower (but still practical) if not.\n"
    "float2 pRotateAxis(float2 p, float a) {\n"
    " p = cos(a)*p + sin(a) * float2(p.y, -p.x);\n"
    " return p;\n"
    "}";

// _axisCodes0 — byte-verbatim from RotateAxis.cs:56-61. Indexed by the Axis enum (X=0,Y=1,Z=2). The
// chosen permutation is the 2-component pair swizzled into pRotateAxis so the rotation acts in the plane
// perpendicular to that axis. NOT packed (enum is a compile-time code selector, bendfield.cpp precedent).
static const char* kAxisCodes[] = {"zy", "zx", "yx"};
constexpr int kAxisCount = static_cast<int>(sizeof(kAxisCodes) / sizeof(kAxisCodes[0]));

// ---- RotateAxis codegen node (a FieldNode subclass; single-input modifier — PRE-wrap path) ----------

struct RotateAxisNode : FieldNode {
  float rotation = 0.f;  // RotateAxis.t3 default Rotation = 0 -> identity rotation. Packed [GraphParam].
  int axis = 0;          // RotateAxis.t3 default Axis = 0 (X). Compile-time swizzle selector, NOT packed.
  // test-only bug mode (configureRotateAxis): 0 = none, 1 = drop the pre line (no rotation).
  int injectBug = 0;

  explicit RotateAxisNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "RotateAxis_" + shortId + "_";
  }

  int axisIdx() const { return (axis >= 0 && axis < kAxisCount) ? axis : 0; }

  void addGlobals(CodeAssembleCtx& c) const override {
    // RotateAxis.cs:39-46 — register pRotateAxis unconditionally (key matches TiXL "pRotateAxis").
    c.globals["pRotateAxis"] = kBodyRotateAxis;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY RotateAxis.cs:49-53 GetPreShaderCode:
    //   axi = _axisCodes0[(int)_axis];
    //   `pRotateAxis(p{c}.{axi}, {ShaderNode}Rotation / 180 * 3.141578);`
    // {c} = ctx id (root ""); {axi} = swizzle perm from the Axis enum; {ShaderNode}Rotation ->
    // P.<prefix>Rotation (fork (1)). CUT-94 fix: the call is a swizzle-ASSIGNMENT from the by-value
    // helper's return (HLSL inout copy-in/copy-out). Emitted BEFORE the child recursion so the child
    // samples the rotated point. The deg->rad constant `/ 180 * 3.141578` is byte-verbatim (fork (2)).
    const std::string ctx = c.ctx();
    if (injectBug == 1) return;  // drop the pre line -> no rotation -> rotated-probe RED.
    const std::string swiz = "p" + ctx + "." + kAxisCodes[axisIdx()];
    c.appendCall(swiz + " = pRotateAxis(" + swiz + ", P." + prefix + "Rotation / 180 * 3.141578);");
  }

  // Modifier: no post code (TiXL RotateAxis has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY Rotation is a [GraphParam] (RotateAxis.cs:75-77). Axis is NOT a [GraphParam] (compile-time
    // selector, never packed). Sole scalar -> floats[0].
    appendScalarParam(floatParams, paramFields, prefix + "Rotation", rotation);
  }
};

NodeSpec rotateAxisSpec() {
  NodeSpec s;
  s.type = "RotateAxis";
  s.title = "Rotate Axis";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Rotation = [GraphParam] float (degrees), .t3 default 0 (identity). RotateAxis.cs:75-77.
  PortSpec rot; rot.id = "Rotation"; rot.name = "Rotation"; rot.dataType = "Float"; rot.isInput = true;
  rot.def = 0.0f; rot.minV = -360.0f; rot.maxV = 360.0f;
  // Axis = enum CODE SELECTOR (Widget::Enum dropdown) — a Float port storing the enum index, .t3 default
  // 0 (X). NOT a [GraphParam] (never packed); the node's `axis` int member carries it at codegen time.
  // Labels mirror RotateAxis.cs:65-70 (X,Y,Z) by index.
  PortSpec ax; ax.id = "Axis"; ax.name = "Axis"; ax.dataType = "Float"; ax.isInput = true;
  ax.def = 0.0f; ax.minV = 0.0f; ax.maxV = 2.0f; ax.widget = Widget::Enum;
  ax.labels = {"X", "Y", "Z"};
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, rot, ax, out};
  return s;
}

std::shared_ptr<FieldNode> makeRotateAxis(const std::string& shortId) {
  return std::make_shared<RotateAxisNode>(shortId);
}

const FieldOp g_rotateAxisOp(rotateAxisSpec(), makeRotateAxis);

}  // namespace

// Param-cook + test seam (mirrors configureTranslate / configureBendField): set the REAL Rotation / Axis
// on a makeFieldNode("RotateAxis",...) node, plus a test-only injectBug (0 none / 1 drop-pre-line) that
// corrupts the OP's REAL preShaderCode emit. The leaf type is TU-private; this downcasts inside the
// owning TU. Production passes injectBug=0. No-op if `node` is not a RotateAxisNode (defensive).
void configureRotateAxis(FieldNode& node, float rotation, int axis, int injectBug) {
  if (auto* n = dynamic_cast<RotateAxisNode*>(&node)) {
    n->rotation = rotation;
    n->axis = axis;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
