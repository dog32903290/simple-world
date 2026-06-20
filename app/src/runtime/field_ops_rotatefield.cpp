// RotateField — single-input field MODIFIER (PRE-wrap): rotates the sampling point about the three
// coordinate axes BEFORE the wrapped field is evaluated, so the child samples the rotated point and the
// shape appears rotated. Like Translate / ReflectField it drives the PRE half of the single-input wrap
// branch (field_graph.cpp:82-86): it emits preShaderCode (executed BEFORE recursing the child), NO post
// code. The three pre lines apply sequential axis rotations (X then Y then Z), each via pRotateAxis on a
// 2-component swizzle of p.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/RotateField.cs
//   AddDefinitions (RotateField.cs:37-44): c.Globals["pRotateXYZ"] = pRotateAxis(inout float2 p, float a)
//       body `p = cos(a)*p + sin(a) * float2(p.y, -p.x);` (the globals KEY is "pRotateXYZ", the helper
//       FUNCTION is named pRotateAxis — both kept byte-verbatim).
//   GetPreShaderCode (RotateField.cs:47-54): three c.AppendCall lines, verbatim:
//       pRotateAxis(p{c}.zy, {ShaderNode}RotateRad.x);
//       pRotateAxis(p{c}.zx, {ShaderNode}RotateRad.y);
//       pRotateAxis(p{c}.yx, {ShaderNode}RotateRad.z);
//   AdditionalParameters (RotateField.cs:17-19): one Parameter("float3","RotateRad",Zero).
//   Update (RotateField.cs:27): RotateRad.Value = Rotation.GetValue(ctx) * MathUtils.ToRad — i.e. the
//       HOST pre-multiplies the user's Rotation (DEGREES) by ToRad (=PI/180) before packing, so the
//       shader receives RADIANS. RotateField has ONE InputField + Slot<ShaderGraphNode> Result.
//
// Forks vs RotateField.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes the literal `{ShaderNode}RotateRad`, where {ShaderNode}
//       interpolates to ShaderGraphNode.BuildNodeId = "<TypeName>_<shortGuid>_". sw's frozen convention
//       (backward-traced from field_ops_combinesdf.cpp:288 / translate.cpp:46 — `prefix = "<Type>_" +
//       shortId + "_"`, accessed `P.<prefix><Name>`) reproduces EXACTLY that name. Emitted tokens:
//       `P.RotateField_<id>_RotateRad.x` / `.y` / `.z`. NOT a forward-assumed literal; a wrong prefix
//       reads the wrong/0 struct member and the golden's rotated probe bites.
//   (2) HLSL->MSL helper port (pRotateAxis body) — the ★CUT-94 SWIZZLE FIX. TiXL's helper is
//       `void pRotateAxis(inout float2 p, float a)` and it is CALLED WITH SWIZZLE ARGS (p.zy / p.zx /
//       p.yx). MSL cannot bind a swizzle to a non-const `thread float2&` (the BendField inout->thread&
//       fork would FAIL to compile on these swizzle call sites). So the helper is ported BY-VALUE +
//       RETURN and each call is a swizzle ASSIGNMENT — this reproduces HLSL inout copy-in/copy-out
//       byte-identically (cos/sin reorder is deterministic). The body math
//       `p = cos(a)*p + sin(a) * float2(p.y, -p.x)` is identical text. pRotateAxis calls only cos/sin
//       built-ins (NO inter-helper call) -> this is the SOLE globals key -> NO MSL forward-decl prototype.
//   (3) DEG->RAD host fork — TiXL's Update() pre-multiplies Rotation(deg) * MathUtils.ToRad into the
//       AdditionalParameters value, so the shader's RotateRad is in RADIANS. sw has no per-frame Update;
//       the configureRotateField seam reproduces this by taking the user's DEGREES and converting to
//       radians (deg * PI/180) before packing. The shader-side token name + math stay byte-verbatim;
//       only the host conversion lives in the seam. (RotateRad is STORED as radians — documented fork.)
//   (4) RotateRad = Vector3 packed via appendVec3Param (packed_float3, 16B-align rule) — same path
//       BoxSDF's Center / Translate's Translation use. The sole [GraphParam] -> floats[0..2].
//   Test-only seam: configureRotateField sets the REAL Rotation (deg->rad) AND an injectBug that corrupts
//   the OP'S REAL preShaderCode emit (drop the z rotation line / swap an axis), so the golden's rotated
//   probe bites the op's emit, not an expected-value tautology. Production default injectBug=0.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// pRotateAxis helper — byte-verbatim body from RotateField.cs:41-43, ported BY-VALUE + RETURN (the
// ★CUT-94 swizzle fix): TiXL's `void pRotateAxis(inout float2 p, float a){ p = ...; }` becomes
// `float2 pRotateAxis(float2 p, float a){ p = ...; return p; }` so the swizzle-arg call sites (p.zy /
// p.zx / p.yx) can be written as swizzle ASSIGNMENTS (MSL rejects binding a swizzle to thread float2&).
// The math `cos(a)*p + sin(a) * float2(p.y, -p.x)` is identical text. No inter-helper call (cos/sin only)
// -> no forward-decl prototype needed. TiXL's globals key is "pRotateXYZ" but we register under the
// SHARED key "pRotateAxis" (see addGlobals: the Cut-95 mixed-graph de-dup fix). The three block comments
// are TiXL's verbatim doc lines (RotateField.cs:38-40), kept for byte parity of the globals body.
static const char* kBodyRotateAxis =
    "// Rotate around a coordinate axis (i.e. in a plane perpendicular to that axis) by angle <a>.\n"
    "// Read like this: R(p.xz, a) rotates \"x towards z\".\n"
    "// This is fast if <a> is a compile-time constant and slower (but still practical) if not.\n"
    "float2 pRotateAxis(float2 p, float a) {\n"
    " p = cos(a)*p + sin(a) * float2(p.y, -p.x);\n"
    " return p;\n"
    "}";

// ---- RotateField codegen node (a FieldNode subclass; single-input modifier — PRE-wrap path) ---------

struct RotateFieldNode : FieldNode {
  // RotateField.t3 default Rotation = (0,0,0) -> RotateRad = (0,0,0) -> identity rotation. STORED in
  // RADIANS (the host deg->rad fork happens in configureRotateField). Packed [GraphParam] (RotateRad).
  float rx = 0.f, ry = 0.f, rz = 0.f;
  // test-only bug modes (configureRotateField): 0 = none, 1 = drop the z rotation line (p.yx), 2 = swap
  // the z line's axis swizzle to p.xy (wrong axis) -> the rotated probe goes RED.
  int injectBug = 0;

  explicit RotateFieldNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "RotateField_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // RotateField.cs:37-44 registers the pRotateAxis helper under TiXL's key "pRotateXYZ". SHARED-KEY
    // MERGE FORK (Cut-95): RotateAxis also emits a helper FUNCTION named pRotateAxis (its .cs key is
    // "pRotateAxis"). The field codegen de-dups globals by std::map KEY and concatenates bodies with NO
    // function-name de-dup (field_graph.cpp:213-216) -> if these two ops used different keys, a graph
    // with BOTH would emit pRotateAxis TWICE = MSL redefinition compile error. Both .cs bodies are
    // BYTE-IDENTICAL, so we register under the SAME key "pRotateAxis" (matching RotateAxis): de-dup
    // collapses the mixed graph to ONE definition. The KEY is a codegen-internal de-dup identity, NEVER
    // emitted into the shader, so RotateField's ISOLATED emitted shader text stays byte-identical to TiXL
    // (same fn body + same call sites). pRotateAxis calls only cos/sin -> no secondary helper/forward-decl.
    c.globals["pRotateAxis"] = kBodyRotateAxis;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY RotateField.cs:49-53 GetPreShaderCode — three sequential axis rotations. {c} = context id
    // (root ""); {ShaderNode}RotateRad -> P.<prefix>RotateRad (fork (1)). Each call is a swizzle
    // ASSIGNMENT (fork (2) by-value+return). Emitted BEFORE the child recursion so the child samples the
    // rotated point -> the wrapped shape appears rotated.
    const std::string ctx = c.ctx();
    const std::string p = "p" + ctx;
    const std::string rr = "P." + prefix + "RotateRad";
    // Line 1: pRotateAxis(p.zy, RotateRad.x)
    c.appendCall(p + ".zy = pRotateAxis(" + p + ".zy, " + rr + ".x);");
    // Line 2: pRotateAxis(p.zx, RotateRad.y)
    c.appendCall(p + ".zx = pRotateAxis(" + p + ".zx, " + rr + ".y);");
    // Line 3: pRotateAxis(p.yx, RotateRad.z) — the golden's z rotation lands the off-center child on the
    // rotated probe. injectBug mutates THIS real line so the tooth bites the op's emit.
    if (injectBug == 1) return;  // drop the z rotation line -> no z rotation -> rotated probe RED.
    const char* swiz = (injectBug == 2) ? ".xy" : ".yx";  // wrong axis swizzle -> rotated probe RED.
    c.appendCall(p + swiz + " = pRotateAxis(" + p + swiz + ", " + rr + ".z);");
  }

  // Modifier: no post code (TiXL RotateField has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY RotateRad is a [GraphParam] (the AdditionalParameters float3). packed_float3 via the frozen
    // helper (appendVec3Param owns the 16B-align padding). Sole param -> floats[0..2]. Stored in RADIANS.
    appendVec3Param(floatParams, paramFields, prefix + "RotateRad", rx, ry, rz);
  }
};

NodeSpec rotateFieldSpec() {
  NodeSpec s;
  s.type = "RotateField";
  s.title = "Rotate Field";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Rotation = Vec3 head run (.x/.y/.z) in DEGREES (TiXL InputSlot<Vector3> Rotation; the host converts
  // to radians before packing). Same Widget::Vec/vecArity shape BoxSDF's Center / Translate's
  // Translation use (a 3-float vec drawn as one widget). .t3 default (0,0,0) -> identity.
  PortSpec rx; rx.id = "Rotation.x"; rx.name = "Rotation"; rx.dataType = "Float"; rx.isInput = true;
  rx.def = 0.0f; rx.minV = -360.0f; rx.maxV = 360.0f; rx.widget = Widget::Vec; rx.vecArity = 3;
  PortSpec ry; ry.id = "Rotation.y"; ry.name = "Rotation.y"; ry.dataType = "Float"; ry.isInput = true;
  ry.def = 0.0f; ry.minV = -360.0f; ry.maxV = 360.0f;
  PortSpec rz; rz.id = "Rotation.z"; rz.name = "Rotation.z"; rz.dataType = "Float"; rz.isInput = true;
  rz.def = 0.0f; rz.minV = -360.0f; rz.maxV = 360.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, rx, ry, rz, out};
  return s;
}

std::shared_ptr<FieldNode> makeRotateField(const std::string& shortId) {
  return std::make_shared<RotateFieldNode>(shortId);
}

const FieldOp g_rotateFieldOp(rotateFieldSpec(), makeRotateField);

}  // namespace

// Param-cook + test seam (mirrors configureTranslate). Takes the user's Rotation in DEGREES and converts
// to RADIANS (deg * PI/180 == MathUtils.ToRad — the TiXL host fork (3)) before packing, plus a test-only
// injectBug: 0 none / 1 drop-z-line / 2 wrong-axis. The leaf type is TU-private; this downcasts inside
// the owning TU. Production passes injectBug=0. No-op if `node` is not a RotateFieldNode (defensive).
void configureRotateField(FieldNode& node, float degX, float degY, float degZ, int injectBug) {
  if (auto* n = dynamic_cast<RotateFieldNode*>(&node)) {
    constexpr float kToRad = 3.14159265358979323846f / 180.0f;  // MathUtils.ToRad = PI/180.
    n->rx = degX * kToRad;
    n->ry = degY * kToRad;
    n->rz = degZ * kToRad;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
