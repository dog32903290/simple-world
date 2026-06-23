// runtime/field_graph — shader-graph CODEGEN (pure string assembly, ZERO Metal).
//
// ZONE: runtime (pure computation). This is sw's port of TiXL's shader-graph code generator
//   external/tixl/Core/DataTypes/ShaderGraphNode.cs        (CollectEmbeddedShaderCode tree recursion)
//   external/tixl/Core/DataTypes/ShaderGraph/CodeAssembleContext.cs (ContextIdStack / Push/Pop / AppendCall)
//   external/tixl/Core/DataTypes/ShaderGraph/ShaderParamHandling.cs  (single float buffer + 16-byte padding)
//   external/tixl/Operators/Lib/field/render/_/GenerateShaderGraphCode.cs (template hook injection)
// It produces a MSL SOURCE STRING + a packed float-param buffer; it never touches Metal. Compiling
// that string is platform/metal_compile's job (the one newLibrary(source) site), wired by app via a
// leaf-seam fn-ptr (SourceCompileFn below) — runtime never includes platform.
//
// HLSL->MSL parity notes (this build does the codegen MECHANISM; SphereSDF's snippet is written in
// MSL; the per-call distance-field body p{c}/f{c}/length() is identical text in HLSL and MSL):
//   - cbuffer{...}            -> the template's /*{FLOAT_PARAMS}*/ block is emitted as struct fields,
//                                wrapped by the template into `constant Params& P [[buffer(N)]]`.
//   - register(b0)            -> [[buffer(N)]] attribute (lives in the template, not the codegen).
//   - node param collision    -> each node prefixes its params with "SphereSDF_<shortId>_" (TiXL
//                                ShaderGraphNode.BuildNodeId); identical-type nodes never collide.
//   - context suffix p{c}/f{c}-> identical to TiXL: root context "", sub-contexts <subIndex><a|b|...>.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sw {

// Leaf seam (ARCHITECTURE.md 葉子接縫): runtime declares the fn-ptr type for "compile an MSL source
// string into a Metal library"; the APP owns platform/metal_compile and registers the implementation
// (app->platform is legal). runtime never includes platform. This build only DECLARES the seam (the
// codegen output is the string + params + srcHash); Build-2 wires it to GPU dispatch.
//   void* = MTL::Library* opaque to runtime (runtime must not name MTL types).
using SourceCompileFn = void* (*)(void* device, const char* msl);
void setFieldSourceCompiler(SourceCompileFn fn);
SourceCompileFn fieldSourceCompiler();

// ---- code-assemble context (port of CodeAssembleContext.cs) ---------------------------------------

// Carried along while recursively collecting a field tree's shader code. Mirrors TiXL field-for-field.
struct CodeAssembleCtx {
  // Globals: pure reusable helper functions, de-duplicated by key (one copy in the final shader).
  std::map<std::string, std::string> globals;
  // Definitions: instance-specific code that may reference unique node params (injected before calls).
  std::string definitions;
  // Calls: the actual distance-function body — the primary target the tree writes to.
  std::string calls;

  // Context id stack. Root is "" (pushed by assembler). Sub-contexts are "<subIndex><a|b|...>".
  std::vector<std::string> contextIdStack;
  int indentCount = 0;
  int subContextCount = 0;

  // Current context id = top of stack ("" when empty). Mirrors CodeAssembleContext.ToString().
  std::string ctx() const { return contextIdStack.empty() ? "" : contextIdStack.back(); }

  // AppendCall: one indented line into `calls`. Indent = (indentCount + 1) tabs, exactly like TiXL.
  void appendCall(const std::string& code);

  // PushContext: opens a sub-scope by declaring fresh p/f locals seeded from the parent context.
  //   float4 p<sub> = p<parent>;
  //   float4 f<sub> = f<parent>;
  void pushContext(int subContextIndex, const std::string& fieldSuffix = "");
  void popContext();
};

// ---- texture binding (sw's port of ShaderGraphNode.SrvBufferReference) ---------------------------

// One fragment-texture an SDF leaf needs bound (the texture-into-field "Seam A"). TiXL declares these
// as HLSL SRVs via IGraphNodeOp.AppendShaderResources (e.g. `Texture2D<float> {node}SdfImage`); the
// codegen collects them depth-first into an ordered list and the host binds them register(t0..tN). The
// sw port mirrors this: a leaf returns its texture decls via collectTextures, the assembler orders
// them, and field_render binds each at [[texture(0..N)]].
//
// PURITY (check-arch: runtime ↛ platform): the texture is held as an OPAQUE `const void*` — runtime
// must NOT name MTL types or include platform. The shell-tier render path (field_render.cpp, which is
// runtime but issues the draw, and may name MTL) casts it back to `MTL::Texture*` at bind time.
//
//   declName  — the MSL fragment-arg name, WITHOUT the leaf's `P_` prefix (the assembler adds `P_`
//               so the texture arg name never collides with the param-struct `P` or another leaf's).
//               E.g. a leaf passes declName = "<prefix>SdfImage".
//   texture   — opaque MTL::Texture* handle, supplied by the cook seam (host) / a future image port.
struct TexBinding {
  std::string declName;          // MSL texture-arg base name (assembler prepends "P_")
  const void* texture = nullptr; // opaque MTL::Texture* (cast back at the bind site only)
};

// ---- field node interface (sw's IGraphNodeOp) ----------------------------------------------------

// A node in the field graph. A leaf SDF (SphereSDF) has no inputs and emits its distance formula in
// preShaderCode. Combiner nodes (later) have inputs and wrap them. This build ships the leaf shape.
struct FieldNode {
  virtual ~FieldNode() = default;

  // Connected input fields (empty for a pure distance leaf like SphereSDF).
  std::vector<std::shared_ptr<FieldNode>> inputs;

  // Unique per-node prefix for param names, e.g. "SphereSDF_a1b2c3_". Set by the node ctor.
  std::string prefix;

  // addGlobals: register reusable helper functions into ctx.globals (de-duped by key). Once per node.
  virtual void addGlobals(CodeAssembleCtx&) const {}

  // tryBuildCustomCode: a node may take over the ENTIRE collect for itself (recursing its own inputs
  // and choosing its own context structure) instead of the standard pre/recurse/post path. Returns true
  // if it emitted the code (the standard recursion is then SKIPPED). 1:1 port of TiXL
  // IGraphNodeOp.TryBuildCustomCode (ShaderGraphNode.cs:196-199): called right after addGlobals, before
  // the InputNodes.Count branch. The DEFAULT IS false — every existing leaf/combiner keeps the standard
  // pre/post path, so the emitted MSL is byte-identical to before this hook existed. Only the ops whose
  // TiXL .cs implements TryBuildCustomCode (PushPullSDF / BlendSDFWithSDF — asymmetric input contexts
  // the uniform multi-input subcontext loop cannot express) override it; they call back into the SAME
  // collectEmbeddedShaderCode for their children via the free function below.
  virtual bool tryBuildCustomCode(CodeAssembleCtx&) const { return false; }

  // preShaderCode / postShaderCode: emitted before / after iterating an input field (inputIndex).
  // A leaf SDF writes its `f{c}.w = ...` distance into preShaderCode and has no post code.
  virtual void preShaderCode(CodeAssembleCtx& c, int inputIndex) const = 0;
  virtual void postShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const {}

  // collectParams: append this node's float params (already padded) and a parallel field-layout list.
  // floatParams is the single shared buffer (16-byte aligned per TiXL ShaderParamHandling).
  // paramFields gets one "<type> <prefix><name>" entry per logical param (for the FLOAT_PARAMS hook).
  virtual void collectParams(std::vector<float>& floatParams,
                             std::vector<std::string>& paramFields) const = 0;

  // collectTextures: append this node's fragment-texture decls (the texture-into-field Seam A). The
  // DEFAULT IS A NO-OP — every existing SDF leaf overrides nothing, so it emits ZERO textures and the
  // assembled MSL is byte-identical to before this seam existed (the /*{TEXTURES}*/ hook collapses to
  // empty for a zero-texture field). Only a texture-consuming leaf (Image2dSDF) overrides this. TiXL
  // parity: IGraphNodeOp.AppendShaderResources — collected depth-first (same order as collectParams).
  virtual void collectTextures(std::vector<TexBinding>&) const {}
};

// ---- assembler (port of GenerateShaderGraphCode.GenerateShaderCode) ------------------------------

struct AssembledField {
  std::string msl;                 // template with all hooks filled (no /*{...}*/ remaining).
  std::vector<float> floatParams;  // the single packed param buffer (16-byte aligned).
  std::vector<TexBinding> textures;// ordered fragment textures (Seam A) — [[texture(0..N)]], depth-first.
  uint64_t srcHash = 0;            // FNV-1a of `msl` — Build-2's PSO/library cache key (computed, unused here).
};

// Recursively assemble the field tree rooted at `root` into MSL. `templateMsl` is the field render
// template (app/shaders/templates/field_render_template.metal contents) carrying the hooks
//   /*{GLOBALS}*/  /*{FLOAT_PARAMS}*/  /*{FIELD_CALL}*/
//   /*{TEXTURES}*/ (fragment args, [[texture(N)]])  /*{TEXTURE_PARAMS}*/ (evalField params, no attr)
//   /*{TEXTURE_ARGS}*/ (evalField call args — forward the texture into evalField)
// All are filled; the result contains no residual /*{...}*/ for them. The three TEXTURE* hooks
// collapse to empty for a zero-texture field (every existing SDF leaf) — byte-identical MSL.
AssembledField assembleFieldMSL(const std::shared_ptr<FieldNode>& root, const std::string& templateMsl);

// FNV-1a 64-bit over a string (srcHash). Exposed so Build-2 keys its cache the same way.
uint64_t fnv1a64(const std::string& s);

// ---- param packing helper (port of ShaderParamHandling padding) ----------------------------------

// Append a float3 with TiXL's 16-byte-alignment padding rule, then return. Used by leaf nodes so
// the packing parity lives in ONE place. (scalar = no padding; float3 pads to not straddle 16B.)
void appendVec3Param(std::vector<float>& v, std::vector<std::string>& fields,
                     const std::string& name, float x, float y, float z);
// Append a float2 with TiXL's size==2 padding rule (AddVec2Parameter: pad currentStart%2 floats so the
// pair starts on an 8-byte boundary). Emitted as a plain MSL `float2` struct member (size 8 / align 8)
// — the padding guarantees an 8-byte-aligned start, so a plain float2 reproduces the HLSL tight layout
// (no packed_float2 needed, unlike the vec3+scalar case). Used by Image2dSDF's Size param.
void appendVec2Param(std::vector<float>& v, std::vector<std::string>& fields,
                     const std::string& name, float x, float y);
void appendScalarParam(std::vector<float>& v, std::vector<std::string>& fields,
                       const std::string& name, float value);
// Append a float4x4 (16 floats, row-major as written into the buffer) and declare a `float4x4` struct
// member. A float4x4 is a tight 64-byte block (4×16B), 16-byte-aligned. TiXL's matrix params live in the
// HLSL cbuffer as a `float4x4`; the packed-float buffer here lays the 16 floats in the SAME order TiXL's
// AdditionalParameters writes them (the C# side has already transposed for HLSL's row-major cbuffer
// layout). Used by TransformField's Transform (a ShaderGraphNode.AdditionalParameters, NOT a [GraphParam]
// — but the packing mechanism is identical: floats into the one shared buffer, a typed struct member).
void appendMat4Param(std::vector<float>& v, std::vector<std::string>& fields,
                     const std::string& name, const float m[16]);

// ---- param-apply slot helpers (PF-0c data-driven field-input projection) -------------------------
//
// Shared apply primitives the per-op slot tables use to project a RESOLVED param map (named-param →
// value, the cook driver's nodeParams output) onto a FieldNode's members. The setter is a LAMBDA, NOT
// offsetof: FieldNode subclasses are non-standard-layout (vtable + std::string/std::vector members), so
// offsetof on a member is UB. Each leaf TU owns the downcast + slot table; these supply the apply rule.
//
// MISSING-KEY CONTRACT (byte-identical guarantee): if the map has no entry for `id`, the setter is NOT
// called → the member keeps its ctor-seeded .t3 default. This is identical to the old per-field `pick`
// fallback (field_ops_toroidalvortexfield.cpp's pick(m,id,current)), so a no-graph-param build assembles
// the byte-identical buffer it did before PF-0c.
//
// Templated on the setter so the lambda captures the (TU-private) leaf pointer with no type erasure and
// no <functional> cost — header-only, packing/assemble untouched.
template <class Setter>
inline void applyFloatSlot(const std::map<std::string, float>& m, const std::string& id, Setter set) {
  auto it = m.find(id);
  if (it != m.end()) set(it->second);
}
// Int selector (an enum index stored as a float): round (int)(v+0.5f), matching the bespoke ToroidalVortex
// Axis read (toroidalvortexfield.cpp:270). Field selectors are ≥0, so the +0.5 round is correct.
template <class Setter>
inline void applyIntSelSlot(const std::map<std::string, float>& m, const std::string& id, Setter set) {
  auto it = m.find(id);
  if (it != m.end()) set(static_cast<int>(it->second + 0.5f));
}
// Bool selector (a bool stored as a float): v > 0.5f.
template <class Setter>
inline void applyBoolSelSlot(const std::map<std::string, float>& m, const std::string& id, Setter set) {
  auto it = m.find(id);
  if (it != m.end()) set(it->second > 0.5f);
}

// Recurse a field subtree's shader code into `cac` (the public entry custom-code ops call to collect
// their OWN children). 1:1 with TiXL ShaderGraphNode.CollectEmbeddedShaderCode: pushes the root context
// if the stack is empty, runs the node's addGlobals, honors tryBuildCustomCode, else drives the standard
// pre/recurse/post path. assembleFieldMSL calls this on the root; a custom-code op calls it on each child.
void collectFieldCode(const FieldNode& node, CodeAssembleCtx& cac);

// Concrete SDF leaves do NOT live here. Each is a self-contained leaf .cpp (field_ops_<name>.cpp)
// that subclasses FieldNode in an anonymous namespace and registers via the FieldOp seam — this
// header is FROZEN base machinery so a new SDF op = ONE new leaf + a CMakeLists line, nothing
// shared. (Mirrors image_filter_op_registry / value_op_registry. A FieldNode subclass needs no
// cross-TU visibility: the assembler only ever touches the polymorphic base via shared_ptr.)

// --selftest-field-codegen entry (field_graph_selftest.cpp). PURE STRING, zero GPU. fn(bool) -> exit.
int runFieldCodegenSelfTest(bool injectBug);

}  // namespace sw
