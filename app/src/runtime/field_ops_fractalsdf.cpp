// FractalSDF field op (zero-shared-file leaf on the field self-registration seam). Like SphereSDF /
// PyramidSDF this single .cpp owns BOTH halves of one SDF op: the codegen NODE (FractalSDFNode below)
// AND the OP layer (a NodeSpec for the Add menu / findSpec + a FieldNodeFactory so a graph walk can
// instantiate it by type name), registered via the file-scope FieldOp registrar. The base machinery
// (FieldNode interface, assembleFieldMSL, param packing) stays FROZEN in runtime/field_graph — adding
// a field op = this one .cpp + one CMakeLists line, no shared file edited.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/FractalSDF.cs (+ FractalSDF.t3).
//   AddDefinitions registers Globals["MandelBulbFractal"] = the fMandelBulbFractal Mandelbulb-fold
//     helper (FractalSDF.cs:36-69) — a GLOBAL helper via addGlobals (like fPyramid), the for-loop
//     lives INSIDE the helper body string.
//   GetPreShaderCode (FractalSDF.cs:72-77):
//     f{c}.w = fMandelBulbFractal(p{c}.xyz, {n}Scale, {n}Clamping, {n}Increment, {n}Minrad, {n}Fold,
//                                 clamp({n}Iterations,1,20));
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//   [GraphParam] declaration order (FractalSDF.cs:81-103): Scale (float), Minrad (float),
//     Clamping (Vector3), Increment (Vector3), Fold (Vector2), Iterations (int).
//   .t3 defaults (FractalSDF.t3): Scale=2.0, Minrad=0.303, Clamping=(0,0,0), Increment=(0,0,0),
//     Fold=(0.5,1.0), Iterations=8 — mirrored in the ctor.
//
// ★ ITERATIONS = COMPILE-TIME CODE SELECTOR (NAMED FORK from TiXL), the load-bearing seam of this leaf.
//   FORK (1) named, load-bearing: in TiXL `Iterations` IS [GraphParam]+Slot<int>, so it is packed into
//   the float buffer (ShaderParamHandling.cs:91-101 -> AddScalarParameter, declared `int {n}Iterations`)
//   AND read at runtime as `clamp({n}Iterations,1,20)` (the dynamic loop bound `for(i<iterations)`),
//   while FractalSDF.Update() ALSO calls ShaderNode.FlagCodeChanged() when it changes. The orchestrator
//   chose to port Iterations as a PURE compile-time selector (mirror CombineSDF.combineMethod): we BAKE
//   the clamped iteration count as a LITERAL into the emitted helper-call AND specialize the helper body
//   for that fixed count, so the loop bound is a compile-time constant and Iterations never enters the
//   packed float buffer. Consequence (the fork): a different Iterations value re-emits different MSL
//   text -> different srcHash -> recompile (exactly CombineSDF.combineMethod's path), instead of TiXL's
//   single PSO with a runtime int uniform. The distance MATH for any fixed iteration count is byte-
//   identical to TiXL's loop run for that count (same folds, same final estimator). clamp(Iterations,1,20)
//   is applied HOST-side here (the same Clamp(1,20) TiXL applies in Update(), FractalSDF.cs:22).
//
// HLSL->MSL forks honored:
//   (2) cbuffer-vs-struct param access: TiXL reads bare `{n}Name` from a global cbuffer; MSL reads
//       `P.{n}Name` from the `constant FieldParams& P` argument — we emit the `P.` prefix in the call.
//   (3) bare `p.w` in the save-local-space line is kept verbatim (SphereSDF already ports this bare
//       form; the template seeds p.w=0 so `p.w < 0.5` is true).
//   The helper body (abs/clamp/dot/pow/length, all identical in MSL; float4/float3 literals valid in
//   MSL) is byte-verbatim from the .cs EXCEPT the loop bound is the baked literal (the Iterations fork)
//   — NO other math fork.
//
// ★ vec2 Fold packing: the FROZEN base exposes appendVec3Param / appendScalarParam but NOT a vec2
//   helper. Rather than touch the base, this leaf replicates TiXL's AddVec2Parameter padding rule
//   (ShaderParamHandling.cs:115-120 + PadFloatParametersToVectorComponentCount size==2,
//   currentStart%2 pad) in a file-scope helper appendVec2ParamLocal below. MSL `float2` struct member
//   is size 8 / align 8; after the size==2 pad the two Fold floats land 8-byte-aligned, matching the
//   HLSL cbuffer float2 slot — no packed_ qualifier needed (unlike float3, which needs packed_float3).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <algorithm>  // std::clamp (host-side Iterations clamp, parity with TiXL .Clamp(1,20))
#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- file-scope vec2 param packer (base has no appendVec2Param; do NOT edit the frozen base) -------
// VERBATIM port of TiXL ShaderParamHandling.AddVec2Parameter (ShaderParamHandling.cs:115-120) +
// PadFloatParametersToVectorComponentCount size==2 (line 172-175: requiredPadding = currentStart % 2).
// Emits the pad floats (named like the base's __padding<size>) then the two Fold floats as `float2`.
void appendVec2ParamLocal(std::vector<float>& v, std::vector<std::string>& fields,
                          const std::string& name, float x, float y) {
  const int currentStart = static_cast<int>(v.size()) % 4;
  const int requiredPadding = currentStart % 2;  // TiXL size==2 rule
  for (int i = 0; i < requiredPadding; ++i) {
    v.push_back(0.f);
    fields.push_back("float __padding" + std::to_string(v.size()));
  }
  v.push_back(x);
  v.push_back(y);
  fields.push_back("float2 " + name);
}

// ---- FractalSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) ---------------

// Distance-to-Mandelbulb-fractal field leaf. Parity: FractalSDF.cs AddDefinitions + GetPreShaderCode +
// FractalSDF.t3 defaults. The five PACKED [GraphParam]s (two scalar + two vec3 + one vec2) are collected
// in field-declaration order (Scale, Minrad, Clamping, Increment, Fold); `iterations` is the compile-
// time code selector (see header ITERATIONS note) held as an int member, NOT packed.
struct FractalSDFNode : FieldNode {
  float scale = 2.0f;
  float minrad = 0.303f;
  float clampingX = 0.f, clampingY = 0.f, clampingZ = 0.f;
  float incrementX = 0.f, incrementY = 0.f, incrementZ = 0.f;
  float foldX = 0.5f, foldY = 1.0f;
  // Iterations code selector — .t3 default 8. Set in the ctor; the golden uses this default. clamped to
  // [1,20] host-side at codegen (TiXL Update(): Iterations.GetValue(context).Clamp(1,20)).
  int iterations = 8;

  explicit FractalSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix.
    prefix = "FractalSDF_" + shortId + "_";
  }

  int clampedIterations() const { return std::clamp(iterations, 1, 20); }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY FractalSDF.cs:34-70 AddDefinitions: c.Globals["MandelBulbFractal"] = fMandelBulbFractal.
    // De-duped by key (std::map::operator[]). Body byte-verbatim from the .cs EXCEPT the loop bound is
    // the baked literal `clampedIterations()` (the Iterations compile-time-selector fork). float4/float3
    // ctor literals, abs/clamp/dot/max/pow/length are all identical in MSL — no math fork.
    //
    // The for-loop `for (int i = 0; i < <N>; i++)` uses the baked literal N. The `iterations` function
    // parameter from the .cs (used only as the loop bound AND in `pow(.., 1 - iterations)`) is REPLACED
    // by the literal: the helper signature drops the trailing `int iterations` arg and the body uses N
    // for both the loop bound and the `1 - N` exponent. (Mirrors TiXL where iterations==clamp value at
    // the call; we resolve it at codegen instead of at runtime.)
    const int N = clampedIterations();
    const std::string n = std::to_string(N);
    c.globals["MandelBulbFractal"] =
        "float fMandelBulbFractal(float3 pos, float scale1, float3 clamping, float3 increment, float minrad, float2 fold) {\n"
        "    float4 pN = float4(pos, 1);\n"
        "    // return dStillLogo(pN);\n"
        "\n"
        "    // precomputed constants\n"
        "    float minRad2 = clamp(minrad, 1.0e-9, 1.0);\n"
        // HLSL->MSL FORK (4) named, load-bearing: TiXL's HLSL `float4(scale1.xxx, abs(scale1))` swizzles
        // a SCALAR float with `.xxx` (HLSL broadcasts a scalar to float3). MSL does NOT allow `.xxx` on a
        // scalar -> "member reference base type 'float' is not a structure or union". Rewrite as
        // `float3(scale1)` (the explicit broadcast) — VALUE-IDENTICAL to scale1.xxx (=(scale1,scale1,
        // scale1)). Zero math change; only the scalar-broadcast syntax differs.
        "    float4 scale = float4(float3(scale1), abs(scale1)) / minRad2;\n"
        "    float absScalem1 = abs(scale1 - 1.0);\n"
        "    float AbsScaleRaisedTo1mIters = pow(abs(scale1), float(1 - " + n + "));\n"
        "    //float DIST_MULTIPLIER = StepSize;\n"
        "\n"
        "    float4 p = float4(pos, 1);\n"
        "    float4 p0 = p; // p.w is the distance estimate\n"
        "\n"
        "    for (int i = 0; i < " + n + "; i++)\n"
        "    {\n"
        "        // box folding:\n"
        "        p.xyz = abs(1 + p.xyz) - p.xyz - abs(1.0 - p.xyz);\n"
        "        p.xyz = clamp(p.xyz, clamping.x, clamping.y) * clamping.z - p.xyz;\n"
        "\n"
        "        // sphere folding:\n"
        "        float r2 = dot(p.xyz, p.xyz);\n"
        "        p *= clamp(max(minRad2 / r2, minRad2), fold.x, fold.y);\n"
        "        p.xyz += float3(increment.x, increment.y, increment.z);\n"
        "        // scale, translate\n"
        "        p = p * scale + p0;\n"
        "    }\n"
        "    float d = ((length(p.xyz) - absScalem1) / p.w - AbsScaleRaisedTo1mIters);\n"
        "    return d;\n"
        "}";
  }

  void preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const override {
    // PARITY FractalSDF.cs:72-77 GetPreShaderCode:
    //   c.AppendCall($"f{c}.w = fMandelBulbFractal(p{c}.xyz, {n}Scale, {n}Clamping, {n}Increment,
    //                   {n}Minrad, {n}Fold, clamp({n}Iterations,1,20));");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // {n} = node prefix (qualified P. for MSL struct access); {c} = context id. The trailing
    // `clamp({n}Iterations,1,20)` argument is DROPPED here (Iterations is baked into the helper body as
    // a literal — the fork). All other args are the packed params, read P.-qualified.
    const std::string ctx = c.ctx();
    c.appendCall("f" + ctx + ".w = fMandelBulbFractal(p" + ctx + ".xyz, P." + prefix + "Scale, P." +
                 prefix + "Clamping, P." + prefix + "Increment, P." + prefix + "Minrad, P." + prefix +
                 "Fold);");
    c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] declaration order = Scale (scalar) -> Minrad (scalar) -> Clamping (vec3) ->
    // Increment (vec3) -> Fold (vec2). Iterations is NOT packed (compile-time selector). Layout traced:
    //   Scale=floats[0], Minrad=floats[1], pad floats[2..3] (vec3 start%4==2 -> pad 2),
    //   Clamping=floats[4..6], pad floats[7] (vec3 start%4==3 -> pad 1), Increment=floats[8..10],
    //   pad floats[11] (vec2 start%4==3 -> pad 1), Fold=floats[12..13]. Total 14 floats.
    // padForVec3 (inside appendVec3Param) and appendVec2ParamLocal insert the pads automatically.
    appendScalarParam(floatParams, paramFields, prefix + "Scale", scale);
    appendScalarParam(floatParams, paramFields, prefix + "Minrad", minrad);
    appendVec3Param(floatParams, paramFields, prefix + "Clamping", clampingX, clampingY, clampingZ);
    appendVec3Param(floatParams, paramFields, prefix + "Increment", incrementX, incrementY, incrementZ);
    appendVec2ParamLocal(floatParams, paramFields, prefix + "Fold", foldX, foldY);
  }
};

NodeSpec fractalSdfSpec() {
  NodeSpec s;
  s.type = "FractalSDF";
  s.title = "Fractal SDF";
  // Scale, Minrad = scalar Floats (.t3 defaults 2.0 / 0.303).
  PortSpec sc; sc.id = "Scale"; sc.name = "Scale"; sc.dataType = "Float"; sc.isInput = true;
  sc.def = 2.0f; sc.minV = -10.0f; sc.maxV = 10.0f;
  PortSpec mr; mr.id = "Minrad"; mr.name = "Minrad"; mr.dataType = "Float"; mr.isInput = true;
  mr.def = 0.303f; mr.minV = 0.0f; mr.maxV = 1.0f;
  // Clamping = Vec3 head run (.x/.y/.z), default (0,0,0).
  PortSpec clx; clx.id = "Clamping.x"; clx.name = "Clamping"; clx.dataType = "Float"; clx.isInput = true;
  clx.def = 0.0f; clx.minV = -10.0f; clx.maxV = 10.0f; clx.widget = Widget::Vec; clx.vecArity = 3;
  PortSpec cly; cly.id = "Clamping.y"; cly.name = "Clamping.y"; cly.dataType = "Float"; cly.isInput = true;
  cly.def = 0.0f; cly.minV = -10.0f; cly.maxV = 10.0f;
  PortSpec clz; clz.id = "Clamping.z"; clz.name = "Clamping.z"; clz.dataType = "Float"; clz.isInput = true;
  clz.def = 0.0f; clz.minV = -10.0f; clz.maxV = 10.0f;
  // Increment = Vec3 head run (.x/.y/.z), default (0,0,0).
  PortSpec inx; inx.id = "Increment.x"; inx.name = "Increment"; inx.dataType = "Float"; inx.isInput = true;
  inx.def = 0.0f; inx.minV = -10.0f; inx.maxV = 10.0f; inx.widget = Widget::Vec; inx.vecArity = 3;
  PortSpec iny; iny.id = "Increment.y"; iny.name = "Increment.y"; iny.dataType = "Float"; iny.isInput = true;
  iny.def = 0.0f; iny.minV = -10.0f; iny.maxV = 10.0f;
  PortSpec inz; inz.id = "Increment.z"; inz.name = "Increment.z"; inz.dataType = "Float"; inz.isInput = true;
  inz.def = 0.0f; inz.minV = -10.0f; inz.maxV = 10.0f;
  // Fold = Vec2 head run (.x/.y), default (0.5, 1.0).
  PortSpec fx; fx.id = "Fold.x"; fx.name = "Fold"; fx.dataType = "Float"; fx.isInput = true;
  fx.def = 0.5f; fx.minV = -10.0f; fx.maxV = 10.0f; fx.widget = Widget::Vec; fx.vecArity = 2;
  PortSpec fy; fy.id = "Fold.y"; fy.name = "Fold.y"; fy.dataType = "Float"; fy.isInput = true;
  fy.def = 1.0f; fy.minV = -10.0f; fy.maxV = 10.0f;
  // Iterations = INT code selector (the dynamic fold count). It is a Float port (storing the int value),
  // drawn as a plain scalar slider (no Widget::Int exists; the range [1,20] is integer-ish). NOT a
  // [GraphParam] that gets packed in TiXL's spirit (here it is the compile-time selector; the node's
  // `iterations` int member carries it at codegen time, host-clamped to [1,20]). .t3 default 8.
  PortSpec it; it.id = "Iterations"; it.name = "Iterations"; it.dataType = "Float"; it.isInput = true;
  it.def = 8.0f; it.minV = 1.0f; it.maxV = 20.0f;
  // Output: a Field (ShaderGraphNode in TiXL) — the connectable result other field ops / Render2dField
  // consume. dataType "Field" keeps it from wiring into Float/Points/Texture2D ports by mistake.
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {sc, mr, clx, cly, clz, inx, iny, inz, fx, fy, it, out};
  return s;
}

// Factory: build a FractalSDFNode for an instance. All params default to the .t3 values (baked in the
// ctor); a graph cook would override them from the node's params before assembly (the golden uses the
// defaults / sets them directly via configureFractalSdf below).
std::shared_ptr<FieldNode> makeFractalSdf(const std::string& shortId) {
  return std::make_shared<FractalSDFNode>(shortId);
}

// PF-0c param-apply (WAVE 2): project a RESOLVED param map onto a FractalSDFNode via setter-lambdas
// (NOT offsetof). Slot ids EQUAL the NodeSpec PortSpec.id for the FIVE PACKED [GraphParam]s only:
// Scale, Minrad, Clamping.x/.y/.z, Increment.x/.y/.z, Fold.x/.y. `Iterations` is DELIBERATELY NOT a
// float slot: it is the compile-time CODE selector (the loop bound baked into the helper body, like
// CombineSDF.combineMethod) — a different value re-emits MSL text, it never enters the packed float
// buffer, so a float round-trip would be meaningless. Iterations still flows via the positional
// configureFractalSdf seam (and is wave-3 text-assertion territory). A missing key keeps the member's
// ctor .t3 default. Routed via the fieldConfigurers() table.
void configureFractalSdfFromParams(FieldNode& node, const std::map<std::string, float>& m) {
  if (auto* n = dynamic_cast<FractalSDFNode*>(&node)) {
    applyFloatSlot(m, "Scale", [&](float v) { n->scale = v; });
    applyFloatSlot(m, "Minrad", [&](float v) { n->minrad = v; });
    applyFloatSlot(m, "Clamping.x", [&](float v) { n->clampingX = v; });
    applyFloatSlot(m, "Clamping.y", [&](float v) { n->clampingY = v; });
    applyFloatSlot(m, "Clamping.z", [&](float v) { n->clampingZ = v; });
    applyFloatSlot(m, "Increment.x", [&](float v) { n->incrementX = v; });
    applyFloatSlot(m, "Increment.y", [&](float v) { n->incrementY = v; });
    applyFloatSlot(m, "Increment.z", [&](float v) { n->incrementZ = v; });
    applyFloatSlot(m, "Fold.x", [&](float v) { n->foldX = v; });
    applyFloatSlot(m, "Fold.y", [&](float v) { n->foldY = v; });
  }
}

// slot ids = the SAME ids configureFractalSdfFromParams applies (Option B guard reads them). Iterations
// is excluded (compile-time code selector, not packed).
const FieldOp g_fractalSdfOp(fractalSdfSpec(), makeFractalSdf, configureFractalSdfFromParams,
                             {"Scale", "Minrad", "Clamping.x", "Clamping.y", "Clamping.z", "Increment.x",
                              "Increment.y", "Increment.z", "Fold.x", "Fold.y"});

}  // namespace

// Param-cook seam (mirrors configureCombineSdf): set the fractal params on a node built via
// makeFieldNode("FractalSDF", ...). The leaf type is TU-private; this free function downcasts inside
// the owning TU so callers (a graph-cook walk; the GPU golden) can override the params without the
// type leaking. `iterations` is the compile-time code selector; the floats are packed [GraphParam]s.
// No-op if `node` is not a FractalSDFNode (defensive; the caller passes this op's factory output).
void configureFractalSdf(FieldNode& node, float scale, float minrad, float clampingX, float clampingY,
                         float clampingZ, float incrementX, float incrementY, float incrementZ,
                         float foldX, float foldY, int iterations) {
  if (auto* n = dynamic_cast<FractalSDFNode*>(&node)) {
    n->scale = scale;
    n->minrad = minrad;
    n->clampingX = clampingX; n->clampingY = clampingY; n->clampingZ = clampingZ;
    n->incrementX = incrementX; n->incrementY = incrementY; n->incrementZ = incrementZ;
    n->foldX = foldX; n->foldY = foldY;
    n->iterations = iterations;
  }
}

}  // namespace sw
