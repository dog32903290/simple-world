// CombineSDF — multi-input SDF boolean/blend fold; first node using the field_graph multi-input walk.
// postShaderCode folds each child into the accumulator via kModes[combineMethod] (compile-time selector,
// not a runtime uniform — mirrors TiXL CombineMethod InputSlot<int> / FlagCodeChanged path). Only K is
// a packed [GraphParam] float.
//
// TiXL authority: external/tixl/Operators/Lib/field/combine/CombineSDF.cs
//
// Forks vs TiXL CombineSDF.cs (Cut 73):
//   (1) lerp->mix; (2) max(float2,0)->max(float2,float2(0.0)); inout->thread&; (3) variadic->fixed-2
//   InputA/InputB; (4) color .rgb line emitted for byte-parity but not observable; TiXL '// <SymbolName>'
//   sub-context comment omitted.
//   (5) HLSL->MSL global-emission-order: the difference variants (CutOutRound/CutOutChamfer) register
//   TWO helpers; the Difference helper CALLS the Intersection helper. HLSL (DXC) resolves global fns in
//   any textual order; the frozen base emits globals in std::map KEY order, where "fOpDifference*" sorts
//   BEFORE "fOpIntersection*" -> Difference emitted first -> MSL (clang) "undeclared identifier". Fixed by
//   prepending a one-line MSL forward declaration of the Intersection helper inside the Difference body
//   (helper math byte-verbatim; prototype line is the only addition). See kBodyDifference{Round,Chamfer}.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- the three ALWAYS-ON globals (verbatim from ShaderGraphIncludes.cs) --------------------------
// CombineSDF.cs:40-42 registers these unconditionally regardless of the combine method. Keys match
// TiXL's nameof(...) so de-dup across many combiner nodes / mixed graphs is exact.

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
    "float pMod1(thread float& p, float size) {\n"
    "\tfloat halfsize = size*0.5;\n"
    "\tfloat c = floor((p + halfsize)/size);\n"
    "\tp = mod(p + halfsize, size) - halfsize;\n"
    "\treturn c;\n"
    "}";

static const char* kGetColorBlendFactor =
    "  float GetColorBlendFactor(float d2, float d1, float k) \n"
    "  {\n"
    "    return  clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);\n"
    "  };";

// ---- combine-mode data table (ARCHITECTURE rule 7) -----------------------------------------------
// One row per CombineMethods value (index == enum value == CombineSDF.cs:226-245). Each row carries
// the ONE mode helper fn (key+body, "" for modes that need no helper — Union/Intersect/CutOut) and a
// CALL-EXPR FORMAT that builds the `f{parent}.w = ...;` distance line given the parent/sub field names
// and the K param expr. {0}=f{parent}.w accumulator, {1}=f{sub}.w incoming, {2}=P.<prefix>K.
//
// Helper bodies transcribed VERBATIM from CombineSDF.cs (forks (1)/(2) applied where noted). Some
// modes share a helper that another mode also registers (e.g. CutOutRound's fOpDifferenceRound calls
// fOpIntersectionRound, so that mode registers BOTH); helperKey2/helperBody2 carries the second.
struct CombineMode {
  const char* name;        // for the NodeSpec enum labels / debugging (matches enum value name)
  const char* helperKey;   // primary global helper key ("" if none — pure inline min/max)
  const char* helperBody;  // primary global helper body
  const char* helperKey2;  // secondary helper key ("" if none — e.g. difference variants)
  const char* helperBody2; // secondary helper body
  bool usesK;              // does the distance call pass K? (Union/Intersect/CutOut do not)
  // distance call expr builder: given accumulator a (=f{parent}.w), incoming b (=f{sub}.w), K expr.
  std::string (*call)(const std::string& a, const std::string& b, const std::string& k);
};

// Each builder returns the RHS of `f{parent}.w = <here>;` — verbatim from CombineSDF.cs:175-217.
std::string cUnion(const std::string& a, const std::string& b, const std::string&) {
  return "min(" + a + ", " + b + ")";
}
std::string cUnionSoft(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpUnionSoft(" + a + ", " + b + ", " + k + ")";
}
std::string cUnionRound(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpUnionRound(" + a + ", " + b + ", " + k + ")";
}
std::string cUnionChamfer(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpUnionChamfer(" + a + ", " + b + ", " + k + ")";
}
std::string cUnionSmooth(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpSmoothUnion(" + a + ", " + b + ", " + k + ")";
}
std::string cCutOut(const std::string& a, const std::string& b, const std::string&) {
  return "max(" + a + ", -" + b + ")";
}
std::string cCutOutRound(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpDifferenceRound(" + a + ", " + b + ", " + k + ")";
}
std::string cCutOutChamfer(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpDifferenceChamfer(" + a + ", " + b + ", " + k + ")";
}
std::string cIntersect(const std::string& a, const std::string& b, const std::string&) {
  return "max(" + a + ", " + b + ")";
}
std::string cIntersectRound(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpIntersectionRound(" + a + ", " + b + ", " + k + ")";
}
std::string cIntersectChamfer(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpIntersectionChamfer(" + a + ", " + b + ", " + k + ")";
}
std::string cPipe(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpPipe(" + a + ", " + b + ", " + k + ")";
}
std::string cEngrave(const std::string& a, const std::string& b, const std::string& k) {
  return "fOpEngrave(" + a + ", " + b + ", " + k + ")";
}

// Helper bodies (verbatim from CombineSDF.cs; fork (2) applied to round variants' max(float2,0)).
static const char* kBodySmoothUnion =
    "float fOpSmoothUnion(float a, float b, float k) {\n"
    "    float h = max(k - abs(a - b), 0.0);\n"
    "    return min(a, b) - (h * h) / (4.0 * k);\n"
    "};";
static const char* kBodyUnionChamfer =
    "// The \"Chamfer\" flavour makes a 45-degree chamfered edge (the diagonal of a square of size <r>):\n"
    "float fOpUnionChamfer(float a, float b, float r) {\n"
    "    return min(min(a, b), (a - r + b)*sqrt(0.5));\n"
    "}";
static const char* kBodyIntersectionChamfer =
    "// Intersection has to deal with what is normally the inside of the resulting object\n"
    "// when using union, which we normally don't care about too much. Thus, intersection\n"
    "// implementations sometimes differ from union implementations.\n"
    "float fOpIntersectionChamfer(float a, float b, float r) {\n"
    "    return max(max(a, b), (a + r + b)*sqrt(0.5));\n"
    "}";
static const char* kBodyDifferenceChamfer =
    // FORK (5) named, load-bearing — HLSL->MSL global-order: TiXL registers fOpDifferenceChamfer and
    // fOpIntersectionChamfer under two Dictionary keys; HLSL (DXC) resolves global fns regardless of
    // textual order, so TiXL never needs a prototype. The base emits globals in std::map KEY order, so
    // "fOpDifferenceChamfer" < "fOpIntersectionChamfer" alphabetically -> Difference is emitted FIRST,
    // and MSL (clang) requires declaration-before-use -> "undeclared identifier". Prepend a one-line MSL
    // forward declaration so the call resolves in any emission order. The helper BODY below is byte-
    // verbatim from CombineSDF.cs:81-83; only this prototype line is added (zero math change).
    "float fOpIntersectionChamfer(float a, float b, float r);\n"
    "// Difference can be built from Intersection or Union:\n"
    "float fOpDifferenceChamfer (float a, float b, float r) {\n"
    "    return fOpIntersectionChamfer(a, -b, r);\n"
    "}";
static const char* kBodyUnionRound =
    " // The \"Round\" variant uses a quarter-circle to join the two objects smoothly:\n"
    " float fOpUnionRound(float a, float b, float r) {\n"
    "     float2 u = max(float2(r - a,r - b), float2(0.0));\n"
    "     return max(r, min (a, b)) - length(u);\n"
    " }";
static const char* kBodyIntersectionRound =
    "float fOpIntersectionRound(float a, float b, float r) {\n"
    "    float2 u = max(float2(r + a,r + b), float2(0.0));\n"
    "    return min(-r, max (a, b)) + length(u);\n"
    "}      ";
static const char* kBodyDifferenceRound =
    // FORK (5) named, load-bearing — same HLSL->MSL global-order issue as kBodyDifferenceChamfer:
    // "fOpDifferenceRound" < "fOpIntersectionRound" in std::map key order, so Difference is emitted
    // before the Intersection helper it calls; MSL requires declaration-before-use. Prepend the MSL
    // forward declaration. Body below is byte-verbatim from CombineSDF.cs:109-111 (zero math change).
    "float fOpIntersectionRound(float a, float b, float r);\n"
    "float fOpDifferenceRound (float a, float b, float r) {\n"
    "    return fOpIntersectionRound(a, -b, r);\n"
    "}";
static const char* kBodyUnionSoft =
    "float fOpUnionSoft(float a, float b, float r) {\n"
    "\tfloat e = max(r - abs(a - b), 0.0);\n"
    "\treturn min(a, b) - e*e*0.25/r;\n"
    "}";
static const char* kBodyPipe =
    "float fOpPipe(float a, float b, float r) {\n"
    "\treturn length(float2(a, b)) - r;\n"
    "}";
static const char* kBodyEngrave =
    "float fOpEngrave(float a, float b, float r) {\n"
    "\treturn max(a, (a + r - abs(b))*sqrt(0.5));\n"
    "}";

// kModes index == CombineMethods value (CombineSDF.cs:226-245). Pure-inline modes carry "" helper.
// fork (2) note: TiXL HLSL `max(float2(r-a,r-b), 0)` had a trailing `0` (line 91); fixed-2 ports note
// is in the NodeSpec. UnionSoft body uses `0` in HLSL -> `0.0` here (no float2; scalar 0/0.0 same in MSL
// but we keep the verbatim-with-.0 form for clean MSL).
static const CombineMode kModes[] = {
    /* 0  Union           */ {"Union", "", "", "", "", false, cUnion},
    /* 1  UnionSoft       */ {"UnionSoft", "fOpUnionSoft", kBodyUnionSoft, "", "", true, cUnionSoft},
    /* 2  UnionRound      */ {"UnionRound", "fOpUnionRound", kBodyUnionRound, "", "", true, cUnionRound},
    /* 3  UnionChamfer    */ {"UnionChamfer", "fOpUnionChamfer", kBodyUnionChamfer, "", "", true, cUnionChamfer},
    /* 4  UnionSmooth     */ {"UnionSmooth", "fOpSmoothUnion", kBodySmoothUnion, "", "", true, cUnionSmooth},
    /* 5  CutOut          */ {"CutOut", "", "", "", "", false, cCutOut},
    /* 6  CutOutRound     */ {"CutOutRound", "fOpIntersectionRound", kBodyIntersectionRound,
                              "fOpDifferenceRound", kBodyDifferenceRound, true, cCutOutRound},
    /* 7  CutOutChamfer   */ {"CutOutChamfer", "fOpIntersectionChamfer", kBodyIntersectionChamfer,
                              "fOpDifferenceChamfer", kBodyDifferenceChamfer, true, cCutOutChamfer},
    /* 8  Intersect       */ {"Intersect", "", "", "", "", false, cIntersect},
    /* 9  IntersectRound  */ {"IntersectRound", "fOpIntersectionRound", kBodyIntersectionRound, "", "",
                              true, cIntersectRound},
    /* 10 IntersectChamfer*/ {"IntersectChamfer", "fOpIntersectionChamfer", kBodyIntersectionChamfer,
                              "", "", true, cIntersectChamfer},
    /* 11 Pipe            */ {"Pipe", "fOpPipe", kBodyPipe, "", "", true, cPipe},
    /* 12 Engrave         */ {"Engrave", "fOpEngrave", kBodyEngrave, "", "", true, cEngrave},
};
constexpr int kModeCount = static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));

// ---- CombineSDF codegen node (a FieldNode subclass; combiner — lives on the multi-input path) -----

struct CombineSDFNode : FieldNode {
  float k = 0.f;            // CombineSDF.t3 default K (the round/smooth blend radius). Packed.
  int combineMethod = 2;    // CombineSDF.t3 default = 2 (UnionRound). Compile-time selector, NOT packed.

  explicit CombineSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix.
    prefix = "CombineSDF_" + shortId + "_";
  }

  const CombineMode& mode() const {
    int m = (combineMethod >= 0 && combineMethod < kModeCount) ? combineMethod : 2;
    return kModes[m];
  }

  void addGlobals(CodeAssembleCtx& c) const override {
    // CombineSDF.cs:40-42 — three always-on includes (keys match TiXL nameof, de-duped by std::map).
    c.globals["Common"] = kCommon;
    c.globals["CommonHgSdf"] = kCommonHgSdf;
    c.globals["GetColorBlendFactor"] = kGetColorBlendFactor;

    // CombineSDF.cs:45-145 — register the ONE mode helper (and its secondary, for difference variants).
    const CombineMode& md = mode();
    if (md.helperKey && md.helperKey[0]) c.globals[md.helperKey] = md.helperBody;
    if (md.helperKey2 && md.helperKey2[0]) c.globals[md.helperKey2] = md.helperBody2;
  }

  // CombineSDF is a combiner: no pre code (the base seeds p<sub>/f<sub> from the parent before this).
  void preShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const override {}

  void postShaderCode(CodeAssembleCtx& c, int inputIndex) const override {
    // PARITY CombineSDF.cs:148-220 (GetPostShaderCode).
    //   <2 connected inputs -> just a skip comment (CombineSDF.cs:151-155).
    if (c.contextIdStack.size() < 2) {
      c.appendCall("// skipping combine with single or no input...");
      return;
    }
    // contextId = parent (^2), subContextId = the just-recursed child (^1 = top).
    const std::string parent = c.contextIdStack[c.contextIdStack.size() - 2];
    const std::string sub = c.ctx();

    if (inputIndex == 0) {
      // Keep the first child's field as the accumulator (CombineSDF.cs:165).
      c.appendCall("f" + parent + " = f" + sub + ";");
      return;
    }

    // i>0: color blend (fork (1) lerp->mix; fork (4) not observable in the f.w-only golden), then the
    // mode's distance fold. K access qualified P. for the MSL struct (HLSL read a bare cbuffer name).
    const std::string kExpr = "P." + prefix + "K";
    c.appendCall("f" + parent + ".rgb = mix(f" + parent + ".rgb, f" + sub +
                 ".rgb, GetColorBlendFactor(f" + parent + ".w, f" + sub + ".w, " + kExpr + "));");
    const CombineMode& md = mode();
    const std::string rhs = md.call("f" + parent + ".w", "f" + sub + ".w", kExpr);
    c.appendCall("f" + parent + ".w = " + rhs + ";");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // ONLY K is a [GraphParam] (CombineSDF.cs:250-252). combineMethod is a compile-time selector
    // (NOT packed, like Torus Axis) — packing it would corrupt the float layout and the golden.
    appendScalarParam(floatParams, paramFields, prefix + "K", k);
  }
};

NodeSpec combineSdfSpec() {
  NodeSpec s;
  s.type = "CombineSDF";
  s.title = "Combine SDF";
  // Two Field inputs (named fork (3): TiXL is a variadic MultiInputSlot; the base codegen handles N,
  // the UI here exposes a fixed 2). dataType "Field" keeps them from wiring into Float/Points/Texture2D.
  PortSpec inA; inA.id = "InputA"; inA.name = "Input A"; inA.dataType = "Field"; inA.isInput = true;
  PortSpec inB; inB.id = "InputB"; inB.name = "Input B"; inB.dataType = "Field"; inB.isInput = true;
  // K = the blend radius [GraphParam] float, .t3 default 0.0 (round at r=0 is finite -> still compiles).
  PortSpec kp; kp.id = "K"; kp.name = "K"; kp.dataType = "Float"; kp.isInput = true;
  kp.def = 0.0f; kp.minV = 0.0f; kp.maxV = 10.0f;
  // CombineMethod = enum CODE SELECTOR (drawn as a dropdown, Widget::Enum) — a Float port storing the
  // enum index, .t3 default 2 (UnionRound). NOT a [GraphParam] (never packed); the node's
  // `combineMethod` int member carries it at codegen time. Labels mirror CombineSDF.cs:226-245 by index.
  PortSpec cm; cm.id = "CombineMethod"; cm.name = "Combine Method"; cm.dataType = "Float";
  cm.isInput = true; cm.def = 2.0f; cm.minV = 0.0f; cm.maxV = 12.0f; cm.widget = Widget::Enum;
  cm.labels = {"Union", "UnionSoft", "UnionRound", "UnionChamfer", "UnionSmooth",
               "CutOut", "CutOutRound", "CutOutChamfer",
               "Intersect", "IntersectRound", "IntersectChamfer",
               "Pipe", "Engrave"};
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {inA, inB, kp, cm, out};
  return s;
}

// Factory: build a CombineSDFNode for an instance. K/combineMethod default to the .t3 values; a graph
// cook would override them from the node's params and WIRE inputs[] from the connected children. The
// factory does NOT wire inputs (the golden / cook builds the subtree explicitly).
std::shared_ptr<FieldNode> makeCombineSdf(const std::string& shortId) {
  return std::make_shared<CombineSDFNode>(shortId);
}

// PF-0c param-apply (setter-lambda, NOT offsetof): slot ids == PortSpec.id. K = packed float; CombineMethod
// = enum selector (applyIntSelSlot (int)(v+0.5f); switches MSL text not buffer). Missing key keeps .t3 default.
void configureCombineSdfFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<CombineSDFNode*>(&node)) {
    applyFloatSlot(m, "K", [&](float v) { n->k = v; });
    applyIntSelSlot(m, "CombineMethod", [&](int v) { n->combineMethod = v; });
  }
}

const FieldOp g_combineSdfOp(combineSdfSpec(), makeCombineSdf, configureCombineSdfFromParams);

}  // namespace

// Direct test/cook seam (downcasts inside the owning TU; used by the combinesdf golden): set K (packed
// [GraphParam]) + combineMethod (compile-time selector) on a makeFieldNode("CombineSDF",...) node. No-op if
// `node` is not a CombineSDFNode. (PF-0c's configureCombineSdfFromParams is the map-driven production path.)
void configureCombineSdf(FieldNode& node, float k, int combineMethod) {
  if (auto* n = dynamic_cast<CombineSDFNode*>(&node)) {
    n->k = k;
    n->combineMethod = combineMethod;
  }
}
}  // namespace sw
