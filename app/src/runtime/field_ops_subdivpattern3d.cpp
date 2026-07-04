// SubDivPattern3d — generate/texture field GENERATOR (zero input): recursive 2D cell-subdivision
// pattern painted into the carried color f.rgb (a pure COLOR field, writes f.rgb only, never f.w).
// For each sample p it walks up to MaxSubdivisions binary splits of the unit cell containing p.xy
// (split axis chosen by a per-cell hash vs the cell aspect; split position = SplitPosition ±
// SplitVariation·hash), stops early when a hash falls under Threshold, then shades: gap (Padding
// band, Feather smoothstep) → GapColor, cell interior → lerp(ColorA, ColorB, splitF) where splitF =
// step/steps (ColorMode 1) or hash11u(mainHash) (ColorMode 0 — NOTE: mainHash∈[0,1) truncates to
// uint 0, so TiXL's own path yields hash11u(0), ported verbatim).
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/texture/SubDivPattern3d.cs
//   AddDefinitions (:26-133): Globals["Common"], Globals["CommonHgSdf"], Globals["ComputeSubdivision"]
//     (the SubDivParams struct :34-51 + ComputeSubdivision body :53-131, which #includes
//     shared/hash-functions.hlsl for hash11u/_PRIME1/_PRIME2).
//   GetPostShaderCode (:135-155): fills a local SubDivParams from the {n}-prefixed [GraphParam]s
//     (SubdivisionThreshold ← {n}Threshold) then `f{c}.rgb = ComputeSubdivision(p{c}.xy, param{n}).rgb;`
//   [GraphParam] declaration order (:158-204, == param-buffer layout): GapColor, ColorA, ColorB,
//     SplitPosition, SplitVariation, Padding, UseAspectForSplit, MaxSubdivisions, ColorMode,
//     RandomSeed, Threshold, Feather.
//   .t3 defaults: GapColor=(0,0,0,1), ColorA=(0,0,0,1), ColorB=(1,1,1,1), SplitPosition=0.5,
//     SplitVariation=0.8, Padding=0.02, UseAspectForSplit=1, MaxSubdivisions=8, ColorMode=1,
//     RandomSeed=42, Threshold=0.2, Feather=0.02.
//
// Forks vs SubDivPattern3d.cs (named, load-bearing):
//   (1) HLSL lerp -> MSL mix (the two shade lines, .cs:128-130).
//   (2) `#include "shared/hash-functions.hlsl"` -> the used subset inlined as Globals["HashFunctions"]
//       (byte-verbatim _PRIME0/1/2 defines + hash11u from hash-functions.hlsl:4-6,:115-123 — MSL has
//       no HLSL include path). Keyed "HashFunctions" so any sibling registering the same subset de-dups.
//   (3) HLSL `mod` — the FLOORED macro from Common (registered verbatim, de-dups by key "Common").
//   (4) `(int2)(p2 + float2(242, 1241))` -> constructor cast `int2(...)` (MSL has no C-style vector
//       cast); `float2 size = 1` -> `float2 size = float2(1)` (explicit splat). Scalar C-casts kept.
//   (5) `[loop]` attribute dropped (HLSL-only).
//   (6) int [GraphParam]s (UseAspectForSplit/MaxSubdivisions/ColorMode/RandomSeed) ride the sw float
//       param buffer (appendScalarParam is float-only); the emitted fill casts back via `int(...)`.
//       Values are small integers -> float32 roundtrip exact.
//   (7) This op writes ONLY f.rgb (no f.w) — same GPU-golden posture as Raster3dField (the golden
//       reads f.r via a golden-local template variant; production codegen byte-faithful).
//   (8) PARAM-NAME PREFIX: TiXL {n} == BuildNodeId "<TypeName>_<shortGuid>_"; sw reproduces it as
//       prefix "SubDivPattern3d_<id>_", accessed P.<prefix><Name>.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec4Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- Common: PI + PHI + the FLOORED `mod` macro (byte-verbatim, same key/text as Raster3dField) ----
static const char* kCommonSdp =
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

// ---- Hash subset ComputeSubdivision uses, byte-verbatim from TiXL
// Operators/Lib/Assets/shaders/shared/hash-functions.hlsl (:4-6 the _PRIME defines, :115-123 hash11u).
// Fork (2): inlined instead of #include. PREPENDED into the "ComputeSubdivision" global below (NOT a
// separate globals key: c.globals emits in key order, and "ComputeSubdivision" < "HashFunctions"
// lexicographically would emit the kernel before its hash dependency -> undeclared identifier).
static const char* kHashFunctionsSdp =
    "#ifndef _PRIME0\n"
    "#define _PRIME0 13331U\n"
    "#define _PRIME1 1345777U\n"
    "#define _PRIME2 98777177U\n"
    "#endif\n"
    "\n"
    "float hash11u(uint x)\n"
    "{\n"
    "    const uint k = 1103515245U; // GLIB C\n"
    "    x *= _PRIME0;\n"
    "    x = ((x >> 8U) ^ x) * k;\n"
    "    x = ((x >> 8U) ^ x) * k;\n"
    "\n"
    "    return float(x) * (1.0 / float(0xffffffffU));\n"
    "}\n";

// ---- ComputeSubdivision: SubDivParams struct (.cs:34-51) + body (.cs:53-131), verbatim modulo the
// named forks (2)(4)(5). Every seed/hash line keeps TiXL's exact int/uint mixed arithmetic.
static const char* kComputeSubdivisionSdp =
    "struct SubDivParams\n"
    "{\n"
    "    float4 GapColor;\n"
    "    float4 ColorA;\n"
    "    float4 ColorB;\n"
    "\n"
    "    float SplitPosition;\n"
    "    float SplitVariation;\n"
    "    float SubdivisionThreshold;\n"
    "    float Padding;\n"
    "\n"
    "    float Feather;\n"
    "    int UseAspectForSplit;\n"
    "    int MaxSubdivisions;\n"
    "\n"
    "    int ColorMode;\n"
    "    int RandomSeed;\n"
    "};\n"
    "\n"
    "float4 ComputeSubdivision(float2 p2, SubDivParams p)\n"
    "{\n"
    "    int steps = (int)clamp(p.MaxSubdivisions, 1, 30);\n"
    "\n"
    "    float2 uvInCell = mod(p2,1);\n"
    "\n"
    "    int step;\n"
    "    int2 cellIds = int2(p2+ float2(242, 1241));\n"                       // fork (4): int2(...) cast
    "    int mainSeed = p.RandomSeed + cellIds.x * 2 + (cellIds.y + 3)*12311;\n"
    "    float mainHash = hash11u(mainSeed);\n"
    "\n"
    "    float2 size = float2(1);\n"                                          // fork (4): explicit splat
    "    float phaseHashForCell = (mainHash - 0.5) * p.SplitVariation + p.SplitPosition;\n"
    "    int seedInCell = mainSeed + p.RandomSeed;\n"
    "    uint lastDirection = 0;\n"
    "\n"
    "    for (step = 0; step < steps; ++step)\n"                              // fork (5): [loop] dropped
    "    {\n"
    "        float aspect = p.UseAspectForSplit == 1 ? size.x / size.y : 1;\n"
    "\n"
    "        // Split vertically\n"
    "        if (hash11u(seedInCell) * 2 < aspect)\n"
    "        {\n"
    "            if (uvInCell.x < phaseHashForCell)\n"
    "            {\n"
    "                uvInCell.x /= phaseHashForCell;\n"
    "                size.x *= phaseHashForCell;\n"
    "                mainSeed += (int)(phaseHashForCell + 2123u);\n"
    "                seedInCell *= 2;\n"
    "            }\n"
    "            else\n"
    "            {\n"
    "                uvInCell.x = (uvInCell.x - phaseHashForCell) / (1 - phaseHashForCell);\n"
    "                size.x *= (1 - phaseHashForCell);\n"
    "                mainSeed = (int)(mainSeed + 213u) % 1251u;\n"
    "                seedInCell *= 3;\n"
    "            }\n"
    "\n"
    "            lastDirection = 0;\n"
    "        }\n"
    "        // Split horizontally\n"
    "        else\n"
    "        {\n"
    "            if (uvInCell.y < phaseHashForCell)\n"
    "            {\n"
    "                uvInCell.y /= phaseHashForCell;\n"
    "                size.y *= phaseHashForCell;\n"
    "                mainSeed = (int)(mainSeed + _PRIME2) % _PRIME1;\n"
    "                seedInCell *= 5;\n"
    "            }\n"
    "            else\n"
    "            {\n"
    "                uvInCell.y = (uvInCell.y - phaseHashForCell) / (1 - phaseHashForCell);\n"
    "                size.y *= (1 - phaseHashForCell);\n"
    "                mainSeed = (int)(mainSeed + _PRIME1) % _PRIME2;\n"
    "                seedInCell *= 7;\n"
    "            }\n"
    "            lastDirection = 1;\n"
    "        }\n"
    "\n"
    "        float hash = hash11u(seedInCell);\n"
    "        phaseHashForCell = (mainHash - 0.5) * p.SplitVariation + p.SplitPosition;\n"
    "\n"
    "        if (hash <= p.SubdivisionThreshold)\n"
    "            break;\n"
    "    }\n"
    "\n"
    "    float splitF = p.ColorMode == 0 ? hash11u(mainHash) : step / (float)steps;\n"
    "\n"
    "    float2 dd = (uvInCell - 0.5) * size;\n"
    "    float2 d4 = (size - abs(dd * 2)); // * float2(aspectRatio, 1);\n"
    "\n"
    "    float d5 = min(d4.x, d4.y);\n"
    "    float sGap = smoothstep(p.Padding - p.Feather, p.Padding + p.Feather, d5);\n"
    "    return mix(p.GapColor, \n"                                           // fork (1): lerp -> mix
    "    mix(p.ColorA, p.ColorB, splitF), \n"
    "    sGap);\n"
    "}\n";

// ---- SubDivPattern3d codegen node (FieldNode subclass; zero-input color generator) -----------------

struct SubDivPattern3dNode : FieldNode {
  // [GraphParam] defaults from SubDivPattern3d.t3 (declaration order == buffer layout).
  float gcR = 0.f, gcG = 0.f, gcB = 0.f, gcA = 1.f;  // GapColor = (0,0,0,1)
  float caR = 0.f, caG = 0.f, caB = 0.f, caA = 1.f;  // ColorA = (0,0,0,1)
  float cbR = 1.f, cbG = 1.f, cbB = 1.f, cbA = 1.f;  // ColorB = (1,1,1,1)
  float splitPosition = 0.5f;                        // SplitPosition
  float splitVariation = 0.8f;                       // SplitVariation
  float padding = 0.02f;                             // Padding
  float useAspectForSplit = 1.f;                     // UseAspectForSplit (int on float rail, fork 6)
  float maxSubdivisions = 8.f;                       // MaxSubdivisions
  float colorMode = 1.f;                             // ColorMode
  float randomSeed = 42.f;                           // RandomSeed
  float threshold = 0.2f;                            // Threshold
  float feather = 0.02f;                             // Feather
  // test-only bug modes (configureSubDivPattern3d): 0 = none, 1 = swap ColorA/ColorB in the emitted
  // param fill (the splitF lerp endpoints flip), 2 = drop the post line (f.rgb stays the seed).
  int injectBug = 0;

  explicit SubDivPattern3dNode(const std::string& shortId) {
    prefix = "SubDivPattern3d_" + shortId + "_";  // TiXL BuildNodeId convention
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // SubDivPattern3d.cs:28-30 registers Common/CommonHgSdf + ComputeSubdivision (:31-132). CommonHgSdf
    // is registered by TiXL but ComputeSubdivision uses nothing from it beyond what Common carries; sw
    // registers Common + the kernel (with the hash subset prepended — fork 2 note above).
    c.globals["Common"] = kCommonSdp;
    c.globals["ComputeSubdivision"] = std::string(kHashFunctionsSdp) + "\n" + kComputeSubdivisionSdp;
  }

  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void postShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY SubDivPattern3d.cs:135-155 GetPostShaderCode: fill SubDivParams from the {n} params, then
    // f{c}.rgb = ComputeSubdivision(p{c}.xy, param{n}).rgb. {c}=context ("" at root); {n}=prefix.
    if (injectBug == 2) return;  // drop the post line -> f.rgb stays the seed -> probes RED.
    const std::string ctx = c.ctx();
    const std::string n = "P." + prefix;
    const std::string v = "param" + prefix;
    // {n}ColorA/{n}ColorB swapped under injectBug==1 (the splitF lerp endpoints flip -> cell probes RED).
    const std::string colA = (injectBug == 1) ? (n + "ColorB") : (n + "ColorA");
    const std::string colB = (injectBug == 1) ? (n + "ColorA") : (n + "ColorB");
    c.appendCall(
        "SubDivParams " + v + ";\n"
        "  " + v + ".GapColor = " + n + "GapColor;\n"
        "  " + v + ".ColorA = " + colA + ";\n"
        "  " + v + ".ColorB = " + colB + ";\n"
        "  " + v + ".SplitPosition = " + n + "SplitPosition;\n"
        "  " + v + ".SplitVariation = " + n + "SplitVariation;\n"
        "  " + v + ".SubdivisionThreshold = " + n + "Threshold;\n"
        "  " + v + ".Padding = " + n + "Padding;\n"
        "  " + v + ".Feather = " + n + "Feather;\n"
        "  " + v + ".UseAspectForSplit = int(" + n + "UseAspectForSplit);\n"   // fork (6)
        "  " + v + ".MaxSubdivisions = int(" + n + "MaxSubdivisions);\n"
        "  " + v + ".ColorMode = int(" + n + "ColorMode);\n"
        "  " + v + ".RandomSeed = int(" + n + "RandomSeed);\n"
        "  f" + ctx + ".rgb = ComputeSubdivision(p" + ctx + ".xy, " + v + ").rgb;");
  }

  void collectParams(std::vector<float>& fp, std::vector<std::string>& pf) const override {
    // [GraphParam] declaration order (SubDivPattern3d.cs:158-204): GapColor, ColorA, ColorB,
    // SplitPosition, SplitVariation, Padding, UseAspectForSplit, MaxSubdivisions, ColorMode,
    // RandomSeed, Threshold, Feather.
    appendVec4Param(fp, pf, prefix + "GapColor", gcR, gcG, gcB, gcA);
    appendVec4Param(fp, pf, prefix + "ColorA", caR, caG, caB, caA);
    appendVec4Param(fp, pf, prefix + "ColorB", cbR, cbG, cbB, cbA);
    appendScalarParam(fp, pf, prefix + "SplitPosition", splitPosition);
    appendScalarParam(fp, pf, prefix + "SplitVariation", splitVariation);
    appendScalarParam(fp, pf, prefix + "Padding", padding);
    appendScalarParam(fp, pf, prefix + "UseAspectForSplit", useAspectForSplit);
    appendScalarParam(fp, pf, prefix + "MaxSubdivisions", maxSubdivisions);
    appendScalarParam(fp, pf, prefix + "ColorMode", colorMode);
    appendScalarParam(fp, pf, prefix + "RandomSeed", randomSeed);
    appendScalarParam(fp, pf, prefix + "Threshold", threshold);
    appendScalarParam(fp, pf, prefix + "Feather", feather);
  }
};

NodeSpec subDivPattern3dSpec() {
  NodeSpec s;
  s.type = "SubDivPattern3d";
  s.title = "SubDiv Pattern 3d";
  auto vec4 = [](const char* base, const char* disp, float r, float g, float b, float a,
                 std::vector<PortSpec>& out) {
    const char* comp[4] = {".r", ".g", ".b", ".a"};
    float def[4] = {r, g, b, a};
    for (int i = 0; i < 4; ++i) {
      PortSpec p; p.id = std::string(base) + comp[i];
      p.name = i == 0 ? std::string(disp) : std::string(disp) + comp[i];
      p.dataType = "Float"; p.isInput = true; p.def = def[i]; p.minV = 0.0f; p.maxV = 1.0f;
      if (i == 0) { p.widget = Widget::Vec; p.vecArity = 4; }
      out.push_back(p);
    }
  };
  std::vector<PortSpec> ports;
  vec4("GapColor", "Gap Color", 0, 0, 0, 1, ports);
  vec4("ColorA", "Color A", 0, 0, 0, 1, ports);
  vec4("ColorB", "Color B", 1, 1, 1, 1, ports);
  PortSpec sp; sp.id = "SplitPosition"; sp.name = "Split Position"; sp.dataType = "Float"; sp.isInput = true;
  sp.def = 0.5f; sp.minV = 0.0f; sp.maxV = 1.0f; ports.push_back(sp);
  PortSpec sv; sv.id = "SplitVariation"; sv.name = "Split Variation"; sv.dataType = "Float"; sv.isInput = true;
  sv.def = 0.8f; sv.minV = 0.0f; sv.maxV = 1.0f; ports.push_back(sv);
  PortSpec pd; pd.id = "Padding"; pd.name = "Padding"; pd.dataType = "Float"; pd.isInput = true;
  pd.def = 0.02f; pd.minV = 0.0f; pd.maxV = 1.0f; ports.push_back(pd);
  PortSpec ua; ua.id = "UseAspectForSplit"; ua.name = "Use Aspect For Split"; ua.dataType = "Float";
  ua.isInput = true; ua.def = 1.0f; ua.minV = 0.0f; ua.maxV = 1.0f; ua.widget = Widget::Bool;
  ports.push_back(ua);
  PortSpec ms; ms.id = "MaxSubdivisions"; ms.name = "Max Subdivisions"; ms.dataType = "Float";
  ms.isInput = true; ms.def = 8.0f; ms.minV = 1.0f; ms.maxV = 30.0f; ports.push_back(ms);
  PortSpec cm; cm.id = "ColorMode"; cm.name = "Color Mode"; cm.dataType = "Float"; cm.isInput = true;
  cm.def = 1.0f; cm.minV = 0.0f; cm.maxV = 1.0f; cm.widget = Widget::Enum;
  cm.labels = {"Random", "Steps"}; ports.push_back(cm);
  PortSpec rs; rs.id = "RandomSeed"; rs.name = "Random Seed"; rs.dataType = "Float"; rs.isInput = true;
  rs.def = 42.0f; rs.minV = 0.0f; rs.maxV = 10000.0f; ports.push_back(rs);
  PortSpec th; th.id = "Threshold"; th.name = "Threshold"; th.dataType = "Float"; th.isInput = true;
  th.def = 0.2f; th.minV = 0.0f; th.maxV = 1.0f; ports.push_back(th);
  PortSpec ft; ft.id = "Feather"; ft.name = "Feather"; ft.dataType = "Float"; ft.isInput = true;
  ft.def = 0.02f; ft.minV = 0.0f; ft.maxV = 1.0f; ports.push_back(ft);
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  ports.push_back(out);
  s.ports = ports;
  return s;
}

std::shared_ptr<FieldNode> makeSubDivPattern3d(const std::string& shortId) {
  return std::make_shared<SubDivPattern3dNode>(shortId);
}

// PF-0c param-apply: project a RESOLVED param map onto the node (slot ids == PortSpec.id).
void configureSubDivPattern3dFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<SubDivPattern3dNode*>(&node)) {
    applyFloatSlot(m, "GapColor.r", [&](float v) { n->gcR = v; });
    applyFloatSlot(m, "GapColor.g", [&](float v) { n->gcG = v; });
    applyFloatSlot(m, "GapColor.b", [&](float v) { n->gcB = v; });
    applyFloatSlot(m, "GapColor.a", [&](float v) { n->gcA = v; });
    applyFloatSlot(m, "ColorA.r", [&](float v) { n->caR = v; });
    applyFloatSlot(m, "ColorA.g", [&](float v) { n->caG = v; });
    applyFloatSlot(m, "ColorA.b", [&](float v) { n->caB = v; });
    applyFloatSlot(m, "ColorA.a", [&](float v) { n->caA = v; });
    applyFloatSlot(m, "ColorB.r", [&](float v) { n->cbR = v; });
    applyFloatSlot(m, "ColorB.g", [&](float v) { n->cbG = v; });
    applyFloatSlot(m, "ColorB.b", [&](float v) { n->cbB = v; });
    applyFloatSlot(m, "ColorB.a", [&](float v) { n->cbA = v; });
    applyFloatSlot(m, "SplitPosition", [&](float v) { n->splitPosition = v; });
    applyFloatSlot(m, "SplitVariation", [&](float v) { n->splitVariation = v; });
    applyFloatSlot(m, "Padding", [&](float v) { n->padding = v; });
    applyFloatSlot(m, "UseAspectForSplit", [&](float v) { n->useAspectForSplit = v; });
    applyFloatSlot(m, "MaxSubdivisions", [&](float v) { n->maxSubdivisions = v; });
    applyFloatSlot(m, "ColorMode", [&](float v) { n->colorMode = v; });
    applyFloatSlot(m, "RandomSeed", [&](float v) { n->randomSeed = v; });
    applyFloatSlot(m, "Threshold", [&](float v) { n->threshold = v; });
    applyFloatSlot(m, "Feather", [&](float v) { n->feather = v; });
  }
}

const FieldOp g_subDivPattern3dOp(
    subDivPattern3dSpec(), makeSubDivPattern3d, configureSubDivPattern3dFromParams,
    {"GapColor.r", "GapColor.g", "GapColor.b", "GapColor.a", "ColorA.r", "ColorA.g", "ColorA.b",
     "ColorA.a", "ColorB.r", "ColorB.g", "ColorB.b", "ColorB.a", "SplitPosition", "SplitVariation",
     "Padding", "UseAspectForSplit", "MaxSubdivisions", "ColorMode", "RandomSeed", "Threshold",
     "Feather"});

}  // namespace

// Param-cook + test seam (leaf type TU-private; the golden forward-declares this). Production passes
// injectBug=0. injectBug: 1 = swap ColorA/ColorB in the emitted fill; 2 = drop the post line.
void configureSubDivPattern3d(FieldNode& node, const float gapColor[4], const float colorA[4],
                              const float colorB[4], float splitPosition, float splitVariation,
                              float padding, int useAspectForSplit, int maxSubdivisions, int colorMode,
                              int randomSeed, float threshold, float feather, int injectBug) {
  if (auto* n = dynamic_cast<SubDivPattern3dNode*>(&node)) {
    n->gcR = gapColor[0]; n->gcG = gapColor[1]; n->gcB = gapColor[2]; n->gcA = gapColor[3];
    n->caR = colorA[0]; n->caG = colorA[1]; n->caB = colorA[2]; n->caA = colorA[3];
    n->cbR = colorB[0]; n->cbG = colorB[1]; n->cbB = colorB[2]; n->cbA = colorB[3];
    n->splitPosition = splitPosition;
    n->splitVariation = splitVariation;
    n->padding = padding;
    n->useAspectForSplit = (float)useAspectForSplit;
    n->maxSubdivisions = (float)maxSubdivisions;
    n->colorMode = (float)colorMode;
    n->randomSeed = (float)randomSeed;
    n->threshold = threshold;
    n->feather = feather;
    n->injectBug = injectBug;
  }
}

}  // namespace sw
