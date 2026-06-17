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

  // preShaderCode / postShaderCode: emitted before / after iterating an input field (inputIndex).
  // A leaf SDF writes its `f{c}.w = ...` distance into preShaderCode and has no post code.
  virtual void preShaderCode(CodeAssembleCtx& c, int inputIndex) const = 0;
  virtual void postShaderCode(CodeAssembleCtx&, int /*inputIndex*/) const {}

  // collectParams: append this node's float params (already padded) and a parallel field-layout list.
  // floatParams is the single shared buffer (16-byte aligned per TiXL ShaderParamHandling).
  // paramFields gets one "<type> <prefix><name>" entry per logical param (for the FLOAT_PARAMS hook).
  virtual void collectParams(std::vector<float>& floatParams,
                             std::vector<std::string>& paramFields) const = 0;
};

// ---- assembler (port of GenerateShaderGraphCode.GenerateShaderCode) ------------------------------

struct AssembledField {
  std::string msl;                 // template with all three hooks filled (no /*{...}*/ remaining).
  std::vector<float> floatParams;  // the single packed param buffer (16-byte aligned).
  uint64_t srcHash = 0;            // FNV-1a of `msl` — Build-2's PSO/library cache key (computed, unused here).
};

// Recursively assemble the field tree rooted at `root` into MSL. `templateMsl` is the field render
// template (app/shaders/templates/field_render_template.metal contents) carrying the three hooks
//   /*{GLOBALS}*/  /*{FLOAT_PARAMS}*/  /*{FIELD_CALL}*/
// All three are filled; the result contains no residual /*{...}*/ for those three.
AssembledField assembleFieldMSL(const std::shared_ptr<FieldNode>& root, const std::string& templateMsl);

// FNV-1a 64-bit over a string (srcHash). Exposed so Build-2 keys its cache the same way.
uint64_t fnv1a64(const std::string& s);

// ---- param packing helper (port of ShaderParamHandling padding) ----------------------------------

// Append a float3 with TiXL's 16-byte-alignment padding rule, then return. Used by leaf nodes so
// the packing parity lives in ONE place. (scalar = no padding; float3 pads to not straddle 16B.)
void appendVec3Param(std::vector<float>& v, std::vector<std::string>& fields,
                     const std::string& name, float x, float y, float z);
void appendScalarParam(std::vector<float>& v, std::vector<std::string>& fields,
                       const std::string& name, float value);

// Concrete SDF leaves do NOT live here. Each is a self-contained leaf .cpp (field_ops_<name>.cpp)
// that subclasses FieldNode in an anonymous namespace and registers via the FieldOp seam — this
// header is FROZEN base machinery so a new SDF op = ONE new leaf + a CMakeLists line, nothing
// shared. (Mirrors image_filter_op_registry / value_op_registry. A FieldNode subclass needs no
// cross-TU visibility: the assembler only ever touches the polymorphic base via shared_ptr.)

// --selftest-field-codegen entry (field_graph_selftest.cpp). PURE STRING, zero GPU. fn(bool) -> exit.
int runFieldCodegenSelfTest(bool injectBug);

}  // namespace sw
