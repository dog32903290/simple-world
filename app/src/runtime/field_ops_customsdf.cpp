// CustomSDF field op (zero-shared-file leaf on the field self-registration seam). Like SphereSDF /
// PyramidSDF this single .cpp owns BOTH halves of one SDF op: the codegen NODE (CustomSDFNode below)
// AND the OP layer (a NodeSpec for the Add menu / findSpec + a FieldNodeFactory so a graph walk can
// instantiate it by type name), registered via the file-scope FieldOp registrar. The base machinery
// (FieldNode interface, assembleFieldMSL, param packing) stays FROZEN in runtime/field_graph — adding
// a field op = this one .cpp + one CMakeLists line, no shared file edited.
//
// TiXL authority: external/tixl/Operators/Lib/field/generate/sdf/CustomSDF.cs (+ CustomSDF.t3).
//   AddDefinitions (CustomSDF.cs:37-52) writes into c.Definitions (NOT c.Globals) — instance-specific
//   code that references this node's params. It emits, in order:
//     (a) a `#ifndef mod ... #endif` guard (the mod macro),
//     (b) the user's AdditionalDefines string verbatim,
//     (c) `float dCustom{node}(float3 p, float3 Offset, float A, float B, float C) {` ... the user's
//         DistanceFunction string verbatim ... `}` — the user body is injected RAW into the function.
//   GetPreShaderCode (CustomSDF.cs:54-59):
//     f{c}.w = dCustom{node}(p{c}.xyz, {n}Offset, {n}A, {n}B, {n}C);
//     f{c}.xyz = p.w < 0.5 ? p{c}.xyz : 1;
//   [GraphParam] declaration order (CustomSDF.cs:64-78): Offset (Vector3), A (float), B (float),
//     C (float). DistanceFunction / AdditionalDefines are plain InputSlot<string> (NOT [GraphParam],
//     never packed — they drive the emitted CODE, like Iterations/Axis selectors elsewhere).
//   .t3 defaults (CustomSDF.t3): Offset=(0,0,0), A=1.0, B=0.0, C=0.0,
//     DistanceFunction="return length(p - Offset)-A;\n", AdditionalDefines="" — mirrored in the ctor.
//
// ★ DEFINITIONS, not GLOBALS (the load-bearing mechanic): TiXL writes the wrapped function to
//   c.Definitions, which the FROZEN base assembler appends AFTER the de-duped globals block
//   (field_graph.cpp:184 `globalsBlock += cac.definitions;`). The FieldNode interface has no separate
//   addDefinitions hook, but addGlobals(CodeAssembleCtx&) takes the ctx by NON-const ref, so this leaf
//   appends to c.definitions INSIDE addGlobals (same once-per-node call site as global registration).
//   This is NOT a fork — it lands in the same final shader region TiXL's Definitions block does.
//   (Caveat vs the std::map de-dup of Globals: Definitions is a plain string concat, so two CustomSDF
//   nodes emit their dCustom functions twice — but each is keyed by the unique prefix/{node} id, so the
//   two functions have DIFFERENT names and do not collide. The mod guard is `#ifndef`-protected, so
//   repeating it is harmless. This matches TiXL: c.Definitions also concatenates per instance.)
//
// ★ VERBATIM USER-STRING INJECTION (the thing the golden proves): the DistanceFunction body is spliced
//   RAW between `{ ... }`. A malformed string => the runtime newLibrary(source) compile fails =>
//   renderField2d returns null (graceful: the source-PSO cache returns no PSO; field_render.cpp / the
//   golden treat null as a render failure, no crash). The golden feeds a FIXED good body
//   (`return length(p)-r;`-shaped) so the recompile+inject mechanics are what is under test, not the
//   user's authoring.
//
// HLSL->MSL forks honored:
//   (1) cbuffer-vs-struct param access: the PRE-SHADER call reads bare `{n}Name` in TiXL; MSL reads
//       `P.{n}Name` — we emit the `P.` prefix on Offset/A/B/C in the call. (The user's DistanceFunction
//       body references the FUNCTION PARAMETERS `p`/`Offset`/`A`/`B`/`C` — local args, NOT struct
//       members — so the body needs NO P. qualification and is injected byte-verbatim.)
//   (2) bare `p.w` in the save-local-space line is kept verbatim (SphereSDF already ports this bare
//       form; the template seeds p.w=0 so `p.w < 0.5` is true).
//   The mod macro / function-wrapper text is identical in MSL (float3/float types, the
//   #ifndef/#define/#endif preprocessor lines compile in MSL) — NO math fork. The default
//   DistanceFunction `length(p - Offset)-A` is valid MSL as-is.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <memory>
#include <string>
#include <vector>

#include "runtime/field_graph.h"          // FieldNode, CodeAssembleCtx, appendVec3Param/appendScalarParam
#include "runtime/field_node_registry.h"  // FieldOp self-registration

namespace sw {
namespace {

// ---- CustomSDF codegen node (a FieldNode subclass; no cross-TU visibility needed) ----------------

// User-authored distance-function field leaf. Parity: CustomSDF.cs AddDefinitions + GetPreShaderCode +
// CustomSDF.t3 defaults. The four PACKED [GraphParam]s (one vec3 + three scalar) are collected in
// field-declaration order (Offset, A, B, C); `distanceFunction` / `additionalDefines` are code-driving
// strings (NOT packed), held as members and spliced into the Definitions block.
struct CustomSDFNode : FieldNode {
  float offsetX = 0.f, offsetY = 0.f, offsetZ = 0.f;
  float a = 1.0f, b = 0.0f, c = 0.0f;
  // .t3 default DistanceFunction body (the user string injected verbatim). Trailing newline kept to
  // match the .t3 default exactly.
  std::string distanceFunction = "return length(p - Offset)-A;\n";
  std::string additionalDefines = "";  // .t3 default empty

  explicit CustomSDFNode(const std::string& shortId) {
    // TiXL BuildNodeId: <TypeName>_<shortGuid>_ — collision-free param prefix AND the unique dCustom
    // function-name suffix (so two CustomSDF nodes emit distinct dCustom<id> functions).
    prefix = "CustomSDF_" + shortId + "_";
  }

  // Unique function-name token = the node prefix (without the trailing underscore is fine; we keep the
  // full prefix so it cannot collide with another node's). TiXL uses `{ShaderNode}` (the node's id).
  std::string customFnName() const { return "dCustom_" + prefix; }

  void addGlobals(CodeAssembleCtx& c) const override {
    // PARITY CustomSDF.cs:37-52 AddDefinitions — append to c.Definitions (NOT c.Globals; see header).
    // The base appends c.definitions after the de-duped globals block, so this lands in the same final
    // shader region TiXL's Definitions block does.
    //
    // (a) mod guard (#ifndef-protected; harmless if another node's Common global already defined mod).
    c.definitions +=
        "#ifndef mod\n"
        "#define mod(x, y) ((x) - (y) * floor((x) / (y)))\n"
        "#endif\n";
    // (b) the user's AdditionalDefines verbatim, then a newline (TiXL: Append(_defines); AppendLine();).
    c.definitions += additionalDefines;
    c.definitions += "\n";
    // (c) the wrapped function: signature, the user DistanceFunction body RAW, then close brace.
    //   TiXL: $"float dCustom{ShaderNode}(float3 p, float3 Offset, float A, float B, float C)\n{{"
    //         Append(_code); AppendLine(); AppendLine("}");
    c.definitions += "float " + customFnName() +
                     "(float3 p, float3 Offset, float A, float B, float C)\n{\n";
    c.definitions += distanceFunction;  // user body injected VERBATIM
    c.definitions += "\n}\n";
  }

  void preShaderCode(CodeAssembleCtx& cc, int /*inputIndex*/) const override {
    // PARITY CustomSDF.cs:54-59 GetPreShaderCode:
    //   c.AppendCall($"f{c}.w = dCustom{ShaderNode}(p{c}.xyz, {n}Offset, {n}A, {n}B, {n}C);");
    //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
    // {n} = node prefix (qualified P. for MSL struct access on the PACKED params); {c} = context id.
    // The dCustom args are: p{c}.xyz (local), then the four packed params read P.-qualified. The user
    // body inside dCustom references its OWN function args (p/Offset/A/B/C), so no P. there.
    const std::string ctx = cc.ctx();
    cc.appendCall("f" + ctx + ".w = " + customFnName() + "(p" + ctx + ".xyz, P." + prefix + "Offset, P." +
                  prefix + "A, P." + prefix + "B, P." + prefix + "C);");
    cc.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
  }

  void collectParams(std::vector<float>& floatParams,
                     std::vector<std::string>& paramFields) const override {
    // [GraphParam] declaration order = Offset (vec3) -> A (scalar) -> B (scalar) -> C (scalar).
    // DistanceFunction / AdditionalDefines are NOT packed (code-driving strings). Layout:
    //   Offset=floats[0..2] (vec3 start%4==0 -> 0 pad), A=floats[3], B=floats[4], C=floats[5].
    appendVec3Param(floatParams, paramFields, prefix + "Offset", offsetX, offsetY, offsetZ);
    appendScalarParam(floatParams, paramFields, prefix + "A", a);
    appendScalarParam(floatParams, paramFields, prefix + "B", b);
    appendScalarParam(floatParams, paramFields, prefix + "C", c);
  }
};

NodeSpec customSdfSpec() {
  NodeSpec s;
  s.type = "CustomSDF";
  s.title = "Custom SDF";
  // Offset = Vec3 head run (.x/.y/.z), default (0,0,0).
  PortSpec ox; ox.id = "Offset.x"; ox.name = "Offset"; ox.dataType = "Float"; ox.isInput = true;
  ox.def = 0.0f; ox.minV = -10.0f; ox.maxV = 10.0f; ox.widget = Widget::Vec; ox.vecArity = 3;
  PortSpec oy; oy.id = "Offset.y"; oy.name = "Offset.y"; oy.dataType = "Float"; oy.isInput = true;
  oy.def = 0.0f; oy.minV = -10.0f; oy.maxV = 10.0f;
  PortSpec oz; oz.id = "Offset.z"; oz.name = "Offset.z"; oz.dataType = "Float"; oz.isInput = true;
  oz.def = 0.0f; oz.minV = -10.0f; oz.maxV = 10.0f;
  // A, B, C = scalar Floats (.t3 defaults 1.0 / 0.0 / 0.0).
  PortSpec pa; pa.id = "A"; pa.name = "A"; pa.dataType = "Float"; pa.isInput = true;
  pa.def = 1.0f; pa.minV = -10.0f; pa.maxV = 10.0f;
  PortSpec pb; pb.id = "B"; pb.name = "B"; pb.dataType = "Float"; pb.isInput = true;
  pb.def = 0.0f; pb.minV = -10.0f; pb.maxV = 10.0f;
  PortSpec pc; pc.id = "C"; pc.name = "C"; pc.dataType = "Float"; pc.isInput = true;
  pc.def = 0.0f; pc.minV = -10.0f; pc.maxV = 10.0f;
  // DistanceFunction / AdditionalDefines = String code-driving inputs (NOT packed; they re-emit the
  // shader text). dataType "String" keeps them off the Float/Field ports. (No widget — a text field in
  // the inspector; out of scope for this batch's wiring, the golden sets them via configureCustomSdf.)
  PortSpec df; df.id = "DistanceFunction"; df.name = "Distance Function"; df.dataType = "String";
  df.isInput = true;
  PortSpec ad; ad.id = "AdditionalDefines"; ad.name = "Additional Defines"; ad.dataType = "String";
  ad.isInput = true;
  // Output: a Field (ShaderGraphNode in TiXL).
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Field"; out.isInput = false;
  s.ports = {ox, oy, oz, pa, pb, pc, df, ad, out};
  return s;
}

// Factory: build a CustomSDFNode for an instance. All params/strings default to the .t3 values (baked
// in the ctor); a graph cook would override them before assembly (the golden sets them directly via
// configureCustomSdf below).
std::shared_ptr<FieldNode> makeCustomSdf(const std::string& shortId) {
  return std::make_shared<CustomSDFNode>(shortId);
}

const FieldOp g_customSdfOp(customSdfSpec(), makeCustomSdf);

}  // namespace

// Param-cook seam (mirrors configureCombineSdf): set the custom params/strings on a node built via
// makeFieldNode("CustomSDF", ...). The leaf type is TU-private; this free function downcasts inside the
// owning TU so callers (a graph-cook walk; the GPU golden) can override without the type leaking.
// `distanceFunction` / `additionalDefines` are the code-driving strings (re-emit the shader); the
// floats are packed [GraphParam]s. No-op if `node` is not a CustomSDFNode (defensive).
void configureCustomSdf(FieldNode& node, const std::string& distanceFunction,
                        const std::string& additionalDefines, float offsetX, float offsetY,
                        float offsetZ, float a, float b, float c) {
  if (auto* n = dynamic_cast<CustomSDFNode*>(&node)) {
    n->distanceFunction = distanceFunction;
    n->additionalDefines = additionalDefines;
    n->offsetX = offsetX; n->offsetY = offsetY; n->offsetZ = offsetZ;
    n->a = a; n->b = b; n->c = c;
  }
}

}  // namespace sw
