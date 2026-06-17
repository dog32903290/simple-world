// runtime/field_graph.cpp — see field_graph.h. Pure string codegen; zero Metal.
#include "runtime/field_graph.h"

#include <string>

namespace sw {

// ---- leaf seam storage ---------------------------------------------------------------------------

namespace {
SourceCompileFn g_sourceCompiler = nullptr;
}

void setFieldSourceCompiler(SourceCompileFn fn) { g_sourceCompiler = fn; }
SourceCompileFn fieldSourceCompiler() { return g_sourceCompiler; }

// ---- CodeAssembleCtx (port of CodeAssembleContext.cs) --------------------------------------------

void CodeAssembleCtx::appendCall(const std::string& code) {
  // TiXL: Calls.Append(new string('\t', IndentCount + 1)); Calls.AppendLine(code);
  calls.append(static_cast<size_t>(indentCount + 1), '\t');
  calls.append(code);
  calls.push_back('\n');
}

void CodeAssembleCtx::pushContext(int subContextIndex, const std::string& fieldSuffix) {
  // TiXL CodeAssembleContext.PushContext: seed child p/f locals from the current (parent) context.
  const std::string contextId = ctx();
  const std::string subContextId = std::to_string(subContextIndex) + fieldSuffix;

  contextIdStack.push_back(subContextId);

  appendCall("float4 p" + subContextId + " = p" + contextId + ";");
  appendCall("float4 f" + subContextId + " = f" + contextId + ";");
}

void CodeAssembleCtx::popContext() {
  if (!contextIdStack.empty()) contextIdStack.pop_back();
}

// ---- tree recursion (port of ShaderGraphNode.CollectEmbeddedShaderCode) --------------------------

namespace {

void collectEmbeddedShaderCode(const FieldNode& node, CodeAssembleCtx& cac) {
  // Root pushes the empty context (TiXL: isRoot -> ContextIdStack.Add("")).
  const bool isRoot = cac.contextIdStack.empty();
  if (isRoot) cac.contextIdStack.push_back("");

  node.addGlobals(cac);

  if (node.inputs.empty()) {
    // A node without an input field is a distance function: emit pre+post at field index 0.
    node.preShaderCode(cac, 0);
    node.postShaderCode(cac, 0);
    return;
  }

  const bool requiresSubContext = node.inputs.size() > 1;

  if (requiresSubContext) {
    cac.subContextCount++;
    cac.indentCount++;
    cac.calls.push_back('\n');

    const int subContextIndex = cac.subContextCount;
    for (size_t inputFieldIndex = 0; inputFieldIndex < node.inputs.size(); ++inputFieldIndex) {
      // Suffix 'a','b','c',... per input field (TiXL: (char)('a' + inputFieldIndex)).
      const std::string suffix(1, static_cast<char>('a' + static_cast<int>(inputFieldIndex)));
      cac.pushContext(subContextIndex, suffix);

      node.preShaderCode(cac, static_cast<int>(inputFieldIndex));
      if (node.inputs[inputFieldIndex])
        collectEmbeddedShaderCode(*node.inputs[inputFieldIndex], cac);
      node.postShaderCode(cac, static_cast<int>(inputFieldIndex));

      cac.popContext();
      cac.calls.push_back('\n');
    }

    cac.indentCount--;
  } else {
    node.preShaderCode(cac, 0);
    if (node.inputs[0]) collectEmbeddedShaderCode(*node.inputs[0], cac);
    node.postShaderCode(cac, 0);
  }
}

void collectAllParams(const FieldNode& node, std::vector<float>& floats,
                      std::vector<std::string>& fields) {
  // TiXL CollectAllNodeParams: inputs first (depth), then this node's params.
  for (const auto& in : node.inputs)
    if (in) collectAllParams(*in, floats, fields);
  node.collectParams(floats, fields);
}

// TiXL GenerateShaderGraphCode.TryInject: replace /*{HOOK}*/ with the assembled text.
std::string injectHook(std::string code, const std::string& hookId, const std::string& insert) {
  const std::string tag = "/*{" + hookId + "}*/";
  size_t pos;
  while ((pos = code.find(tag)) != std::string::npos) {
    code.replace(pos, tag.size(), insert);
  }
  return code;
}

}  // namespace

uint64_t fnv1a64(const std::string& s) {
  uint64_t h = 1469598103934665603ULL;  // FNV offset basis
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;  // FNV prime
  }
  return h;
}

// ---- param packing (port of ShaderParamHandling padding) -----------------------------------------

namespace {
// TiXL PadFloatParametersToVectorComponentCount for size==3: pad so the vec3 does not straddle 16B.
void padForVec3(std::vector<float>& v, std::vector<std::string>& fields) {
  const int currentStart = static_cast<int>(v.size()) % 4;
  int requiredPadding = 0;
  if (currentStart == 2)
    requiredPadding = 2;
  else if (currentStart == 3)
    requiredPadding = 1;
  for (int i = 0; i < requiredPadding; ++i) {
    v.push_back(0.f);
    fields.push_back("float __padding" + std::to_string(v.size()));
  }
}
}  // namespace

void appendVec3Param(std::vector<float>& v, std::vector<std::string>& fields,
                     const std::string& name, float x, float y, float z) {
  padForVec3(v, fields);
  v.push_back(x);
  v.push_back(y);
  v.push_back(z);
  // HLSL->MSL ALIGNMENT FORK (load-bearing, found by the Build-2 GPU golden): TiXL's HLSL cbuffer
  // packs `float3 Center; float Radius;` into ONE 16-byte register — Radius sits at byte 12, right
  // after Center. The packed float buffer here follows that (Center=floats[0..2], Radius=floats[3]).
  // BUT in MSL a plain `float3` STRUCT MEMBER has size 16 / alignment 16, so a following `float`
  // would start a NEW 16-byte slot (byte 16) and read garbage/0 — exactly the bug the golden caught
  // (Radius read as 0). `packed_float3` has size 12 / alignment 4, so the next scalar packs at byte
  // 12, reproducing the HLSL cbuffer layout against our tight packed buffer. (metal-cpp-discipline:
  // packed_float3 is THE tool for matching a C/HLSL-side tight float3+scalar layout in MSL.)
  fields.push_back("packed_float3 " + name);
}

void appendScalarParam(std::vector<float>& v, std::vector<std::string>& fields,
                       const std::string& name, float value) {
  // scalar: no padding (TiXL AddScalarParameter).
  v.push_back(value);
  fields.push_back("float " + name);
}

// ---- assembler (port of GenerateShaderGraphCode.GenerateShaderCode) ------------------------------

AssembledField assembleFieldMSL(const std::shared_ptr<FieldNode>& root,
                                const std::string& templateMsl) {
  AssembledField out;
  if (!root) return out;

  // 前置條件：節點 shortId 必唯一（live SymbolChildId 保證）；重複會產生重複 MSL struct member = 編譯錯。
  // 未來 graph-cook 餵非唯一 id 須先去重。
  CodeAssembleCtx cac;

  // 1. Calls + globals + definitions (tree recursion).
  collectEmbeddedShaderCode(*root, cac);

  // 2. Float params (single buffer) + their field declarations (depth-first, parity order).
  std::vector<std::string> paramFields;
  collectAllParams(*root, out.floatParams, paramFields);

  // 3. Build the GLOBALS block (helper functions, de-duped by key, in map order).
  std::string globalsBlock = "// --- globals -------------------\n";
  for (const auto& [key, code] : cac.globals) {
    globalsBlock += code;
    globalsBlock += "\n\n";
  }
  globalsBlock += cac.definitions;

  // 4. Build the FLOAT_PARAMS block (struct fields, one per line, tab-indented like TiXL).
  std::string paramsBlock;
  for (const auto& f : paramFields) {
    paramsBlock += "\t";
    paramsBlock += f;
    paramsBlock += ";\n";
  }

  // 5. Inject the three hooks.
  std::string code = templateMsl;
  code = injectHook(code, "GLOBALS", globalsBlock);
  code = injectHook(code, "FLOAT_PARAMS", paramsBlock);
  code = injectHook(code, "FIELD_CALL", cac.calls);

  out.msl = code;
  out.srcHash = fnv1a64(out.msl);
  return out;
}

// ---- SphereSDF leaf ------------------------------------------------------------------------------

SphereSDFNode::SphereSDFNode(const std::string& shortId) {
  // TiXL BuildNodeId: <TypeName>_<shortGuid>_  — collision-free param prefix.
  prefix = "SphereSDF_" + shortId + "_";
}

void SphereSDFNode::preShaderCode(CodeAssembleCtx& c, int /*inputIndex*/) const {
  // PARITY external/tixl/Operators/Lib/field/generate/sdf/SphereSDF.cs:35-36
  //   c.AppendCall($"f{c}.w = length(p{c}.xyz - {n}Center) - {n}Radius;");
  //   c.AppendCall($"f{c}.xyz = p.w < 0.5 ?  p{c}.xyz : 1;");
  // {n} = node prefix; {c} = context id. `length`, `.xyz`, `float4` are common HLSL/MSL syntax.
  //
  // HLSL->MSL FORK (named): in TiXL the params live in a global-scope HLSL cbuffer, so the snippet
  // reads them as a bare name `{n}Center`. In MSL they live inside the `constant FieldParams& P`
  // argument, so every param read must be qualified `P.{n}Center`. We emit the `P.` prefix here.
  // The distance MATH (length(p.xyz - Center) - Radius) is byte-identical; only the cbuffer-vs-struct
  // access syntax differs — this is the load-bearing HLSL->MSL handoff for Build-2.
  const std::string ctx = c.ctx();
  c.appendCall("f" + ctx + ".w = length(p" + ctx + ".xyz - P." + prefix + "Center) - P." + prefix +
               "Radius;");
  c.appendCall("f" + ctx + ".xyz = p.w < 0.5 ? p" + ctx + ".xyz : float3(1.0); // save local space");
}

void SphereSDFNode::collectParams(std::vector<float>& floatParams,
                                  std::vector<std::string>& paramFields) const {
  // Field-declaration order = Center (vec3) then Radius (scalar), matching SphereSDF.cs reflection
  // order on [GraphParam] fields. Center(3) + Radius(1) = 4 floats = exactly one 16B slot, no padding.
  appendVec3Param(floatParams, paramFields, prefix + "Center", centerX, centerY, centerZ);
  appendScalarParam(floatParams, paramFields, prefix + "Radius", radius);
}

}  // namespace sw
