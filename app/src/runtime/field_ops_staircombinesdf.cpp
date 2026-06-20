// StairCombineSDF — multi-input SDF boolean fold with CARPENTER-JOINERY flavours (columns / stairs /
// groove / tongue). Sibling of CombineSDF (the first multi-input combiner): postShaderCode folds each
// child into the accumulator via kModes[combineMethod] (a compile-time selector, NOT a runtime uniform —
// mirrors TiXL CombineMethod InputSlot<int> / FlagCodeChanged). TWO packed [GraphParam] floats: K (the
// joint radius) and Steps (the column/stair count, passed as the helper's `n`).
//
// TiXL authority: external/tixl/Operators/Lib/field/combine/StairCombineSDF.cs
//
// Forks vs StairCombineSDF.cs (named, load-bearing):
//   (1) PARAM-NAME PREFIX — TiXL writes `{n}K` / `{n}Steps` where {n}=ShaderNode interpolates to
//       BuildNodeId "<TypeName>_<shortGuid>_". sw's frozen convention (combinesdf.cpp:288) reproduces it:
//       P.StairCombineSDF_<id>_K / P.StairCombineSDF_<id>_Steps. A wrong prefix reads the wrong/0 member
//       and the golden's fold probe bites.
//   (2) HLSL->MSL helper port — the Columns helpers (fOpUnionColumns / fOpDifferenceColumns) call pR45
//       and pMod1 (from CommonHgSdf). pR45(p) is called with a LOCAL float2 `p` (legal with thread
//       float2&) BUT pMod1(p.y, ...) is called with a SWIZZLE `p.y` -> needs the ★CUT-94 BY-VALUE pMod1
//       form (MSL rejects binding a swizzle to thread float&). So this op registers the CommonHgSdf
//       whose pMod1 is BY-VALUE + RETURN (byte-identical to field_ops_repeataxis.cpp's kCommonHgSdf, the
//       MSL-legal swizzle form), NOT combinesdf.cpp's `thread float& pMod1` form. SAME-KEY note: the key
//       is "CommonHgSdf" (TiXL nameof); RepeatAxis registers the SAME by-value body under the SAME key ->
//       de-dups cleanly in a mixed graph. (CombineSDF registers a DIFFERENT pMod1 body under the same
//       key — a PRE-EXISTING latent collision between CombineSDF and RepeatAxis, NOT introduced here; a
//       graph mixing CombineSDF + StairCombineSDF Columns-mode would inherit it. The default UnionStairs
//       mode uses NEITHER pMod1 nor pR45, so the default golden is unaffected.)
//   (3) variadic MultiInputSlot -> fixed-2 InputA/InputB in the NodeSpec (same fork as CombineSDF (3));
//       the base codegen handles N inputs, the UI exposes 2.
//   (4) HLSL->MSL `sqrt(2)` -> `sqrt(2.0)` (load-bearing, blood-proven by the UnionColumns golden):
//       MSL's sqrt is OVERLOADED on half/float, so an INTEGER literal `sqrt(2)` is AMBIGUOUS (clang:
//       "call to 'sqrt' is ambiguous"), where HLSL silently promotes the int to float. The Columns
//       helpers' `sqrt(2)` are ported to `sqrt(2.0)` (the ONLY change; the result is byte-identical
//       sqrt(2.0f)). `mod(n,2)` stays verbatim — the Common `mod` MACRO expands `2` into float division
//       so no overload ambiguity. The ternary color line `f{pc}.xyz = f{pc}.w < f{c}.w ? f{pc}.xyz :
//       f{c}.xyz;` is float3-vs-float3 (legal MSL `?:`) — emitted for byte-parity (carries the nearer
//       child's color; not observed in the f.w-only golden, named like CombineSDF's color fork (4)).
//   (5) GLOBAL-ORDER — the Columns helpers (fOpDifferenceColumns / fOpIntersectionColumns) call EACH
//       OTHER (Intersection calls Difference). std::map KEY order: "fOpDifferenceColumns" <
//       "fOpIntersectionColumns" -> Difference emitted FIRST -> Intersection (which calls it) resolves
//       cleanly (declaration-before-use satisfied). So NO forward-decl is needed for THIS pair (unlike
//       CombineSDF fork (5), where Difference called Intersection and sorted BEFORE it). The Stairs trio
//       (Union/Intersection/Difference) all live in ONE global string with Union defined first, so the
//       intra-string order already satisfies MSL.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- the two ALWAYS-ON globals (StairCombineSDF.cs:40-41) -----------------------------------------
// Keys match TiXL nameof so de-dup across combiners / mixed graphs is exact.
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

// CommonHgSdf — byte-identical to field_ops_repeataxis.cpp's kCommonHgSdf (the ★CUT-94 BY-VALUE pMod1
// form), so the Columns helpers' `pMod1(p.y, ...)` swizzle call site compiles in MSL and so a mixed
// graph with RepeatAxis de-dups (same key, byte-identical body). pR45 stays `thread float2&` (its call
// sites pass a LOCAL float2, never a swizzle). See fork (2).
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
    "\n"
    "// Rotate around a coordinate axis (i.e. in a plane perpendicular to that axis) by angle <a>.\n"
    "// Read like this: R(p.xz, a) rotates \"x towards z\".\n"
    "// This is fast if <a> is a compile-time constant and slower (but still practical) if not.\n"
    "void pR(thread float2& p, float a) {\n"
    "\tp = cos(a)*p + sin(a)*float2(p.y, -p.x);\n"
    "}\n"
    "\n"
    "// Shortcut for 45-degrees rotation\n"
    "void pR45(thread float2& p) {\n"
    "\tp = (p + float2(p.y, -p.x))*sqrt(0.5);\n"
    "}\n"
    "\n"
    "// Repeat space along one axis. Use like this to repeat along the x axis:\n"
    "// <float cell = pMod1(p.x,5);> - using the return value is optional.\n"
    "// MSL fork: HLSL inout float p -> by-value float p, the FOLDED point is returned so a SWIZZLE call\n"
    "// site (p.y) can be a legal lvalue assignment target. Math byte-identical to the inout copy-in/out.\n"
    "float pMod1(float p, float size) {\n"
    "\tfloat halfsize = size*0.5;\n"
    "\tfloat c = floor((p + halfsize)/size);\n"
    "\tp = mod(p + halfsize, size) - halfsize;\n"
    "\treturn p;\n"
    "}";

// ---- mode helper bodies (verbatim from StairCombineSDF.cs) ----------------------------------------
// The Columns helpers call pMod1(p.y,...) (swizzle -> needs by-value pMod1) and pR45(p) (local float2).
// CUT-94 NOTE: TiXL's helper writes `pMod1(p.y, columnradius*2);` discarding the return; the by-value
// MSL pMod1 needs the result ASSIGNED BACK to be a real fold -> ported as `p.y = pMod1(p.y, ...);`
// (HLSL inout copy-out == by-value return assign; byte-identical effect). This is the SAME swizzle fix
// applied at the helper-INTERNAL call site (not the op's emit). Named under fork (2).
static const char* kBodyUnionColumns =
    "float fOpUnionColumns(float a, float b, float r, float n) {\n"
    " if ((a < r) && (b < r)) {\n"
    "  float2 p = float2(a, b);\n"
    "  float columnradius = r*sqrt(2.0)/((n-1)*2+sqrt(2.0));\n"
    "  pR45(p);\n"
    "  p.x -= sqrt(2.0)/2*r;\n"
    "  p.x += columnradius*sqrt(2.0);\n"
    "  if (mod(n,2) == 1) {\n"
    "   p.y += columnradius;\n"
    "  }\n"
    "  // At this point, we have turned 45 degrees and moved at a point on the\n"
    "  // diagonal that we want to place the columns on.\n"
    "  // Now, repeat the domain along this direction and place a circle.\n"
    "  p.y = pMod1(p.y, columnradius*2);\n"
    "  float result = length(p) - columnradius;\n"
    "  result = min(result, p.x);\n"
    "  result = min(result, a);\n"
    "  return min(result, b);\n"
    " } else {\n"
    "  return min(a, b);\n"
    " }\n"
    "}";
static const char* kBodyDifferenceColumns =
    "float fOpDifferenceColumns(float a, float b, float r, float n) {\n"
    "    a = -a;\n"
    "    float m = min(a, b);\n"
    "    //avoid the expensive computation where not needed (produces discontinuity though)\n"
    "    if ((a < r) && (b < r)) {\n"
    "    float2 p = float2(a, b);\n"
    "    float columnradius = r*sqrt(2.0)/n/2.0;\n"
    "    columnradius = r*sqrt(2.0)/((n-1)*2+sqrt(2.0));\n"
    "\n"
    "    pR45(p);\n"
    "    p.y += columnradius;\n"
    "    p.x -= sqrt(2.0)/2*r;\n"
    "    p.x += -columnradius*sqrt(2.0)/2;\n"
    "\n"
    "    if (mod(n,2) == 1) {\n"
    "    p.y += columnradius;\n"
    "    }\n"
    "    p.y = pMod1(p.y,columnradius*2);\n"
    "\n"
    "    float result = -length(p) + columnradius;\n"
    "    result = max(result, p.x);\n"
    "    result = min(result, a);\n"
    "    return -min(result, b);\n"
    "    } else {\n"
    "    return -m;\n"
    "    }\n"
    "}";
static const char* kBodyIntersectionColumns =
    "float fOpIntersectionColumns(float a, float b, float r, float n) {\n"
    "     return fOpDifferenceColumns(a,-b,r, n);\n"
    "}";
// The Stairs trio shares ONE global string; Union is defined first so Intersection/Difference (which
// call it) satisfy MSL declaration-before-use intra-string. Verbatim from StairCombineSDF.cs:121-135.
static const char* kBodyStairs =
    "float fOpUnionStairs(float a, float b, float r, float n) {\n"
    "  float s = r/n;\n"
    "  float u = b-r;\n"
    "  return min(min(a,b), 0.5 * (u + a + abs ((mod (u - a + s, 2 * s)) - s)));\n"
    "}\n"
    "\n"
    "float fOpIntersectionStairs(float a, float b, float r, float n) {\n"
    "  return -fOpUnionStairs(-a, -b, r, n);\n"
    "}\n"
    "float fOpDifferenceStairs(float a, float b, float r, float n) {\n"
    "\treturn -fOpUnionStairs(-a, b, r, n);\n"
    "}";
static const char* kBodyGroove =
    "float fOpGroove(float a, float b, float ra, float rb) {\n"
    "\treturn max(a, min(a + ra, rb - abs(b)));\n"
    "}";
static const char* kBodyTongue =
    "float fOpTongue(float a, float b, float ra, float rb) {\n"
    "return min(a, max(a - ra, abs(b) - rb));\n"
    "}";

// ---- combine-mode data table (ARCHITECTURE rule 7) -----------------------------------------------
// One row per CombineMethods value (index == enum value == StairCombineSDF.cs:216-227). Each row carries
// up to TWO helper globals (key+body; the Columns difference path needs both fOpDifferenceColumns and
// fOpIntersectionColumns) and the fold function NAME. ALL modes pass (K, Steps) -> the call expr is
// uniform: `f{pc}.w = <fn>(f{pc}.w, f{c}.w, {n}K, {n}Steps);`.
struct StairMode {
  const char* name;        // enum value name (NodeSpec labels / debugging)
  const char* helperKey;   // primary global helper key
  const char* helperBody;  // primary global helper body
  const char* helperKey2;  // secondary helper key ("" if none — Columns difference/intersection)
  const char* helperBody2; // secondary helper body
  const char* fnName;      // the fold function NAME called in postShaderCode
};

// kModes index == CombineMethods value (StairCombineSDF.cs:216-227).
//   0 UnionColumns        -> fOpUnionColumns       (uses pR45+pMod1 from CommonHgSdf)
//   1 DifferenceColumns   -> fOpDifferenceColumns  (+ also registers fOpIntersectionColumns per the .cs)
//   2 IntersectionColumns -> fOpIntersectionColumns (calls fOpDifferenceColumns -> register BOTH)
//   3 UnionStairs         -> fOpUnionStairs        (DEFAULT — Stairs trio, no CommonHgSdf helper needed)
//   4 IntersectionStairs  -> fOpIntersectionStairs (Stairs trio)
//   5 DifferenceStairs    -> fOpDifferenceStairs   (Stairs trio)
//   6 Groove              -> fOpGroove
//   7 Tongue              -> fOpTongue
static const StairMode kModes[] = {
    /* 0 */ {"UnionColumns", "fOpUnionColumns", kBodyUnionColumns, "", "", "fOpUnionColumns"},
    /* 1 */ {"DifferenceColumns", "fOpDifferenceColumns", kBodyDifferenceColumns,
             "fOpIntersectionColumns", kBodyIntersectionColumns, "fOpDifferenceColumns"},
    /* 2 */ {"IntersectionColumns", "fOpDifferenceColumns", kBodyDifferenceColumns,
             "fOpIntersectionColumns", kBodyIntersectionColumns, "fOpIntersectionColumns"},
    /* 3 */ {"UnionStairs", "fOpUnionStairs", kBodyStairs, "", "", "fOpUnionStairs"},
    /* 4 */ {"IntersectionStairs", "fOpUnionStairs", kBodyStairs, "", "", "fOpIntersectionStairs"},
    /* 5 */ {"DifferenceStairs", "fOpUnionStairs", kBodyStairs, "", "", "fOpDifferenceStairs"},
    /* 6 */ {"Groove", "fOpGroove", kBodyGroove, "", "", "fOpGroove"},
    /* 7 */ {"Tongue", "fOpTongue", kBodyTongue, "", "", "fOpTongue"},
};
constexpr int kModeCount = static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));

// ---- StairCombineSDF codegen node (a FieldNode subclass; combiner — multi-input path) -------------

struct StairCombineSDFNode : FieldNode {
  float k = 0.5f;        // StairCombineSDF.t3 default K (joint radius). Packed [GraphParam].
  float steps = 3.f;     // StairCombineSDF.t3 default Steps (column/stair count = helper `n`). Packed.
  int combineMethod = 3; // StairCombineSDF.t3 default = 3 (UnionStairs). Compile-time selector, NOT packed.

  explicit StairCombineSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix.
    prefix = "StairCombineSDF_" + shortId + "_";
  }

  const StairMode& mode() const {
    int m = (combineMethod >= 0 && combineMethod < kModeCount) ? combineMethod : 3;
    return kModes[m];
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // StairCombineSDF.cs:40-41 — two always-on includes. The Stairs trio shares CommonHgSdf via Common's
    // `mod`; the Columns modes additionally NEED CommonHgSdf's pR45+pMod1. Register CommonHgSdf
    // unconditionally (the .cs registers it always at :41, then re-registers for the Columns branch).
    c.globals["Common"] = kCommon;
    c.globals["CommonHgSdf"] = kCommonHgSdf;

    // StairCombineSDF.cs:44-155 — register the ONE mode helper (and its secondary, for the Columns
    // difference/intersection pair).
    const StairMode& md = mode();
    if (md.helperKey && md.helperKey[0]) c.globals[md.helperKey] = md.helperBody;
    if (md.helperKey2 && md.helperKey2[0]) c.globals[md.helperKey2] = md.helperBody2;
  }

  // Combiner: no pre code (the base seeds p<sub>/f<sub> from the parent before this).
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void postShaderCode(CodeAssembleCtx& c, int inputIndex) const override {
    // PARITY StairCombineSDF.cs:158-209 (GetPostShaderCode).
    //   <2 connected inputs -> just a skip comment (StairCombineSDF.cs:161-165).
    if (c.contextIdStack.size() < 2) {
      c.appendCall("// skipping combine with single or no input...");
      return;
    }
    // contextId = parent (^2), subContextId = the just-recursed child (^1 = top).
    const std::string parent = c.contextIdStack[c.contextIdStack.size() - 2];
    const std::string sub = c.ctx();

    if (inputIndex == 0) {
      // Keep the first child's field as the accumulator (StairCombineSDF.cs:172-176).
      c.appendCall("f" + parent + " = f" + sub + ";");
      return;
    }

    // i>0: the mode's distance fold (uniform call: K + Steps), then the nearer-child color carry.
    const std::string kExpr = "P." + prefix + "K";
    const std::string stepsExpr = "P." + prefix + "Steps";
    const StairMode& md = mode();
    c.appendCall("f" + parent + ".w = " + std::string(md.fnName) + "(f" + parent + ".w, f" + sub +
                 ".w, " + kExpr + ", " + stepsExpr + ");");
    // color carry (fork (4): float3-vs-float3 ternary; not observed in the f.w-only golden).
    c.appendCall("f" + parent + ".xyz = f" + parent + ".w < f" + sub + ".w ? f" + parent + ".xyz : f" +
                 sub + ".xyz;");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] order = .cs declaration order: K (StairCombineSDF.cs:232-234) then Steps
    // (StairCombineSDF.cs:236-238). combineMethod is a compile-time selector (NOT packed). Two scalars.
    appendScalarParam(floatParams, paramFields, prefix + "K", k);
    appendScalarParam(floatParams, paramFields, prefix + "Steps", steps);
  }
};

NodeSpec stairCombineSdfSpec() {
  NodeSpec s;
  s.type = "StairCombineSDF";
  s.title = "Stair Combine SDF";
  // Two Field inputs (fork (3): TiXL is a variadic MultiInputSlot; UI exposes fixed 2). dataType "Field".
  PortSpec inA; inA.id = "InputA"; inA.name = "Input A"; inA.dataType = "Field"; inA.isInput = true;
  PortSpec inB; inB.id = "InputB"; inB.name = "Input B"; inB.dataType = "Field"; inB.isInput = true;
  // K = joint radius [GraphParam] float, .t3 default 0.5.
  PortSpec kp; kp.id = "K"; kp.name = "K"; kp.dataType = "Float"; kp.isInput = true;
  kp.def = 0.5f; kp.minV = 0.0f; kp.maxV = 10.0f;
  // Steps = column/stair count [GraphParam] float (passed as the helper's `n`), .t3 default 3.0.
  PortSpec st; st.id = "Steps"; st.name = "Steps"; st.dataType = "Float"; st.isInput = true;
  st.def = 3.0f; st.minV = 1.0f; st.maxV = 32.0f;
  // CombineMethod = enum CODE SELECTOR (Widget::Enum dropdown) — a Float port storing the enum index,
  // .t3 default 3 (UnionStairs). NOT a [GraphParam]; the node's `combineMethod` int carries it at codegen
  // time. Labels mirror StairCombineSDF.cs:216-227 by index.
  PortSpec cm; cm.id = "CombineMethod"; cm.name = "Combine Method"; cm.dataType = "Float";
  cm.isInput = true; cm.def = 3.0f; cm.minV = 0.0f; cm.maxV = 7.0f; cm.widget = Widget::Enum;
  cm.labels = {"UnionColumns", "DifferenceColumns", "IntersectionColumns",
               "UnionStairs", "IntersectionStairs", "DifferenceStairs",
               "Groove", "Tongue"};
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {inA, inB, kp, st, cm, out};
  return s;
}

std::shared_ptr<FieldNode> makeStairCombineSdf(const std::string& shortId) {
  return std::make_shared<StairCombineSDFNode>(shortId);
}

const FieldOp g_stairCombineSdfOp(stairCombineSdfSpec(), makeStairCombineSdf);

}  // namespace

// Param-cook seam (mirrors configureCombineSdf): set K / Steps / combineMethod on a node built via
// makeFieldNode("StairCombineSDF", ...). The leaf type is TU-private; this downcasts inside the owning
// TU. K + Steps are packed [GraphParam]s; combineMethod is the compile-time code selector. No-op if
// `node` is not a StairCombineSDFNode (defensive).
void configureStairCombineSdf(FieldNode& node, float k, float steps, int combineMethod) {
  if (auto* n = dynamic_cast<StairCombineSDFNode*>(&node)) {
    n->k = k;
    n->steps = steps;
    n->combineMethod = combineMethod;
  }
}
}  // namespace sw
