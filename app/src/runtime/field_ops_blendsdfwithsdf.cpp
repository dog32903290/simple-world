// BlendSDFWithSDF — 3-input field COMBINE op that blends two SDFs (FieldA, FieldB) by a third SDF used
// as a MASK (WeightField). Where the mask distance is < Offset the result follows A, where > Offset it
// follows B, with a smooth transition of width ~Range. It folds BOTH the distance f.w (via the
// sdfBlendByMask helper) AND the carried color f.xyz (via a smoothstep lerp). Like PushPullSDF this op
// uses TiXL's CUSTOM-CODE collect (IGraphNodeOp.TryBuildCustomCode): FieldA is collected in the PARENT
// context (it stays the accumulator); FieldB and WeightField each get their OWN subcontext. The standard
// multi-input subcontext loop cannot express this (it would push a subcontext for FieldA too, and would
// not give the named "fieldB"/"weight" suffixes the .cs uses) — hence the tryBuildCustomCode override.
//
// TiXL authority: external/tixl/Operators/Lib/field/combine/BlendSDFWithSDF.cs
//   AddDefinitions (:28-64): Globals[IncludeBiasFunctions], Globals[Common] (for mod), Globals["sdfBlendByMask"]
//     (the smin/smax/blend helper trio).
//   TryBuildCustomCode (:66-111): requires exactly 3 inputs; then
//     inputFieldA.CollectEmbeddedShaderCode(c);                 // FieldA -> PARENT context (no push)
//     c.AppendCall("{"); c.Indent();
//     c.PushContext(c.ContextIdStack.Count, "fieldB"); ... FieldB ... PopContext();
//     c.PushContext(c.ContextIdStack.Count, "weight"); ... Weight ... PopContext();
//     c.AppendCall($"f{c}.w = sdfBlendByMask(f{c}.w, f{B}.w, f{W}.w - {n}Offset, {n}Range);");
//     c.AppendCall($"f{c}.xyz = lerp(f{c}.xyz, f{B}.xyz, smoothstep(0,1,(f{W}.w - {n}Offset)/{n}Range));");
//     c.Unindent(); c.AppendCall("}");
//   [GraphParam] InputSlot<float> Range (.t3 default 1.0); [GraphParam] InputSlot<float> Offset (.t3 0.0).
//   InputSlot<ShaderGraphNode> FieldA (inputs[0]), FieldB (inputs[1]), WeightField (inputs[2]).
//
// Branch: CUSTOM-CODE (tryBuildCustomCode). 3 inputs; FieldA accumulator, FieldB blend target, Weight mask.
//
// ★SHARED-KEY: this op registers Globals["sdfBlendByMask"] (a unique fn-trio key), Globals["Common"]
//   (the SAME byte-identical mod-macro string RepeatPolar/RepeatAxis/CombineSDF/StairCombineSDF register
//   under key "Common" — de-dups cleanly in a mixed graph), and DROPS IncludeBiasFunctions (fork (3)).
//   "sdfBlendByMask" is unique to this op (no other op defines it), so no cross-op redefinition risk;
//   "Common" de-dups by byte-identical body. The mixed-graph compile (this op + another "Common"
//   registrant in one graph) is verified by the golden's two-op tree + a standalone xcrun metal compile.
//
// Forks vs BlendSDFWithSDF.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — {ShaderNode}Range / {ShaderNode}Offset -> P.<prefix>Range / P.<prefix>Offset
//       (sw frozen convention). A wrong prefix reads the wrong/0 member and the blend probe bites.
//   (2) HLSL lerp -> MSL mix — appears in BOTH the helper body (`lerp(b, a, h)`) AND the op's emitted
//       f.xyz line (`lerp(f{c}.xyz, f{B}.xyz, ...)`). All `lerp` -> `mix`. `saturate`/`smoothstep`/`max`/
//       `min` keep their names (legal MSL built-ins). The `mod` macro comes from Common (unused by the
//       emitted code here but registered per the .cs — harmless).
//   (3) DROP IncludeBiasFunctions — TiXL's `IncludeBiasFunctions` global is the literal HLSL directive
//       `#include "shared/bias-functions.hlsl"`. MSL has no such file and the sdfBlendByMask body calls
//       NONE of the bias functions (only saturate/lerp/max/min/smoothstep). Registering it would emit a
//       dangling `#include` that fails MSL compile, so it is DROPPED. Named, load-bearing (the golden's
//       compile would fail if it were kept).
//   (4) CUSTOM-COLLECT via tryBuildCustomCode — FieldA recursed through collectFieldCode in the PARENT
//       context; FieldB and WeightField each pushContext at depth = contextIdStack.size() with the literal
//       suffixes "fieldB"/"weight" (matching TiXL c.ContextIdStack.Count + the named suffix). Both
//       subcontexts use the SAME index (the stack returns to its prior depth after each pop).
//   (5) f.xyz vec-color blend (the ★CombineFieldColor f.xyz precedent class): a float3 = mix(float3,
//       float3, scalar) — legal MSL (scalar broadcasts in mix's t arg). The smoothstep produces a scalar
//       t in [0,1]; `mix(a3, b3, tScalar)` lerps each component. Observable in the golden's f.xyz readback
//       (mirrors CombineFieldColor / TranslateUV goldens).
//   (6) variadic? NO — TiXL fixes exactly 3 InputFields (FieldA/FieldB/WeightField named slots), so the
//       NodeSpec exposes 3 fixed Field inputs (not the variadic-collapsed-to-2 fork CombineSDF uses).
//   Test-only seam: configureBlendSdfWithSdf sets the REAL Range/Offset AND an injectBug (corrupt the
//       blend / drop the f.xyz blend) so the golden's tooth bites the op's REAL emit. Production off.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, collectFieldCode, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- the Common include (byte-identical to the other "Common" registrants — de-dups in mixed graphs) -
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

// ---- sdfBlendByMask helper trio (BlendSDFWithSDF.cs:33-63) ----------------------------------------
// Body byte-verbatim EXCEPT HLSL lerp -> MSL mix (fork (2)). Calls only saturate/mix/max/min (MSL
// built-ins) — NO inter-include call beyond itself; the smin/smax/blend trio is self-contained and
// ordered smin -> smax(calls smin) -> blend(calls both), so declaration-before-use is satisfied
// intra-string (smin first). Unique key "sdfBlendByMask" -> no cross-op redefinition.
static const char* kSdfBlendByMask =
    "// Polynomial smooth min/max (k = transition width in distance units)\n"
    "float sdfBlendByMaskSmin(float a, float b, float k) \n"
    "{\n"
    "    float h = saturate(0.5 + 0.5 * (b - a) / k);\n"
    "    return mix(b, a, h) - k * h * (1.0 - h);\n"
    "}\n"
    "\n"
    "// smooth intersection\n"
    "float sdfBlendByMaskSMax(float a, float b, float k) \n"
    "{ \n"
    "    return -sdfBlendByMaskSmin(-a, -b, k); \n"
    "} \n"
    "\n"
    "// Blend A/B by mask SDF dM. \n"
    "// w = 0 -> hard switch on dM=0 (exact SDF). w>0 -> smooth switch of width ~w.\n"
    "float sdfBlendByMask(float dA, float dB, float dM, float w)\n"
    "{\n"
    "    if (w <= 0.0) {\n"
    "        // Exact: (A intersect {dM<=0}) union (B intersect {dM>=0})\n"
    "        float da = max(dA,  dM);   // intersect A with mask-negative halfspace\n"
    "        float db = max(dB, -dM);   // intersect B with mask-positive halfspace\n"
    "        return min(da, db);        // union the two clipped parts\n"
    "    } else {\n"
    "        // Smooth: replace max/min with smooth variants\n"
    "        float da = sdfBlendByMaskSMax(dA,  dM,  w); // smooth intersection\n"
    "        float db = sdfBlendByMaskSMax(dB, -dM,  w);\n"
    "        return sdfBlendByMaskSmin(da, db, w);       // smooth union\n"
    "    }\n"
    "}";

// ---- BlendSDFWithSDF codegen node (a FieldNode subclass; custom-collect 3-input combiner) ----------

struct BlendSDFWithSDFNode : FieldNode {
  float range = 1.f;   // BlendSDFWithSDF.t3 default Range = 1.0. Packed [GraphParam].
  float offset = 0.f;  // BlendSDFWithSDF.t3 default Offset = 0.0. Packed [GraphParam].
  // test-only bug modes (configureBlendSdfWithSdf): 0 = none, 1 = corrupt the f.w blend (use FieldB
  // directly, ignoring the mask), 2 = drop the f.xyz blend. Both corrupt the OP's REAL emit.
  int injectBug = 0;

  explicit BlendSDFWithSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — this IS the {ShaderNode} prefix the .cs interpolates.
    prefix = "BlendSDFWithSDF_" + shortId + "_";
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // BlendSDFWithSDF.cs:30-63. IncludeBiasFunctions DROPPED (fork (3)). Common (mod) + the blend trio.
    c.globals["Common"] = kCommon;
    c.globals["sdfBlendByMask"] = kSdfBlendByMask;
  }

  bool tryBuildCustomCode(CodeAssembleCtx& c) const override {
    // PARITY BlendSDFWithSDF.cs:66-111. Requires exactly 3 inputs; <3 -> emit nothing (TiXL sets a
    // status warning and returns true). We mirror: if not 3 connected inputs, take over and emit nothing.
    if (inputs.size() != 3 || !inputs[0] || !inputs[1] || !inputs[2]) return true;

    const FieldNode* fieldA = inputs[0].get();
    const FieldNode* fieldB = inputs[1].get();
    const FieldNode* weight = inputs[2].get();

    // FieldA -> PARENT context (fork (4)): recurse with the stack UNCHANGED so it writes into f{c}.
    collectFieldCode(*fieldA, c);

    c.appendCall("{");
    c.indentCount++;

    const std::string parent = c.ctx();

    // FieldB -> "fieldB" subcontext at depth = stack size (TiXL c.ContextIdStack.Count).
    const int idxB = static_cast<int>(c.contextIdStack.size());
    c.pushContext(idxB, "fieldB");
    const std::string subB = c.ctx();
    collectFieldCode(*fieldB, c);
    c.popContext();

    // WeightField -> "weight" subcontext at the SAME depth (the stack returned after the FieldB pop).
    const int idxW = static_cast<int>(c.contextIdStack.size());
    c.pushContext(idxW, "weight");
    const std::string subW = c.ctx();
    collectFieldCode(*weight, c);
    c.popContext();

    const std::string offsetExpr = "P." + prefix + "Offset";
    const std::string rangeExpr = "P." + prefix + "Range";

    // f.w distance blend (fork (1) prefix). injectBug==1 corrupts it: use FieldB directly (mask ignored).
    if (injectBug == 1) {
      c.appendCall("f" + parent + ".w = f" + subB + ".w;");
    } else {
      c.appendCall("f" + parent + ".w = sdfBlendByMask(f" + parent + ".w, f" + subB + ".w, f" + subW +
                   ".w - " + offsetExpr + ", " + rangeExpr + ");");
    }
    // f.xyz color blend (fork (2) lerp->mix; fork (5) vec-color). injectBug==2 drops it.
    if (injectBug != 2) {
      c.appendCall("f" + parent + ".xyz = mix(f" + parent + ".xyz, f" + subB +
                   ".xyz, smoothstep(0, 1, (f" + subW + ".w - " + offsetExpr + ") / " + rangeExpr + "));");
    }

    c.indentCount--;
    c.appendCall("}");
    return true;
  }

  // tryBuildCustomCode owns the whole emit; preShaderCode is pure-virtual so provide an empty override
  // (never reached once tryBuildCustomCode returns true).
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order: Range (BlendSDFWithSDF.cs:131-133) then Offset
    // (BlendSDFWithSDF.cs:135-137). Two scalars. (collectAllParams walks the 3 child fields depth-first
    // FIRST.)
    appendScalarParam(floatParams, paramFields, prefix + "Range", range);
    appendScalarParam(floatParams, paramFields, prefix + "Offset", offset);
  }
};

NodeSpec blendSdfWithSdfSpec() {
  NodeSpec s;
  s.type = "BlendSDFWithSDF";
  s.title = "Blend SDF With SDF";
  // Three FIXED Field inputs (fork (6): TiXL names exactly FieldA/FieldB/WeightField). dataType "Field".
  PortSpec fa; fa.id = "FieldA"; fa.name = "Field A"; fa.dataType = "Field"; fa.isInput = true;
  PortSpec fb; fb.id = "FieldB"; fb.name = "Field B"; fb.dataType = "Field"; fb.isInput = true;
  PortSpec wf; wf.id = "WeightField"; wf.name = "Weight Field"; wf.dataType = "Field"; wf.isInput = true;
  // Range = transition width [GraphParam] float, .t3 default 1.0.
  PortSpec rg; rg.id = "Range"; rg.name = "Range"; rg.dataType = "Float"; rg.isInput = true;
  rg.def = 1.0f; rg.minV = 0.0f; rg.maxV = 10.0f;
  // Offset = mask threshold [GraphParam] float, .t3 default 0.0.
  PortSpec of; of.id = "Offset"; of.name = "Offset"; of.dataType = "Float"; of.isInput = true;
  of.def = 0.0f; of.minV = -10.0f; of.maxV = 10.0f;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {fa, fb, wf, rg, of, out};
  return s;
}

std::shared_ptr<FieldNode> makeBlendSdfWithSdf(const std::string& shortId) {
  return std::make_shared<BlendSDFWithSDFNode>(shortId);
}

const FieldOp g_blendSdfWithSdfOp(blendSdfWithSdfSpec(), makeBlendSdfWithSdf);

}  // namespace

// Param-cook + test seam: set Range/Offset (and a test-only injectBug: 0 none / 1 corrupt-f.w-blend /
// 2 drop-f.xyz-blend) on a makeFieldNode("BlendSDFWithSDF",...) node. The leaf type is TU-private; this
// downcasts inside the owning TU. Production passes injectBug=0.
void configureBlendSdfWithSdf(FieldNode& node, float range, float offset, int injectBug) {
  if (auto* n = dynamic_cast<BlendSDFWithSDFNode*>(&node)) {
    n->range = range;
    n->offset = offset;
    n->injectBug = injectBug;
  }
}
}  // namespace sw
