// Raster3dField — generate/texture field GENERATOR (zero input): paints a 3D raster grid into the
// carried color f.rgb. It is a pure COLOR field (writes f.rgb only, never f.w): for each sample p it
// computes a cell-edge line factor via fRaster3d (a periodic box-frame line) and lerps between ColorA
// and ColorB by that factor. No SDF distance is produced — this op is meant to feed a downstream color
// step (SetSDFMaterial's ColorField / a raymarch color), exactly like SubDivPattern3d / the texture
// field family.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/texture/Raster3dField.cs
//   AddDefinitions (Raster3dField.cs:30-44): registers Globals["Common"], Globals["CommonHgSdf"]
//     (for `mod` + `vmax`), and Globals["fRaster3d"]:
//       float fRaster3d(float3 p, float3 center, float3 size, float lineWidth, float feather) {
//           float3 q = mod(p / size - center, 1) - 0.5;
//           float distanceToEdge = vmax(abs(q));
//           float line2 = smoothstep(lineWidth/2 + feather, lineWidth/2 - feather, distanceToEdge);
//           return line2;
//       }
//   GetPostShaderCode (Raster3dField.cs:47-51):
//       f{c}.rgb = lerp({n}ColorA.rgb, {n}ColorB.rgb,
//                       fRaster3d(p{c}.xyz, {n}Offset, {n}Scale, {n}LineWidth, {n}Feather));
//   [GraphParam] order (Raster3dField.cs, declaration order == param-buffer layout):
//     ColorA (Vector4), ColorB (Vector4), Offset (Vector3), Scale (Vector3), LineWidth, Feather.
//
//   .t3 defaults (Raster3dField.t3): ColorA=(0,0,0,1), ColorB=(1,1,1,1), Offset=(0,0,0),
//     Scale=(1,1,1), LineWidth=0.9, Feather=0.002.
//
// Branch: ZERO-INPUT GENERATOR (no Field input). The base calls preShaderCode(0) then postShaderCode(0)
// with nothing between (field_graph.cpp:64-68). TiXL emits via GetPostShaderCode, so this op emits in
// postShaderCode and leaves preShaderCode empty — semantically identical for a leaf (both fire at the
// root context, index 0).
//
// Forks vs Raster3dField.cs (named, load-bearing):
//   (1) HLSL lerp -> MSL mix (the post line). fRaster3d's body has no lerp.
//   (2) HLSL `mod` — the `#define mod(x,y) ((x)-(y)*floor((x)/(y)))` macro from Common (HLSL's `%`/`fmod`
//       truncates toward zero; this macro is FLOORED). MSL has a `fmod` (truncated) but NOT this floored
//       mod, so the Common macro is registered verbatim (same key "Common" the repeat ops use -> de-dups).
//       `mod(p/size - center, 1)` -> the `1` scalar broadcasts to float3 against the float3 LHS in BOTH
//       HLSL and MSL (the macro is type-generic). No literal change.
//   (3) `vmax(float3)` from CommonHgSdf (max(max(x,y),z)). Registered with key "CommonHgSdf" (de-dups
//       with RepeatAxis/CombineSDF's identical copy).
//   (4) PARAM-NAME PREFIX — TiXL's {n} ( == {ShaderNode} ) interpolates to BuildNodeId
//       "<TypeName>_<shortGuid>_"; sw's frozen convention reproduces it as prefix "Raster3dField_<id>_",
//       accessed P.<prefix><Name>. Wrong prefix -> reads the wrong/0 struct member -> golden bites.
//   (5) Vector4 ColorA/ColorB packed via appendVec4Param; Vector3 Offset/Scale via appendVec3Param
//       (the 16B-align helpers). Declaration order = the GraphParam order above (param-buffer parity).
//   (6) This op writes ONLY f.rgb (no f.w). The stock field_render template emits f.w into RED, so the
//       op's effect is invisible there; its GPU golden uses a golden-LOCAL template variant that reads
//       f.r back (see field_ops_raster3dfield_golden.cpp). The production codegen is byte-faithful.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendVec4Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- Common: PI + PHI + the FLOORED `mod` macro (byte-verbatim from RepeatAxis/RepeatPolar kCommon) -
// Key "Common" matches TiXL nameof -> de-dups with any other op that registers it.
static const char* kCommon =
    "#ifndef PI\n"
    "#define PI 3.141592653589793\n"
    "#endif\n"
    "\n"
    "#ifndef PHI\n"
    "#define PHI (sqrt(5)*0.5 + 0.5)\n"
    "#endif\n"
    "\n"
    "#ifndef mod\n"
    "#define mod(x, y) ((x) - (y) * floor((x) / (y)))\n"
    "#endif";

// ---- CommonHgSdf: carries vmax (the only helper fRaster3d calls). Byte-verbatim from the SAME
// ShaderGraphIncludes block vetted in field_ops_repeataxis.cpp (kCommonHgSdf). Key "CommonHgSdf"
// matches TiXL nameof -> de-dups.
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
    "\n";

// fRaster3d — byte-verbatim from Raster3dField.cs:34-43 (the line2 body). Calls only mod (Common macro),
// vmax (CommonHgSdf), abs/smoothstep (built-ins). Key "fRaster3d" matches TiXL Globals key.
static const char* kFRaster3d =
    "float fRaster3d(float3 p, float3 center, float3 size, float lineWidth, float feather) \n"
    "{\n"
    "    float3 q = mod(p / size - center, 1) - 0.5;\n"
    "    float distanceToEdge = vmax(abs(q));\n"
    "    float line2 = smoothstep(lineWidth / 2 + feather, lineWidth / 2 - feather, distanceToEdge);\n"
    "    return line2;\n"
    "}";

// ---- Raster3dField codegen node (FieldNode subclass; zero-input color generator) ------------------

struct Raster3dFieldNode : FieldNode {
  // [GraphParam] defaults from Raster3dField.t3 (declaration order == buffer layout).
  float caR = 0.f, caG = 0.f, caB = 0.f, caA = 1.f;  // ColorA = (0,0,0,1)
  float cbR = 1.f, cbG = 1.f, cbB = 1.f, cbA = 1.f;  // ColorB = (1,1,1,1)
  float ox = 0.f, oy = 0.f, oz = 0.f;                // Offset = (0,0,0)
  float sx = 1.f, sy = 1.f, sz = 1.f;                // Scale = (1,1,1)
  float lineWidth = 0.9f;                            // LineWidth = 0.9
  float feather = 0.002f;                            // Feather = 0.002
  // test-only bug modes (configureRaster3dField): 0 = none, 1 = swap ColorA/ColorB in the emit (so the
  // lerp endpoints flip), 2 = drop the post line (no color write -> f.rgb stays the seed 1.0).
  int injectBug = 0;

  explicit Raster3dFieldNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix.
    prefix = "Raster3dField_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // Raster3dField.cs:30-44 registers all three unconditionally.
    c.globals["Common"] = kCommon;
    c.globals["CommonHgSdf"] = kCommonHgSdf;
    c.globals["fRaster3d"] = kFRaster3d;
  }

  // Zero-input generator: no pre code (TiXL emits via GetPostShaderCode). preShaderCode is pure virtual.
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY Raster3dField.cs:47-51 GetPostShaderCode. {c}=context ("" at root); {n}=prefix.
    if (injectBug == 2) return;  // drop the post line -> f.rgb stays the seed (1,1,1) -> probe RED.
    const std::string ctx = c.ctx();
    const std::string n = "P." + prefix;
    // {n}ColorA/{n}ColorB swapped under injectBug==1 (lerp endpoints flip -> RED at non-line texels).
    const std::string colA = (injectBug == 1) ? (n + "ColorB.rgb") : (n + "ColorA.rgb");
    const std::string colB = (injectBug == 1) ? (n + "ColorA.rgb") : (n + "ColorB.rgb");
    // fork (1): lerp -> mix.
    c.appendCall("f" + ctx + ".rgb = mix(" + colA + ", " + colB + ", fRaster3d(p" + ctx + ".xyz, " +
                 n + "Offset, " + n + "Scale, " + n + "LineWidth, " + n + "Feather));");
  }

  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    // [GraphParam] declaration order (Raster3dField.cs): ColorA, ColorB, Offset, Scale, LineWidth, Feather.
    appendVec4Param(fp, pf, prefix + "ColorA", caR, caG, caB, caA);
    appendVec4Param(fp, pf, prefix + "ColorB", cbR, cbG, cbB, cbA);
    appendVec3Param(fp, pf, prefix + "Offset", ox, oy, oz);
    appendVec3Param(fp, pf, prefix + "Scale", sx, sy, sz);
    appendScalarParam(fp, pf, prefix + "LineWidth", lineWidth);
    appendScalarParam(fp, pf, prefix + "Feather", feather);
  }
};

NodeSpec raster3dFieldSpec() {
  NodeSpec s;
  s.type = "Raster3dField";
  s.title = "Raster 3d Field";
  // ColorA [GraphParam] float4 (RGBA), .t3 default (0,0,0,1).
  PortSpec aR; aR.id = "ColorA.r"; aR.name = "Color A"; aR.dataType = "Float"; aR.isInput = true;
  aR.def = 0.0f; aR.minV = 0.0f; aR.maxV = 1.0f; aR.widget = Widget::Vec; aR.vecArity = 4;
  PortSpec aG; aG.id = "ColorA.g"; aG.name = "Color A.g"; aG.dataType = "Float"; aG.isInput = true;
  aG.def = 0.0f; aG.minV = 0.0f; aG.maxV = 1.0f;
  PortSpec aB; aB.id = "ColorA.b"; aB.name = "Color A.b"; aB.dataType = "Float"; aB.isInput = true;
  aB.def = 0.0f; aB.minV = 0.0f; aB.maxV = 1.0f;
  PortSpec aA; aA.id = "ColorA.a"; aA.name = "Color A.a"; aA.dataType = "Float"; aA.isInput = true;
  aA.def = 1.0f; aA.minV = 0.0f; aA.maxV = 1.0f;
  // ColorB [GraphParam] float4 (RGBA), .t3 default (1,1,1,1).
  PortSpec bR; bR.id = "ColorB.r"; bR.name = "Color B"; bR.dataType = "Float"; bR.isInput = true;
  bR.def = 1.0f; bR.minV = 0.0f; bR.maxV = 1.0f; bR.widget = Widget::Vec; bR.vecArity = 4;
  PortSpec bG; bG.id = "ColorB.g"; bG.name = "Color B.g"; bG.dataType = "Float"; bG.isInput = true;
  bG.def = 1.0f; bG.minV = 0.0f; bG.maxV = 1.0f;
  PortSpec bB; bB.id = "ColorB.b"; bB.name = "Color B.b"; bB.dataType = "Float"; bB.isInput = true;
  bB.def = 1.0f; bB.minV = 0.0f; bB.maxV = 1.0f;
  PortSpec bA; bA.id = "ColorB.a"; bA.name = "Color B.a"; bA.dataType = "Float"; bA.isInput = true;
  bA.def = 1.0f; bA.minV = 0.0f; bA.maxV = 1.0f;
  // Offset [GraphParam] float3, .t3 default (0,0,0).
  PortSpec ox; ox.id = "Offset.x"; ox.name = "Offset"; ox.dataType = "Float"; ox.isInput = true;
  ox.def = 0.0f; ox.minV = -10.0f; ox.maxV = 10.0f; ox.widget = Widget::Vec; ox.vecArity = 3;
  PortSpec oy; oy.id = "Offset.y"; oy.name = "Offset.y"; oy.dataType = "Float"; oy.isInput = true;
  oy.def = 0.0f; oy.minV = -10.0f; oy.maxV = 10.0f;
  PortSpec oz; oz.id = "Offset.z"; oz.name = "Offset.z"; oz.dataType = "Float"; oz.isInput = true;
  oz.def = 0.0f; oz.minV = -10.0f; oz.maxV = 10.0f;
  // Scale [GraphParam] float3, .t3 default (1,1,1).
  PortSpec sx; sx.id = "Scale.x"; sx.name = "Scale"; sx.dataType = "Float"; sx.isInput = true;
  sx.def = 1.0f; sx.minV = 0.01f; sx.maxV = 10.0f; sx.widget = Widget::Vec; sx.vecArity = 3;
  PortSpec sy; sy.id = "Scale.y"; sy.name = "Scale.y"; sy.dataType = "Float"; sy.isInput = true;
  sy.def = 1.0f; sy.minV = 0.01f; sy.maxV = 10.0f;
  PortSpec sz; sz.id = "Scale.z"; sz.name = "Scale.z"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 1.0f; sz.minV = 0.01f; sz.maxV = 10.0f;
  // LineWidth [GraphParam] float, .t3 default 0.9.
  PortSpec lw; lw.id = "LineWidth"; lw.name = "Line Width"; lw.dataType = "Float"; lw.isInput = true;
  lw.def = 0.9f; lw.minV = 0.0f; lw.maxV = 1.0f;
  // Feather [GraphParam] float, .t3 default 0.002.
  PortSpec ft; ft.id = "Feather"; ft.name = "Feather"; ft.dataType = "Float"; ft.isInput = true;
  ft.def = 0.002f; ft.minV = 0.0f; ft.maxV = 1.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {aR, aG, aB, aA, bR, bG, bB, bA, ox, oy, oz, sx, sy, sz, lw, ft, out};
  return s;
}

std::shared_ptr<FieldNode> makeRaster3dField(const std::string& shortId) {
  return std::make_shared<Raster3dFieldNode>(shortId);
}

// PF-0c param-apply: project a RESOLVED param map onto a Raster3dFieldNode via setter-lambdas. Slot ids
// EQUAL the NodeSpec PortSpec.id. injectBug is NOT a param (test-only). A missing key keeps the ctor .t3
// default. Routed via fieldConfigurers().
void configureRaster3dFieldFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<Raster3dFieldNode*>(&node)) {
    applyFloatSlot(m, "ColorA.r", [&](float v) { n->caR = v; });
    applyFloatSlot(m, "ColorA.g", [&](float v) { n->caG = v; });
    applyFloatSlot(m, "ColorA.b", [&](float v) { n->caB = v; });
    applyFloatSlot(m, "ColorA.a", [&](float v) { n->caA = v; });
    applyFloatSlot(m, "ColorB.r", [&](float v) { n->cbR = v; });
    applyFloatSlot(m, "ColorB.g", [&](float v) { n->cbG = v; });
    applyFloatSlot(m, "ColorB.b", [&](float v) { n->cbB = v; });
    applyFloatSlot(m, "ColorB.a", [&](float v) { n->cbA = v; });
    applyFloatSlot(m, "Offset.x", [&](float v) { n->ox = v; });
    applyFloatSlot(m, "Offset.y", [&](float v) { n->oy = v; });
    applyFloatSlot(m, "Offset.z", [&](float v) { n->oz = v; });
    applyFloatSlot(m, "Scale.x", [&](float v) { n->sx = v; });
    applyFloatSlot(m, "Scale.y", [&](float v) { n->sy = v; });
    applyFloatSlot(m, "Scale.z", [&](float v) { n->sz = v; });
    applyFloatSlot(m, "LineWidth", [&](float v) { n->lineWidth = v; });
    applyFloatSlot(m, "Feather", [&](float v) { n->feather = v; });
  }
}

const FieldOp g_raster3dFieldOp(raster3dFieldSpec(), makeRaster3dField, configureRaster3dFieldFromParams,
                                {"ColorA.r", "ColorA.g", "ColorA.b", "ColorA.a", "ColorB.r", "ColorB.g",
                                 "ColorB.b", "ColorB.a", "Offset.x", "Offset.y", "Offset.z", "Scale.x",
                                 "Scale.y", "Scale.z", "LineWidth", "Feather"});

}  // namespace

// Param-cook + test seam (leaf type TU-private; golden forward-declares this). Production passes
// injectBug=0. injectBug: 1 = swap ColorA/ColorB in the emit; 2 = drop the post line.
void configureRaster3dField(FieldNode& node, float caR, float caG, float caB, float caA, float cbR,
                            float cbG, float cbB, float cbA, float ox, float oy, float oz, float sx,
                            float sy, float sz, float lineWidth, float feather, int injectBug) {
  if (auto* n = dynamic_cast<Raster3dFieldNode*>(&node)) {
    n->caR = caR; n->caG = caG; n->caB = caB; n->caA = caA;
    n->cbR = cbR; n->cbG = cbG; n->cbB = cbB; n->cbA = cbA;
    n->ox = ox; n->oy = oy; n->oz = oz;
    n->sx = sx; n->sy = sy; n->sz = sz;
    n->lineWidth = lineWidth;
    n->feather = feather;
    n->injectBug = injectBug;
  }
}

}  // namespace sw
