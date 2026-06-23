// SpatialDisplaceSDF — single-input field MODIFIER (PRE-wrap): displaces the SAMPLE POINT p by a
// per-axis 3D simplex-noise vector BEFORE the wrapped field is evaluated, so the child samples a warped
// point and the whole shape ripples. Drives the PRE half of the field_graph single-input wrap branch
// (field_graph.cpp:82-86): it emits ONLY preShaderCode (executed BEFORE recursing the child) and has no
// post code. Unlike NoiseDisplaceSDF (which adds noise to the DISTANCE f.w), this warps the POSITION p
// (a vector displacement) — the difference between "bumpy surface" and "wobbly space".
//
// TiXL authority: external/tixl/Operators/Lib/field/adjust/SpatialDisplaceSDF.cs
//   AddDefinitions (SpatialDisplaceSDF.cs:26-131): TWO globals —
//     c.Globals["fSimplexNoiseDisplace"]   = mod289/permute/taylorInvSqrt/simplexNoise3D/
//                                            fSimplexNoiseDisplace(pos, amount, scale, offset) (BARE
//                                            helper names — distinct from NoiseDisplaceSDF's _noiseOffset_
//                                            block; see fork (4)).
//     c.Globals["fSimplexNoiseDisplace3D"] = float3 vNoise(pos, amount, scale, vscale, offset, spos) {
//                                              xN = fSimplexNoiseDisplace(pos+float3(spos.x,0,0), amount,
//                                                   scale*vscale.x, offset); (and y/z) ; return (xN,yN,zN);
//                                            } (vNoise CALLS fSimplexNoiseDisplace).
//   GetPreShaderCode (SpatialDisplaceSDF.cs:133-137):
//     c.AppendCall($"p{c}.xyz += vNoise(p{c}.xyz, {n}Amount, {n}Scale, {n}vScale, -{n}Offset, {n}SamplePos );");
//     (note the verbatim trailing space before `)` — kept.)
//   [GraphParam] order: Amount (float) / Scale (float) / vScale (Vector3) / Offset (Vector3) / SamplePos
//     (Vector3) (SpatialDisplaceSDF.cs:142-161). One InputField; one Result. NO enum/bool selector.
//
// Branch: SINGLE-INPUT PRE-wrap (pre BEFORE child recurse; no post). Same shape as Translate/RotateField.
//
// Forks vs SpatialDisplaceSDF.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes `{n}Amount` etc. ({n}=ShaderNode=BuildNodeId
//       "<TypeName>_<shortGuid>_"). sw's frozen convention (combinesdf.cpp:288) reproduces
//       P.SpatialDisplaceSDF_<id>_Amount / _Scale / _vScale / _Offset / _SamplePos. A wrong prefix reads
//       the wrong/0 member and the golden's warped probe bites.
//   (2) HLSL->MSL — both globals are float-math only (the noise body + vNoise's three float3-ctor calls);
//       identical text in MSL. NO inout, NO swizzle-by-ref -> no Cut-94 fix. vNoise CALLS
//       fSimplexNoiseDisplace -> declaration-before-use is required; the field codegen emits globals in
//       std::map KEY order and "fSimplexNoiseDisplace" < "fSimplexNoiseDisplace3D" alphabetically, so
//       fSimplexNoiseDisplace is emitted FIRST and vNoise resolves cleanly -> NO MSL forward-decl needed
//       (the favourable-order sibling of CombineSDF fork (5)).
//   (3) Offset NEGATION — the call passes `-{n}Offset` (a packed_float3 negated -> float3; matches the
//       `float3 offset` param). Kept verbatim (TiXL's negation, not a fork).
//   (4) SHARED-KEY note — both this op and NoiseDisplaceSDF register a global under the SAME std::map KEY
//       "fSimplexNoiseDisplace" with DIFFERENT bodies (this op's helpers are bare mod289/permute/...;
//       NoiseDisplaceSDF's are `_noiseOffset_`-prefixed). In a graph mixing BOTH the map keeps ONE body
//       (last addGlobals wins) — MIRRORING TiXL's own Dictionary semantics, NOT a sw regression. Both
//       bodies expose the SAME `fSimplexNoiseDisplace(float3,float,float,float3)` signature, so whichever
//       survives still satisfies BOTH ops' call sites (this op's vNoise + NoiseDisplaceSDF's direct call).
//       This op's ISOLATED shader is byte-faithful. NAMED so the orchestrator's mixed-graph compile knows
//       the collision is expected & benign.
//   Test-only seam: configureSpatialDisplaceSdf sets the REAL Amount/Scale/vScale/Offset/SamplePos AND an
//       injectBug that corrupts the OP'S REAL pre emit (drop the `p.xyz += vNoise(...)` line) so the
//       golden's warped probe bites the op's real emit. Production default injectBug=0.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam, appendVec3Param
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// fSimplexNoiseDisplace global — byte-verbatim from SpatialDisplaceSDF.cs:28-119 (BARE helper names —
// mod289/permute/taylorInvSqrt/simplexNoise3D; distinct from NoiseDisplaceSDF's _noiseOffset_ block, see
// fork (4)). Self-contained, float-math only. The .cs has two trailing blank lines after the closing
// brace; kept (the assembler appends "\n\n" after each global anyway, so trailing blanks are cosmetic).
static const char* kBodyFSimplexNoiseDisplace =
    "float mod289(float x) {\n"
    "    return x - floor(x * (1.0 / 289.0)) * 289.0;\n"
    "}\n"
    "\n"
    "float3 mod289(float3 x) {\n"
    "    return x - floor(x * (1.0 / 289.0)) * 289.0;\n"
    "}\n"
    "\n"
    "float4 mod289(float4 x) {\n"
    "    return x - floor(x * (1.0 / 289.0)) * 289.0;\n"
    "}\n"
    "\n"
    "float4 permute(float4 x) {\n"
    "    return mod289(((x * 34.0) + 1.0) * x);\n"
    "}\n"
    "\n"
    "float4 taylorInvSqrt(float4 r) {\n"
    "    return 1.79284291400159 - 0.85373472095314 * r;\n"
    "}\n"
    "\n"
    "float simplexNoise3D(float3 v) {\n"
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
    "    i = mod289(i);\n"
    "    float4 p = permute(permute(permute(\n"
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
    "    float4 norm = taylorInvSqrt(float4(dot(g0,g0), dot(g1,g1), dot(g2,g2), dot(g3,g3)));\n"
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
    "    return simplexNoise3D(pos / scale + offset ) * amount;\n"
    "}";

// fSimplexNoiseDisplace3D global — byte-verbatim from SpatialDisplaceSDF.cs:121-130. vNoise CALLS
// fSimplexNoiseDisplace; the favourable std::map KEY order ("fSimplexNoiseDisplace" emitted first)
// satisfies MSL declaration-before-use -> no forward-decl (fork (2)).
static const char* kBodyFSimplexNoiseDisplace3D =
    "float3 vNoise(float3 pos, float amount, float scale, float3 vscale, float3 offset, float3 spos){\n"
    "    \n"
    "    float xN = fSimplexNoiseDisplace(pos+float3(spos.x,0,0), amount, scale*vscale.x, offset);\n"
    "    float yN = fSimplexNoiseDisplace(pos+float3(0,spos.y,0), amount, scale*vscale.y, offset);\n"
    "    float zN = fSimplexNoiseDisplace(pos+float3(0,0,spos.z), amount, scale*vscale.z, offset);\n"
    "    return float3(xN,yN,zN);\n"
    "}";

// ---- SpatialDisplaceSDF codegen node (a FieldNode subclass; single-input modifier — PRE-wrap) -------

struct SpatialDisplaceSDFNode : FieldNode {
  float amount = 0.5f;                  // .t3 default Amount = 0.5. Packed [GraphParam].
  float scale = 1.f;                    // .t3 default Scale = 1.0. Packed [GraphParam].
  float vsx = 1.f, vsy = 1.f, vsz = 1.f;// .t3 default vScale = (1,1,1). Packed [GraphParam] (vec3).
  float ox = 0.f, oy = 0.f, oz = 0.f;   // .t3 default Offset = (0,0,0). Packed [GraphParam] (vec3).
  float spx = 0.f, spy = 0.f, spz = 0.f;// .t3 default SamplePos = (0,0,0). Packed [GraphParam] (vec3).
  // test-only bug mode (configureSpatialDisplaceSdf): 0 = none, 1 = drop the pre `p.xyz += vNoise(...)`
  // line (no warp). Corrupts the OP'S REAL pre emit.
  int injectBug = 0;

  explicit SpatialDisplaceSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "SpatialDisplaceSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // SpatialDisplaceSDF.cs:26-131 — TWO globals (keys match TiXL nameof). See fork (4) for the
    // shared-key note vs NoiseDisplaceSDF; fork (2) for the favourable emission order (vNoise resolves).
    c.globals["fSimplexNoiseDisplace"] = kBodyFSimplexNoiseDisplace;
    c.globals["fSimplexNoiseDisplace3D"] = kBodyFSimplexNoiseDisplace3D;
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY SpatialDisplaceSDF.cs:133-137: warp the sample point by the per-axis noise vector BEFORE the
    // child recursion. {c} = context id (root ""); {n}<Name> -> P.<prefix><Name> (fork (1)); Offset
    // negated (fork (3)). The verbatim trailing space before `)` is kept.
    const std::string ctx = c.ctx();
    if (injectBug == 1) return;  // drop the pre line -> no warp -> warped probe RED.
    const std::string p = "p" + ctx + ".xyz";
    c.appendCall(p + " += vNoise(" + p + ", P." + prefix + "Amount, P." + prefix + "Scale, P." + prefix +
                 "vScale, -P." + prefix + "Offset, P." + prefix + "SamplePos );");
  }

  // Modifier: no post code (TiXL SpatialDisplaceSDF has no GetPostShaderCode).

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order (SpatialDisplaceSDF.cs:142-161): Amount / Scale / vScale
    // (vec3) / Offset (vec3) / SamplePos (vec3). The vec3 padding rule lands each vec3 correctly
    // (appendVec3Param owns padForVec3). No compile-time selector.
    appendScalarParam(floatParams, paramFields, prefix + "Amount", amount);
    appendScalarParam(floatParams, paramFields, prefix + "Scale", scale);
    appendVec3Param(floatParams, paramFields, prefix + "vScale", vsx, vsy, vsz);
    appendVec3Param(floatParams, paramFields, prefix + "Offset", ox, oy, oz);
    appendVec3Param(floatParams, paramFields, prefix + "SamplePos", spx, spy, spz);
  }
};

NodeSpec spatialDisplaceSdfSpec() {
  NodeSpec s;
  s.type = "SpatialDisplaceSDF";
  s.title = "Spatial Displace SDF";
  // One Field input (TiXL InputField). dataType "Field" blocks wrong-type wires.
  PortSpec in; in.id = "InputField"; in.name = "Input Field"; in.dataType = "Field"; in.isInput = true;
  // Amount = displacement magnitude [GraphParam] float, .t3 default 0.5.
  PortSpec am; am.id = "Amount"; am.name = "Amount"; am.dataType = "Float"; am.isInput = true;
  am.def = 0.5f; am.minV = -10.0f; am.maxV = 10.0f;
  // Scale = noise spatial scale [GraphParam] float, .t3 default 1.0.
  PortSpec sc; sc.id = "Scale"; sc.name = "Scale"; sc.dataType = "Float"; sc.isInput = true;
  sc.def = 1.0f; sc.minV = 0.001f; sc.maxV = 10.0f;
  // vScale = per-axis scale multiplier [GraphParam] Vec3, default (1,1,1).
  PortSpec vx; vx.id = "vScale.x"; vx.name = "vScale"; vx.dataType = "Float"; vx.isInput = true;
  vx.def = 1.0f; vx.minV = 0.0f; vx.maxV = 10.0f; vx.widget = Widget::Vec; vx.vecArity = 3;
  PortSpec vy; vy.id = "vScale.y"; vy.name = "vScale.y"; vy.dataType = "Float"; vy.isInput = true;
  vy.def = 1.0f; vy.minV = 0.0f; vy.maxV = 10.0f;
  PortSpec vz; vz.id = "vScale.z"; vz.name = "vScale.z"; vz.dataType = "Float"; vz.isInput = true;
  vz.def = 1.0f; vz.minV = 0.0f; vz.maxV = 10.0f;
  // Offset = Vec3 noise-domain offset [GraphParam], default (0,0,0).
  PortSpec ox; ox.id = "Offset.x"; ox.name = "Offset"; ox.dataType = "Float"; ox.isInput = true;
  ox.def = 0.0f; ox.minV = -10.0f; ox.maxV = 10.0f; ox.widget = Widget::Vec; ox.vecArity = 3;
  PortSpec oy; oy.id = "Offset.y"; oy.name = "Offset.y"; oy.dataType = "Float"; oy.isInput = true;
  oy.def = 0.0f; oy.minV = -10.0f; oy.maxV = 10.0f;
  PortSpec oz; oz.id = "Offset.z"; oz.name = "Offset.z"; oz.dataType = "Float"; oz.isInput = true;
  oz.def = 0.0f; oz.minV = -10.0f; oz.maxV = 10.0f;
  // SamplePos = Vec3 per-axis sample shift (so each axis samples a slightly offset noise) [GraphParam],
  // default (0,0,0).
  PortSpec px; px.id = "SamplePos.x"; px.name = "Sample Pos"; px.dataType = "Float"; px.isInput = true;
  px.def = 0.0f; px.minV = -10.0f; px.maxV = 10.0f; px.widget = Widget::Vec; px.vecArity = 3;
  PortSpec py; py.id = "SamplePos.y"; py.name = "Sample Pos.y"; py.dataType = "Float"; py.isInput = true;
  py.def = 0.0f; py.minV = -10.0f; py.maxV = 10.0f;
  PortSpec pz; pz.id = "SamplePos.z"; pz.name = "Sample Pos.z"; pz.dataType = "Float"; pz.isInput = true;
  pz.def = 0.0f; pz.minV = -10.0f; pz.maxV = 10.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {in, am, sc, vx, vy, vz, ox, oy, oz, px, py, pz, out};
  return s;
}

std::shared_ptr<FieldNode> makeSpatialDisplaceSdf(const std::string& shortId) {
  return std::make_shared<SpatialDisplaceSDFNode>(shortId);
}

// PF-0c param-apply (WAVE 2): project a RESOLVED param map onto a SpatialDisplaceSDFNode via setter-
// lambdas (NOT offsetof). Slot ids EQUAL the NodeSpec PortSpec.id (Amount, Scale, vScale.x/.y/.z,
// Offset.x/.y/.z, SamplePos.x/.y/.z). injectBug is NOT a param (test-only, set via the positional
// configureSpatialDisplaceSdf seam). A missing key keeps the member's ctor .t3 default. Routed via the
// fieldConfigurers() table.
void configureSpatialDisplaceSdfFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<SpatialDisplaceSDFNode*>(&node)) {
    applyFloatSlot(m, "Amount", [&](float v) { n->amount = v; });
    applyFloatSlot(m, "Scale", [&](float v) { n->scale = v; });
    applyFloatSlot(m, "vScale.x", [&](float v) { n->vsx = v; });
    applyFloatSlot(m, "vScale.y", [&](float v) { n->vsy = v; });
    applyFloatSlot(m, "vScale.z", [&](float v) { n->vsz = v; });
    applyFloatSlot(m, "Offset.x", [&](float v) { n->ox = v; });
    applyFloatSlot(m, "Offset.y", [&](float v) { n->oy = v; });
    applyFloatSlot(m, "Offset.z", [&](float v) { n->oz = v; });
    applyFloatSlot(m, "SamplePos.x", [&](float v) { n->spx = v; });
    applyFloatSlot(m, "SamplePos.y", [&](float v) { n->spy = v; });
    applyFloatSlot(m, "SamplePos.z", [&](float v) { n->spz = v; });
  }
}

// slot ids = the SAME ids configureSpatialDisplaceSdfFromParams applies (Option B guard reads them).
const FieldOp g_spatialDisplaceSdfOp(spatialDisplaceSdfSpec(), makeSpatialDisplaceSdf,
                                     configureSpatialDisplaceSdfFromParams,
                                     {"Amount", "Scale", "vScale.x", "vScale.y", "vScale.z", "Offset.x",
                                      "Offset.y", "Offset.z", "SamplePos.x", "SamplePos.y",
                                      "SamplePos.z"});

}  // namespace

// Param-cook + test seam (mirrors configureRotateField): set the REAL Amount/Scale/vScale/Offset/
// SamplePos on a makeFieldNode("SpatialDisplaceSDF",...) node, plus a test-only injectBug (0 none /
// 1 drop-pre-warp). The leaf type is TU-private; this downcasts inside the owning TU. Production passes
// injectBug=0.
void configureSpatialDisplaceSdf(FieldNode& node, float amount, float scale, float vsx, float vsy,
                                 float vsz, float ox, float oy, float oz, float spx, float spy,
                                 float spz, int injectBug) {
  if (auto* n = dynamic_cast<SpatialDisplaceSDFNode*>(&node)) {
    n->amount = amount;
    n->scale = scale;
    n->vsx = vsx;
    n->vsy = vsy;
    n->vsz = vsz;
    n->ox = ox;
    n->oy = oy;
    n->oz = oz;
    n->spx = spx;
    n->spy = spy;
    n->spz = spz;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
