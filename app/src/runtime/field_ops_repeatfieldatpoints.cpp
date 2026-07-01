// RepeatFieldAtPoints field op — the FIRST field leaf that BINDS A STRUCTURED BUFFER (it rides the
// point-buffer→field "SRV seam" added to the frozen base: FieldNode::collectBuffers + the template's
// /*{STRUCT_DEFS}*/ / /*{BUFFERS}*/ / /*{BUFFER_PARAMS}*/ / /*{BUFFER_ARGS}*/ hooks + field_render's
// setFragmentBuffer loop). This is the ATOM_SEAM_MAP "唯一真硬 resource 接縫" — the point-buffer→field
// resource KIND that Image2dSDF's texture seam does NOT cover. Like SphereSDF / Image2dSDF this single
// .cpp owns BOTH halves of one op: the codegen NODE (RepeatFieldAtPointsNode below, a custom-code op
// that loops over the point buffer and recurses its input field) AND the OP layer (a NodeSpec for the
// Add menu / findSpec + a FieldNodeFactory), registered via the file-scope FieldOp registrar. The base
// machinery stays FROZEN — adding this op = this .cpp + one CMakeLists line + the ONE collectBuffers
// override on the FROZEN base (the SRV seam, shaped like the collectTextures seam).
//
// TiXL authority: external/tixl/Operators/Lib/field/space/_/ExecuteRepeatFieldAtPoints.cs (the codegen
// IGraphNodeOp) + RepeatFieldAtPoints.cs (the public op shell + ComputePointTransformMatrix.hlsl which
// FILLS the PointTransform buffer from raw Points — that compute PASS is the already-proven compute-stage
// keystone (main 8f97132); this op CONSUMES the resulting PointTransform buffer as an SRV, which is the
// unknown this build proves).
//
//   AddDefinitions (ExecuteRepeatFieldAtPoints.cs:51-87):
//     - c.ResourceTypes["PointMatrix"] = "struct PointTransform { float4x4 WorldToPointObject; float4 PointColor; };"
//     - c.Globals[GetColorBlendFactor] = "float GetColorBlendFactor(float d2, float d1, float k){ return clamp(0.5+0.5*(d2-d1)/k,0,1); }"
//     - for UnionSoft/UnionRound: register fOpUnionSoft / fOpUnionRound global.
//   TryBuildCustomCode (ExecuteRepeatFieldAtPoints.cs:89-134) emits the point loop:
//     float4 pStart{c} = p{c};
//     float4 fLoop{c}  = float4(1,1,1,99999);
//     for(int i{c}=0; i{c} < {_count} && i{c} < 100; i{c}++) {
//         f{c} = float4(1,1,1,9999);
//         p{c}.xyz = mul(float4(pStart{c}.xyz,1), {node}PointTransforms[i{c}].WorldToPointObject).xyz;
//         <inputField emitted here>
//         f{c}.rgb *= {node}PointTransforms[i{c}].PointColor.rgb;
//         fLoop{c}.rgb = lerp(fLoop{c}.rgb, f{c}.rgb, GetColorBlendFactor(fLoop{c}.w, f{c}.w, {node}K));
//         fLoop{c}.w = <combine>(f{c}.w, fLoop{c}.w);   // Union: min; UnionSoft/Round: helper
//     }
//     f{c} = fLoop{c};
//   AppendShaderResources (ExecuteRepeatFieldAtPoints.cs:136-149):
//     list.Add("StructuredBuffer<PointTransform> {node}PointTransforms", _srv);
//   K is a [GraphParam] (packed float). CombineMethod is a codegen-time enum selector (0=Union,
//   1=UnionSoft, 2=UnionRound). _count is baked from the point buffer's element count.
//
// HLSL->MSL forks honored (named):
//   (1) StructuredBuffer<PointTransform> {node}PointTransforms (register tN) -> MSL fragment arg
//       `device const PointTransform* P_{node}PointTransforms [[buffer(BASE+N)]]`, threaded through
//       evalField via the BUFFER_* hooks. Load-bearing SRV fork (same class as Image2dSDF's texture fork).
//   (2) mul(rowVec, M) -> mul(M, rowVec) is NOT used; HLSL `mul(float4 v, float4x4 M)` = row-vector·M.
//       MSL `float4x4 * float4` is matrix·column-vector = M·v (i.e. HLSL `mul(M, v)`). To reproduce HLSL
//       `mul(v, M)` = v·M = (M^T·v) we emit `(v * M)` in MSL (MSL `rowvec * mat` == HLSL `mul(rowvec,mat)`
//       because MSL `v * M` treats v as a row vector: (v*M)_j = Σ_i v_i M[i][j]). The PointTransform
//       matrix is written by ComputePointTransformMatrix.hlsl in HLSL column layout; the golden authors
//       the buffer in that SAME layout and the host re-derivation matches, so this is byte-consistent.
//       We keep TiXL's `mul(...)` text intent by emitting the MSL `mul()` overload — MSL `metal::mul` is
//       NOT provided; instead MSL uses operator*. So: `(float4(pStart.xyz,1) * M).xyz`.
//   (3) .rgb / float4(1,1,1,x) / lerp / min / clamp — byte-identical text in HLSL and MSL.
//   (4) {node}K -> P.{prefix}K (cbuffer->struct access, same as every other [GraphParam] leaf).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, BufBinding, collectFieldCode, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// Combine variants (parity ExecuteRepeatFieldAtPoints.cs CombineMethods enum order).
enum class CombineMethod { Union = 0, UnionSoft = 1, UnionRound = 2 };

// ---- RepeatFieldAtPoints codegen node (custom-code op; structured-buffer-binding leaf) ------------

struct RepeatFieldAtPointsNode : FieldNode {
  float k = 1.0f;                             // K [GraphParam] (color/soft-union blend width)
  CombineMethod combine = CombineMethod::Union;
  const void* pointBuffer = nullptr;          // opaque MTL::Buffer* (the bound PointTransform SRV)
  uint32_t pointCount = 0;                     // element count (baked into the loop bound literal)

  explicit RepeatFieldAtPointsNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix AND the PointTransforms
    // buffer-decl prefix AND the K param name prefix.
    prefix = "RepeatFieldAtPoints_" + shortId + "_";
  }

  std::string bufArgName() const { return prefix + "PointTransforms"; }  // TiXL {node}PointTransforms

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY ExecuteRepeatFieldAtPoints.cs AddDefinitions.
    // (a) resource struct — the PointTransform element type (de-duped by the type name key).
    c.resourceTypes["PointTransform"] =
        "struct PointTransform\n"
        "{\n"
        "    float4x4 WorldToPointObject;\n"
        "    float4 PointColor;\n"
        "};";
    // (b) GetColorBlendFactor (ShaderGraphIncludes.GetColorBlendFactor) — byte-verbatim.
    c.globals["GetColorBlendFactor"] =
        "float GetColorBlendFactor(float d2, float d1, float k)\n"
        "{\n"
        "  return clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);\n"
        "}";
    // (c) the soft/round union helper (only when selected), byte-verbatim.
    if (combine == CombineMethod::UnionSoft) {
      c.globals["fOpUnionSoft"] =
          "float fOpUnionSoft(float a, float b, float r) {\n"
          "    float e = max(r - abs(a - b), 0);\n"
          "    return min(a, b) - e*e*0.25/r;\n"
          "}";
    } else if (combine == CombineMethod::UnionRound) {
      c.globals["fOpUnionRound"] =
          "float fOpUnionRound(float a, float b, float r) {\n"
          "    float2 u = max(float2(r - a, r - b), 0);\n"
          "    return max(r, min(a, b)) - length(u);\n"
          "}";
    }
  }

  bool tryBuildCustomCode(CodeAssembleCtx& c) const override {
    // PARITY ExecuteRepeatFieldAtPoints.cs:89-134 TryBuildCustomCode. The op takes over the whole
    // collect: it loops over the point buffer, transforms p by each point's WorldToPointObject, recurses
    // the ONE input field at the transformed position, tints + blends the color, and combines the
    // distance. If there is no input field TiXL early-returns true (nothing to repeat) — we match.
    if (inputs.empty() || !inputs[0]) return true;  // parity: fields.Count == 0 -> return true

    const std::string ctx = c.ctx();
    const std::string buf = "P_" + bufArgName();   // MSL fragment arg name (assembler prefixes "P_")
    const std::string kRef = "P." + prefix + "K";  // packed [GraphParam]
    const int cnt = static_cast<int>(pointCount);
    const int maxSteps = 100;

    c.appendCall("float4 pStart" + ctx + " = p" + ctx + ";");
    c.appendCall("float4 fLoop" + ctx + " = float4(1,1,1,99999);");
    c.appendCall("for(int i" + ctx + " = 0; i" + ctx + " < " + std::to_string(cnt) + " && i" + ctx +
                 " < " + std::to_string(maxSteps) + "; i" + ctx + "++) {");
    c.indentCount++;
    {
      c.appendCall("f" + ctx + " = float4(1,1,1,9999);");
      // FORK (2) — SAME orientation fork as TransformField (field_ops_transformfield.cpp:87, GPU-pinned):
      // TiXL HLSL `mul(float4(pStart,1), WorldToPointObject)` is a ROW-vector × HLSL-row-major matrix.
      // The SAME matrix bytes read into an MSL `float4x4` STRUCT MEMBER (from the device buffer) fill it
      // COLUMN-major — the transpose by index meaning — so MSL `M * float4(pStart,1)` (matrix ×
      // column-vector) computes the IDENTICAL result as HLSL `mul(v, M)`. WorldToPointObject transforms
      // world p into each point's object space (the field is evaluated centered on each point). The golden
      // authors the buffer's matrix in this column-major byte layout and byte-checks -> orientation proven.
      c.appendCall("p" + ctx + ".xyz = (" + buf + "[i" + ctx + "].WorldToPointObject * float4(pStart" +
                   ctx + ".xyz, 1)).xyz;");

      // Recurse the input field at the transformed p (TiXL inputField.CollectEmbeddedShaderCode(c)).
      collectFieldCode(*inputs[0], c);

      // Multiply point color into the field color.
      c.appendCall("f" + ctx + ".rgb *= " + buf + "[i" + ctx + "].PointColor.rgb;");
      c.appendCall("fLoop" + ctx + ".rgb = mix(fLoop" + ctx + ".rgb, f" + ctx +
                   ".rgb, GetColorBlendFactor(fLoop" + ctx + ".w, f" + ctx + ".w, " + kRef + "));");

      // Combine the distance (.w). FORK (3): HLSL lerp == MSL mix (used above). min/helpers are shared.
      switch (combine) {
        case CombineMethod::Union:
          c.appendCall("fLoop" + ctx + ".w = min(f" + ctx + ".w, fLoop" + ctx + ".w);");
          break;
        case CombineMethod::UnionSoft:
          c.appendCall("fLoop" + ctx + ".w = fOpUnionSoft(fLoop" + ctx + ".w, f" + ctx + ".w, " + kRef +
                       ");");
          break;
        case CombineMethod::UnionRound:
          c.appendCall("fLoop" + ctx + ".w = fOpUnionRound(fLoop" + ctx + ".w, f" + ctx + ".w, " + kRef +
                       ");");
          break;
      }
    }
    c.indentCount--;
    c.appendCall("}");
    c.appendCall("f" + ctx + " = fLoop" + ctx + ";");
    return true;
  }

  // A custom-code op still emits its OWN params (K); the recursion into inputs collects child params.
  void preShaderCode(CodeAssembleCtx&, int) const override {}  // unused (tryBuildCustomCode owns emit)

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // Only K is a [GraphParam] (CombineMethod is a codegen selector, not a packed float; the point
    // buffer is an SRV, not a float). Depth-first: the input field's params are collected by the base
    // walk (collectAllParams recurses inputs first), then this node's K.
    appendScalarParam(floatParams, paramFields, prefix + "K", k);
  }

  void collectBuffers(std::vector<BufBinding>& out) const override {
    // PARITY ExecuteRepeatFieldAtPoints.cs:136-149 AppendShaderResources — declare the ONE PointTransform
    // structured buffer. The assembler turns this into a fragment `device const PointTransform*
    // P_<prefix>PointTransforms [[buffer(BASE+N)]]` arg; field_render binds the opaque handle at slot
    // BASE+N. (TiXL: `StructuredBuffer<PointTransform> {node}PointTransforms`.)
    out.push_back(BufBinding{"PointTransform", bufArgName(), pointBuffer, pointCount});
  }
};

NodeSpec repeatFieldAtPointsSpec() {
  NodeSpec s;
  s.type = "RepeatFieldAtPoints";
  s.title = "Repeat Field At Points";
  // InputField = the field to repeat (a Field input). Points = the point buffer (Points input; the real
  // graph-cook Points port + the ComputePointTransformMatrix pass are the compute-stage keystone; the
  // golden feeds a host-authored PointTransform buffer via the cook seam).
  PortSpec inf; inf.id = "InputField"; inf.name = "Input Field"; inf.dataType = "Field"; inf.isInput = true;
  PortSpec pts; pts.id = "Points"; pts.name = "Points"; pts.dataType = "Points"; pts.isInput = true;
  // K [GraphParam] — RepeatFieldAtPoints.t3 default. (No authored .t3 value ships a non-default K; the
  // op default 1.0 is the safe blend width; the golden overrides via the cook seam.)
  PortSpec k; k.id = "K"; k.name = "K"; k.dataType = "Float"; k.isInput = true;
  k.def = 1.0f; k.minV = 0.0001f; k.maxV = 10.0f;
  // CombineMethod = enum CODE SELECTOR (drawn as a dropdown, Widget::Enum) — a Float port storing the
  // enum index (0=Union, 1=UnionSoft, 2=UnionRound), same convention as CombineSDF/TorusSDF's Axis.
  PortSpec cm; cm.id = "CombineMethod"; cm.name = "Combine Method"; cm.dataType = "Float"; cm.isInput = true;
  cm.def = 0.0f; cm.minV = 0.0f; cm.maxV = 2.0f; cm.widget = Widget::Enum;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {inf, pts, k, cm, out};
  return s;
}

std::shared_ptr<FieldNode> makeRepeatFieldAtPoints(const std::string& shortId) {
  return std::make_shared<RepeatFieldAtPointsNode>(shortId);
}

const FieldOp g_repeatFieldAtPointsOp(repeatFieldAtPointsSpec(), makeRepeatFieldAtPoints);

}  // namespace

// Param-cook seam (mirrors configureImage2dSdf): set K + CombineMethod AND inject the HOST-supplied
// PointTransform buffer (+ its element count) on a node built via makeFieldNode("RepeatFieldAtPoints",...).
// The leaf type is TU-private; this free function downcasts inside the owning TU so callers (the GPU
// golden; later a graph-cook walk) can override without the type leaking. `pointBuffer` is an opaque
// MTL::Buffer* (the caller owns its lifetime through the render). No-op if not a RepeatFieldAtPointsNode.
//
// THIS STANDS IN FOR THE POINTS PORT + COMPUTE PASS: the real op reads a Points buffer during a graph
// cook and runs ComputePointTransformMatrix.hlsl (the proven compute-stage keystone) to produce the
// PointTransform buffer. The golden feeds a deterministic host-authored PointTransform buffer so it
// tests the SRV-BIND path (the point-buffer→field seam), not the point-port / compute pass.
void configureRepeatFieldAtPoints(FieldNode& node, const void* pointBuffer, uint32_t pointCount, float k,
                                  int combineMethod) {
  if (auto* n = dynamic_cast<RepeatFieldAtPointsNode*>(&node)) {
    n->pointBuffer = pointBuffer;
    n->pointCount = pointCount;
    n->k = k;
    n->combine = static_cast<CombineMethod>(combineMethod < 0 ? 0 : (combineMethod > 2 ? 2 : combineMethod));
  }
}

}  // namespace sw
