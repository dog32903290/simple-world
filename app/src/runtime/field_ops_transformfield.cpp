// TransformField — single-input field MODIFIER (PRE + POST wrap, space family): transforms the
// SAMPLING POINT by an inverse object-to-world matrix BEFORE the child evaluates (moving/scaling/rotating
// the shape), then rescales the returned distance by UniformScale AFTER. It drives BOTH halves of the
// field_graph single-input wrap branch (field_graph.cpp:89-93): pre BEFORE the child recurse, post AFTER.
//
// TiXL authority: external/tixl/Operators/Lib/field/space/TransformField.cs
//   GetPreShaderCode (TransformField.cs:77-83):
//     c.AppendCall($"p{c}.xyz = mul(float4(p{c}.xyz,1), {ShaderNode}Transform).xyz;");
//   GetPostShaderCode (TransformField.cs:85-95):
//     if (_rotateFieldVecs) c.AppendCall($"f{c}.xyz = mul({ShaderNode}Transform, float4(f{c}.xyz,0)).xyz;");
//     c.AppendCall($"f{c}.w *= {ShaderNode}UniformScale; ");
//   ShaderNode.AdditionalParameters (TransformField.cs:24-27) — NOT [GraphParam]:
//     Parameter("float4x4", "Transform", Matrix4x4.Identity), Parameter("float", "UniformScale", 1).
//   The C# Update (TransformField.cs:31-73) composes Translation/Rotation/Scale/Shear/Pivot into one
//   matrix, TRANSPOSES it (HLSL row-major cbuffer), INVERTS it, and writes the INVERTED matrix into
//   AdditionalParameters[0]; UniformScale into [1]. The SHADER sees only those two values — so the leaf's
//   codegen + packing is exactly Transform(float4x4) + UniformScale(float). The authoring knobs
//   (Translation/Rotation/Scale/...) live on the C# host side and are PARITY-DEFERRED here (sw has no
//   matrix-composition cook yet); the leaf exposes Transform's 16 elements + UniformScale directly, which
//   is what the GPU consumes. A future cook can compose the knobs into the matrix.
//
// Branch: SINGLE-INPUT PRE+POST wrap (pre before child; post after). Same shape as RotateField/BendField.
//
// Forks vs TransformField.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes `{ShaderNode}Transform` / `{ShaderNode}UniformScale`, where
//       {ShaderNode}=BuildNodeId "<TypeName>_<shortGuid>_". sw's frozen convention (combinesdf.cpp:288)
//       reproduces it: P.TransformField_<id>_Transform / P.TransformField_<id>_UniformScale.
//   (2) ★mul(rowVec, M) -> MSL `M * rowVec` (load-bearing, derived + GPU-pinned by the golden). TiXL's
//       HLSL `mul(float4(p.xyz,1), Transform)` is ROW-vector × ROW-major-cbuffer matrix. The SAME 16
//       floats uploaded into an MSL `float4x4` STRUCT MEMBER fill it COLUMN-major, i.e. MSL's matrix is
//       the transpose of HLSL's by index meaning; consequently MSL `Transform * float4(p.xyz,1)` (matrix
//       × column-vector) computes the IDENTICAL result as HLSL `mul(v, Transform)` for the same uploaded
//       bytes. (Derivation: (M_msl*v)[i] = Σ_j M_hlsl[j][i]·v[j] = HLSL mul(v,M_hlsl)[i].) This is the
//       SAME class of fork as BendField/TwistField's `mul(m, p.xy)` -> `m * p.xy`, only with the operand
//       order flipped because TiXL put the vector FIRST. The golden RENDERS a translate+scale matrix and
//       byte-checks, so the orientation is proven on hardware, not assumed.
//   (3) AdditionalParameters (NOT [GraphParam]) packed via collectParams — the param-collection MECHANISM
//       is identical (16 floats for the matrix + 1 scalar into the one shared buffer; typed struct
//       members). The matrix uses the new appendMat4Param helper (a 64-byte float4x4 member); UniformScale
//       follows as a scalar. Declaration order = AdditionalParameters order (Transform then UniformScale).
//   (4) RotateFieldVecs = compile-time selector (TiXL InputSlot<bool> with FlagCodeChanged, .t3 default
//       false). Default OFF -> the post f.xyz-rotate line is NOT emitted; only `f.w *= UniformScale`. A
//       `rotateFieldVecs` bool member carries it (NOT packed). When ON the rotate line is emitted with the
//       SAME mul-orientation fork: TiXL `mul(Transform, float4(f.xyz,0))` is matrix × COLUMN-vector
//       already (vector SECOND) -> MSL keeps `Transform * float4(f.xyz,0)` (no flip; matrix*vec is the
//       same in both). Both branches ship faithfully.
//   Test-only seam: configureTransformField sets the REAL Transform/UniformScale/rotateFieldVecs AND an
//       injectBug (drop the pre transform / drop the post scale) so the golden's tooth bites the op's REAL
//       emit. Production default off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendMat4Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- TransformField codegen node (a FieldNode subclass; single-input modifier — PRE+POST wrap) -----

struct TransformFieldNode : FieldNode {
  // Transform = float4x4 AdditionalParameter, .t3 default Identity (the C# host inverts+transposes the
  // composed matrix; the GPU sees the result directly). Stored as 16 floats in the order MSL's float4x4
  // STRUCT MEMBER reads them (column-major bytes — see fork (2)). Default = identity.
  float transform[16] = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
                         0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
  float uniformScale = 1.f;     // AdditionalParameter, .t3 default 1.0.
  bool rotateFieldVecs = false; // .t3 default false. Compile-time selector (NOT packed) — fork (4).
  // test-only bug modes (configureTransformField): 0 = none, 1 = drop the pre transform (no point xform),
  // 2 = drop the post scale (UniformScale not applied). Both corrupt the OP's REAL emit, not the golden.
  int injectBug = 0;

  explicit TransformFieldNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "TransformField_" + shortId + "_";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY TransformField.cs:77-83. {c}=context id; {ShaderNode}Transform -> P.<prefix>Transform
    // (fork (1)); mul(v, M) -> M * v (fork (2)). Emitted BEFORE the child recursion so the child samples
    // the transformed point -> the shape moves/scales/rotates by the inverse object-to-world matrix.
    if (injectBug == 1) return;  // drop the pre transform -> point not transformed -> probe RED.
    const std::string ctx = c.ctx();
    c.appendCall("p" + ctx + ".xyz = (P." + prefix + "Transform * float4(p" + ctx + ".xyz, 1)).xyz;");
  }

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY TransformField.cs:85-95. RotateFieldVecs OFF (default) -> only the distance rescale; ON ->
    // also rotate the carried f.xyz by the matrix (vector SECOND in TiXL -> no orientation flip needed).
    const std::string ctx = c.ctx();
    if (rotateFieldVecs) {
      c.appendCall("f" + ctx + ".xyz = (P." + prefix + "Transform * float4(f" + ctx + ".xyz, 0)).xyz;");
    }
    if (injectBug == 2) return;  // drop the post scale -> f.w not rescaled -> probe RED.
    c.appendCall("f" + ctx + ".w *= P." + prefix + "UniformScale;");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // AdditionalParameters order (TransformField.cs:24-27): Transform (float4x4) THEN UniformScale
    // (float). rotateFieldVecs is a compile-time selector (NOT packed). Matrix=16 floats + scalar=1.
    appendMat4Param(floatParams, paramFields, prefix + "Transform", transform);
    appendScalarParam(floatParams, paramFields, prefix + "UniformScale", uniformScale);
  }
};

NodeSpec transformFieldSpec() {
  NodeSpec s;
  s.type = "TransformField";
  s.title = "Transform Field";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // UniformScale = AdditionalParameter exposed as a Float knob, .t3 default 1.0. (The matrix is composed
  // on the host from Translation/Rotation/Scale/Shear/Pivot — those authoring knobs are parity-deferred;
  // the GPU param the shader reads is the matrix + UniformScale.)
  PortSpec us; us.id = "UniformScale"; us.name = "Uniform Scale"; us.dataType = "Float"; us.isInput = true;
  us.def = 1.0f; us.minV = 0.0f; us.maxV = 10.0f;
  // RotateFieldVecs = bool code selector (drawn as a 2-value Enum Off/On), .t3 default 0 (Off). NOT packed.
  PortSpec rv; rv.id = "RotateFieldVecs"; rv.name = "Rotate Field Vecs"; rv.dataType = "Float";
  rv.isInput = true; rv.def = 0.0f; rv.minV = 0.0f; rv.maxV = 1.0f; rv.widget = Widget::Enum;
  rv.labels = {"Off", "On"};
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, us, rv, out};
  return s;
}

std::shared_ptr<FieldNode> makeTransformField(const std::string& shortId) {
  return std::make_shared<TransformFieldNode>(shortId);
}

// PF-0d DEFERRED: no param-apply configurer (the 2-arg ctor registers a null configurer). TransformField's
// Transform is a float4x4 (16 floats) that cannot flow through the map<string,float> float spine — its
// param-apply lands in PF-0d, not PF-0c. NULL configurer = explicit no-op (node keeps ctor .t3 defaults).
const FieldOp g_transformFieldOp(transformFieldSpec(), makeTransformField);

}  // namespace

// Param-cook + test seam: set the Transform matrix (16 floats, MSL float4x4 column-major byte order),
// UniformScale, rotateFieldVecs (and a test-only injectBug: 0 none / 1 drop-pre-transform /
// 2 drop-post-scale) on a makeFieldNode("TransformField",...) node. The leaf type is TU-private; this
// downcasts inside the owning TU. Production passes injectBug=0.
void configureTransformField(FieldNode& node, const float transform[16], float uniformScale,
                             bool rotateFieldVecs, int injectBug) {
  if (auto* n = dynamic_cast<TransformFieldNode*>(&node)) {
    for (int i = 0; i < 16; ++i) n->transform[i] = transform[i];
    n->uniformScale = uniformScale;
    n->rotateFieldVecs = rotateFieldVecs;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
