// NoiseDisplaceSDF — single-input field MODIFIER (PRE+POST wrap): adds a 3D simplex-noise offset to the
// wrapped field's DISTANCE (f.w), then scales by StepFactor. Drives BOTH halves of the field_graph
// single-input wrap branch (field_graph.cpp:82-86): in the default (non-local-space) mode preShaderCode
// SNAPSHOTS the pre-child sample point into a per-node local `_t` (BEFORE the child can transform p), and
// postShaderCode reads that snapshot to displace the child's distance (AFTER). In local-space mode the
// pre line is dropped and the post reads p{c} directly. Like BendField it is the PRE+POST shape; the
// noise body is a verbatim-ported simplex-noise helper.
//
// TiXL authority: external/tixl/Operators/Lib/field/adjust/NoiseDisplaceSDF.cs
//   AddDefinitions (NoiseDisplaceSDF.cs:37-129): c.Globals["fSimplexNoiseDisplace"] = a single multi-fn
//     block: _noiseOffset_mod289 (3 overloads) / _noiseOffset_permute / _noiseOffset_taylorInvSqrt /
//     _noiseOffset_simplexNoise3D / fSimplexNoiseDisplace(pos, amount, scale, offset) =
//     _noiseOffset_simplexNoise3D(pos/scale + offset) * amount. (Distinct helper NAMES from
//     SpatialDisplaceSDF's block — the `_noiseOffset_` prefix — so the two ops' globals do NOT name-clash
//     even when they share the std::map KEY "fSimplexNoiseDisplace"; see fork (4).)
//   GetPreShaderCode (NoiseDisplaceSDF.cs:131-141): if (_useLocalSpace) return; else
//     c.AppendCall($"float3 {ShaderNode}_t = p{c}.xyz;"); (snapshot pre-child point).
//   GetPostShaderCode (NoiseDisplaceSDF.cs:144-163):
//     localSpace : f{c}.w += fSimplexNoiseDisplace(p{c}.xyz, {n}Amount, {n}Scale, -{n}Offset);
//                  f{c}.w *= {n}StepFactor;
//     else       : f{c}.w += fSimplexNoiseDisplace({n}_t.xyz, {n}Amount, {n}Scale, -{n}Offset);
//                  f{c}.w *= {n}StepFactor;
//   [GraphParam] order: Amount (float) / Scale (float) / Offset (Vector3) / StepFactor (float)
//     (NoiseDisplaceSDF.cs:168-182). InputSlot<bool> UseLocalSpace (NoiseDisplaceSDF.cs:184-185) drives
//     FlagCodeChanged (NoiseDisplaceSDF.cs:24-31) — a COMPILE-TIME selector, NOT packed.
//
// Branch: SINGLE-INPUT PRE+POST wrap. Same shape as BendField.
//
// Forks vs NoiseDisplaceSDF.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes `{ShaderNode}Amount` etc. ({ShaderNode}=BuildNodeId
//       "<TypeName>_<shortGuid>_"). sw's frozen convention (combinesdf.cpp:288 / translate.cpp:46)
//       reproduces P.NoiseDisplaceSDF_<id>_Amount / _Scale / _Offset / _StepFactor. Also drives the
//       per-node LOCAL var name {ShaderNode}_t -> `NoiseDisplaceSDF_<id>__t` (the prefix already ends in
//       "_", so the var is `<prefix>_t` — the second underscore is TiXL's literal `_t`). A wrong prefix
//       reads the wrong/0 member (or a wrong local) and the golden's displaced probe bites.
//   (2) HLSL->MSL — the noise body is float-math only (floor / step / dot / abs / max + float2/3/4 ctors)
//       which is identical text in MSL. NO inout, NO inter-global call (the block is self-contained: all
//       sub-helpers precede their callers WITHIN the one string), NO swizzle-by-ref -> no Cut-94 fix and
//       no forward-decl needed. The local snapshot is `float3 <prefix>_t = p{c}.xyz;` (a plain MSL local
//       declaration emitted via appendCall into the calls block) — legal MSL.
//   (3) Offset NEGATION — the call passes `-{ShaderNode}Offset` (a packed_float3 negated). MSL `-` on a
//       packed_float3 yields a float3; `fSimplexNoiseDisplace(..., float3)` matches the `float3 offset`
//       param -> legal. Kept verbatim (the negation is TiXL's, not a fork).
//   (4) SHARED-KEY note — both this op and SpatialDisplaceSDF register a global under the SAME std::map
//       KEY "fSimplexNoiseDisplace" with DIFFERENT bodies (this op's helpers are `_noiseOffset_`-prefixed;
//       SpatialDisplaceSDF's are bare mod289/permute/...). In a graph mixing BOTH, the map keeps ONE body
//       (last addGlobals wins). This MIRRORS TiXL's own behaviour (its CodeAssembleContext.Globals is a
//       Dictionary keyed identically -> same single-survivor semantics) — NOT a sw-introduced regression.
//       The two bodies are functionally equivalent (same simplex-noise math, renamed helpers), and each
//       op's ISOLATED shader is byte-faithful. NAMED so the orchestrator's mixed-graph compile knows the
//       collision is expected & benign (whichever body survives, the surviving fSimplexNoiseDisplace
//       signature is identical, so both ops' call sites still resolve).
//   (5) UseLocalSpace = compile-time bool selector member `useLocalSpace`, NOT packed (combinesdf.cpp:284
//       precedent). Default false -> the snapshot path (pre line emitted). The local var snapshot is the
//       point at which displacement is sampled in WORLD space (pre-child), vs LOCAL space (post p{c}).
//   Test-only seam: configureNoiseDisplaceSdf sets the REAL Amount/Scale/Offset/StepFactor/useLocalSpace
//       AND an injectBug that corrupts the OP'S REAL post emit (drop the `*= StepFactor` / drop the noise
//       add) so the golden's probes bite the op's real emit. Production default injectBug=0.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam, appendVec3Param
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// fSimplexNoiseDisplace global — byte-verbatim from NoiseDisplaceSDF.cs:39-128. Self-contained (every
// sub-helper is defined before its caller WITHIN this one string), float-math only -> no MSL fork beyond
// the implicit HLSL/MSL float-math identity. (The `_noiseOffset_` prefix distinguishes these helpers
// from SpatialDisplaceSDF's bare-named copy — see fork (4).)
static const char* kBodyFSimplexNoiseDisplace =
    "float _noiseOffset_mod289(float x) {\n"
    "    return x - floor(x * (1.0 / 289.0)) * 289.0;\n"
    "}\n"
    "\n"
    "float3 _noiseOffset_mod289(float3 x) {\n"
    "    return x - floor(x * (1.0 / 289.0)) * 289.0;\n"
    "}\n"
    "\n"
    "float4 _noiseOffset_mod289(float4 x) {\n"
    "    return x - floor(x * (1.0 / 289.0)) * 289.0;\n"
    "}\n"
    "\n"
    "float4 _noiseOffset_permute(float4 x) {\n"
    "    return _noiseOffset_mod289(((x * 34.0) + 1.0) * x);\n"
    "}\n"
    "\n"
    "float4 _noiseOffset_taylorInvSqrt(float4 r) {\n"
    "    return 1.79284291400159 - 0.85373472095314 * r;\n"
    "}\n"
    "\n"
    "float _noiseOffset_simplexNoise3D(float3 v) {\n"
    "    const float2  C = float2(1.0 / 6.0, 1.0 / 3.0);\n"
    "    const float4  D = float4(0.0, 0.5, 1.0, 2.0);\n"
    "\n"
    "    // First corner\n"
    "    float3 i  = floor(v + dot(v, C.yyy));\n"
    "    float3 x0 = v - i + dot(i, C.xxx);\n"
    "\n"
    "    // Other corners\n"
    "    float3 g = step(x0.yzx, x0.xyz);\n"
    "    float3 l = 1.0 - g;\n"
    "    float3 i1 = min(g.xyz, l.zxy);\n"
    "    float3 i2 = max(g.xyz, l.zxy);\n"
    "\n"
    "    float3 x1 = x0 - i1 + C.xxx;\n"
    "    float3 x2 = x0 - i2 + C.yyy;\n"
    "    float3 x3 = x0 - 0.5;\n"
    "\n"
    "    // Permutations\n"
    "    i = _noiseOffset_mod289(i);\n"
    "    float4 p = _noiseOffset_permute(_noiseOffset_permute(_noiseOffset_permute(\n"
    "                 i.z + float4(0.0, i1.z, i2.z, 1.0))\n"
    "               + i.y + float4(0.0, i1.y, i2.y, 1.0))\n"
    "               + i.x + float4(0.0, i1.x, i2.x, 1.0));\n"
    "\n"
    "    // Gradients\n"
    "    float4 j = p - 49.0 * floor(p * (1.0 / 49.0));  // mod(p,7*7)\n"
    "\n"
    "    float4 x_ = floor(j * (1.0 / 7.0));\n"
    "    float4 y_ = floor(j - 7.0 * x_);    // mod(j,7)\n"
    "\n"
    "    float4 x = (x_ * 2.0 + 0.5) / 7.0 - 1.0;\n"
    "    float4 y = (y_ * 2.0 + 0.5) / 7.0 - 1.0;\n"
    "\n"
    "    float4 h = 1.0 - abs(x) - abs(y);\n"
    "\n"
    "    float4 b0 = float4(x.xy, y.xy);\n"
    "    float4 b1 = float4(x.zw, y.zw);\n"
    "\n"
    "    float4 s0 = floor(b0) * 2.0 + 1.0;\n"
    "    float4 s1 = floor(b1) * 2.0 + 1.0;\n"
    "    float4 sh = -step(h, 0.0);\n"
    "\n"
    "    float4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;\n"
    "    float4 a1 = b1.xzyw + s1.xzyw * sh.zzww;\n"
    "\n"
    "    float3 g0 = float3(a0.xy, h.x);\n"
    "    float3 g1 = float3(a0.zw, h.y);\n"
    "    float3 g2 = float3(a1.xy, h.z);\n"
    "    float3 g3 = float3(a1.zw, h.w);\n"
    "\n"
    "    // Normalize gradients\n"
    "    float4 norm = _noiseOffset_taylorInvSqrt(float4(dot(g0,g0), dot(g1,g1), dot(g2,g2), dot(g3,g3)));\n"
    "    g0 *= norm.x;\n"
    "    g1 *= norm.y;\n"
    "    g2 *= norm.z;\n"
    "    g3 *= norm.w;\n"
    "\n"
    "    // Mix contributions\n"
    "    float4 m = max(0.6 - float4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);\n"
    "    m = m * m;\n"
    "    return 42.0 * dot(m * m, float4(dot(g0,x0), dot(g1,x1), dot(g2,x2), dot(g3,x3)));\n"
    "}\n"
    "\n"
    "float fSimplexNoiseDisplace(float3 pos, float amount, float scale, float3 offset) {\n"
    "    return _noiseOffset_simplexNoise3D(pos / scale + offset ) * amount;\n"
    "}";

// ---- NoiseDisplaceSDF codegen node (a FieldNode subclass; single-input modifier — PRE+POST wrap) ----

struct NoiseDisplaceSDFNode : FieldNode {
  float amount = 0.5f;     // .t3 default Amount = 0.5. Packed [GraphParam].
  float scale = 1.f;       // .t3 default Scale = 1.0. Packed [GraphParam].
  float ox = 0.f, oy = 0.f, oz = 0.f;  // .t3 default Offset = (0,0,0). Packed [GraphParam] (vec3).
  float stepFactor = 0.5f; // .t3 default StepFactor = 0.5. Packed [GraphParam].
  bool useLocalSpace = false;  // .t3 default false -> snapshot path. Compile-time selector, NOT packed.
  // test-only bug modes (configureNoiseDisplaceSdf): 0 = none, 1 = drop the `*= StepFactor` post line,
  // 2 = drop the noise add (no displacement). Both corrupt the OP'S REAL post emit.
  int injectBug = 0;

  explicit NoiseDisplaceSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix. prefix ends in "_", so
    // the {ShaderNode}_t local emits as `<prefix>_t` (the literal `_t` adds the second underscore).
    prefix = "NoiseDisplaceSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // NoiseDisplaceSDF.cs:37-129 — one always-on global (key matches TiXL nameof). See fork (4) for the
    // shared-key note vs SpatialDisplaceSDF.
    c.globals["fSimplexNoiseDisplace"] = kBodyFSimplexNoiseDisplace;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY NoiseDisplaceSDF.cs:131-141: in NON-local-space mode, snapshot the pre-child sample point
    // into a per-node local `{ShaderNode}_t`. In local-space mode, NO pre line (the post reads p{c}).
    if (useLocalSpace) return;
    const std::string ctx = c.ctx();
    c.appendCall("float3 " + prefix + "_t = p" + ctx + ".xyz;");
  }

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY NoiseDisplaceSDF.cs:144-163: add the simplex-noise displacement to f.w, then scale by
    // StepFactor. The noise SAMPLE point is p{c}.xyz (local-space mode) or the snapshot {ShaderNode}_t
    // (world-space mode). Offset is negated (fork (3)). {ShaderNode}<Name> -> P.<prefix><Name> (fork (1)).
    const std::string ctx = c.ctx();
    const std::string samplePoint = useLocalSpace ? ("p" + ctx + ".xyz") : (prefix + "_t.xyz");
    if (injectBug != 2) {  // injectBug==2 drops the noise add -> displaced probe reads the bare child d.
      c.appendCall("f" + ctx + ".w += fSimplexNoiseDisplace(" + samplePoint + ", P." + prefix +
                   "Amount, P." + prefix + "Scale, -P." + prefix + "Offset);");
    }
    if (injectBug == 1) return;  // drop the `*= StepFactor` -> StepFactor!=1 probe RED.
    c.appendCall("f" + ctx + ".w *= P." + prefix + "StepFactor;");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order (NoiseDisplaceSDF.cs:168-182): Amount / Scale / Offset
    // (vec3) / StepFactor. useLocalSpace is a compile-time selector (NOT packed). The vec3 padding rule
    // lands Offset correctly (appendVec3Param owns padForVec3).
    appendScalarParam(floatParams, paramFields, prefix + "Amount", amount);
    appendScalarParam(floatParams, paramFields, prefix + "Scale", scale);
    appendVec3Param(floatParams, paramFields, prefix + "Offset", ox, oy, oz);
    appendScalarParam(floatParams, paramFields, prefix + "StepFactor", stepFactor);
  }
};

NodeSpec noiseDisplaceSdfSpec() {
  NodeSpec s;
  s.type = "NoiseDisplaceSDF";
  s.title = "Noise Displace SDF";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Amount = displacement magnitude [GraphParam] float, .t3 default 0.5.
  PortSpec am; am.id = "Amount"; am.name = "Amount"; am.dataType = "Float"; am.isInput = true;
  am.def = 0.5f; am.minV = -10.0f; am.maxV = 10.0f;
  // Scale = noise spatial scale [GraphParam] float, .t3 default 1.0 (the noise pos is divided by it).
  PortSpec sc; sc.id = "Scale"; sc.name = "Scale"; sc.dataType = "Float"; sc.isInput = true;
  sc.def = 1.0f; sc.minV = 0.001f; sc.maxV = 10.0f;
  // Offset = Vec3 noise-domain offset [GraphParam], default (0,0,0).
  PortSpec ox; ox.id = "Offset.x"; ox.name = "Offset"; ox.dataType = "Float"; ox.isInput = true;
  ox.def = 0.0f; ox.minV = -10.0f; ox.maxV = 10.0f; ox.widget = Widget::Vec; ox.vecArity = 3;
  PortSpec oy; oy.id = "Offset.y"; oy.name = "Offset.y"; oy.dataType = "Float"; oy.isInput = true;
  oy.def = 0.0f; oy.minV = -10.0f; oy.maxV = 10.0f;
  PortSpec oz; oz.id = "Offset.z"; oz.name = "Offset.z"; oz.dataType = "Float"; oz.isInput = true;
  oz.def = 0.0f; oz.minV = -10.0f; oz.maxV = 10.0f;
  // StepFactor = distance scale after displacement [GraphParam] float, .t3 default 0.5.
  PortSpec sf; sf.id = "StepFactor"; sf.name = "Step Factor"; sf.dataType = "Float"; sf.isInput = true;
  sf.def = 0.5f; sf.minV = 0.0f; sf.maxV = 2.0f;
  // UseLocalSpace = bool code selector (Off/On Enum), .t3 default 0 (Off -> snapshot/world-space path).
  // NOT packed; the node's `useLocalSpace` bool carries it.
  PortSpec ls; ls.id = "UseLocalSpace"; ls.name = "Use Local Space"; ls.dataType = "Float";
  ls.isInput = true; ls.def = 0.0f; ls.minV = 0.0f; ls.maxV = 1.0f; ls.widget = Widget::Enum;
  ls.labels = {"Off", "On"};
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, am, sc, ox, oy, oz, sf, ls, out};
  return s;
}

std::shared_ptr<FieldNode> makeNoiseDisplaceSdf(const std::string& shortId) {
  return std::make_shared<NoiseDisplaceSDFNode>(shortId);
}

const FieldOp g_noiseDisplaceSdfOp(noiseDisplaceSdfSpec(), makeNoiseDisplaceSdf);

}  // namespace

// Param-cook + test seam (mirrors configureBendField): set the REAL Amount/Scale/Offset/StepFactor/
// useLocalSpace on a makeFieldNode("NoiseDisplaceSDF",...) node, plus a test-only injectBug (0 none /
// 1 drop-StepFactor / 2 drop-noise-add). The leaf type is TU-private; this downcasts inside the owning
// TU. Production passes injectBug=0.
void configureNoiseDisplaceSdf(FieldNode& node, float amount, float scale, float ox, float oy, float oz,
                               float stepFactor, bool useLocalSpace, int injectBug) {
  if (auto* n = dynamic_cast<NoiseDisplaceSDFNode*>(&node)) {
    n->amount = amount;
    n->scale = scale;
    n->ox = ox;
    n->oy = oy;
    n->oz = oz;
    n->stepFactor = stepFactor;
    n->useLocalSpace = useLocalSpace;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
