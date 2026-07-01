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

// collectFieldCode — PUBLIC (declared in field_graph.h). 1:1 port of TiXL
// ShaderGraphNode.CollectEmbeddedShaderCode. Custom-code ops call this on their children; the assembler
// calls it on the root. (Was the anonymous-namespace `collectEmbeddedShaderCode`; promoted to public so
// tryBuildCustomCode ops can recurse their own inputs through the SAME entry — keeping context-id /
// globals / addGlobals ordering identical to the standard path.)
void collectFieldCode(const FieldNode& node, CodeAssembleCtx& cac) {
  // Root pushes the empty context (TiXL: isRoot -> ContextIdStack.Add("")).
  const bool isRoot = cac.contextIdStack.empty();
  if (isRoot) cac.contextIdStack.push_back("");

  node.addGlobals(cac);

  // TiXL ShaderGraphNode.cs:196-199 — a node may emit ALL its code itself (recursing its own inputs,
  // choosing its own context structure). If it does, the standard pre/recurse/post path is skipped.
  if (node.tryBuildCustomCode(cac)) return;

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
        collectFieldCode(*node.inputs[inputFieldIndex], cac);
      node.postShaderCode(cac, static_cast<int>(inputFieldIndex));

      cac.popContext();
      cac.calls.push_back('\n');
    }

    cac.indentCount--;
  } else {
    node.preShaderCode(cac, 0);
    if (node.inputs[0]) collectFieldCode(*node.inputs[0], cac);
    node.postShaderCode(cac, 0);
  }
}

namespace {

void collectAllParams(const FieldNode& node, std::vector<float>& floats,
                      std::vector<std::string>& fields) {
  // TiXL CollectAllNodeParams: inputs first (depth), then this node's params.
  for (const auto& in : node.inputs)
    if (in) collectAllParams(*in, floats, fields);
  node.collectParams(floats, fields);
}

void collectAllTextures(const FieldNode& node, std::vector<TexBinding>& out) {
  // Seam A: same depth-first order as collectAllParams (TiXL AppendShaderResources is collected during
  // the same tree walk). Inputs first, then this node's textures. The index a binding ends up at in
  // `out` IS its [[texture(N)]] slot — depth-first, 0-based — mirroring TiXL's register(tN) allocation.
  for (const auto& in : node.inputs)
    if (in) collectAllTextures(*in, out);
  node.collectTextures(out);
}

void collectAllBuffers(const FieldNode& node, std::vector<BufBinding>& out) {
  // Structured-buffer SRV seam: same depth-first order as collectAllTextures. Inputs first, then this
  // node's buffers. The index a binding lands at in `out` IS its offset from the buffer base slot
  // (BASE + i), mirroring TiXL's register(tN) allocation ordering for structured buffers.
  for (const auto& in : node.inputs)
    if (in) collectAllBuffers(*in, out);
  node.collectBuffers(out);
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
// TiXL PadFloatParametersToVectorComponentCount for size==2: pad currentStart%2 floats so the pair
// starts on an 8-byte (2-float) boundary.
void padForVec2(std::vector<float>& v, std::vector<std::string>& fields) {
  const int requiredPadding = static_cast<int>(v.size()) % 4 % 2;
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

void appendVec2Param(std::vector<float>& v, std::vector<std::string>& fields,
                     const std::string& name, float x, float y) {
  // TiXL AddVec2Parameter: pad to an 8-byte boundary, push x,y, declare a `float2` member. Because the
  // padding lands the pair on a 2-float boundary (offset 0 or 2 mod 4), a plain MSL `float2` (size 8 /
  // align 8) matches the HLSL float2 layout — no packed_float2 needed.
  padForVec2(v, fields);
  v.push_back(x);
  v.push_back(y);
  fields.push_back("float2 " + name);
}

void appendVec4Param(std::vector<float>& v, std::vector<std::string>& fields,
                     const std::string& name, float x, float y, float z, float w) {
  // TiXL PadFloatParametersToVectorComponentCount size==4: pad by (4 - currentStart%4)%4 floats so
  // the float4 starts at a 16-byte boundary. An MSL `float4` struct member (size 16 / align 16)
  // then reads all four components from one aligned slot — no packed_ needed. Used by
  // SetSDFMaterial's Color [GraphParam] (InputSlot<Vector4>).
  const int currentStart = static_cast<int>(v.size()) % 4;
  const int requiredPadding = (4 - currentStart) % 4;
  for (int i = 0; i < requiredPadding; ++i) {
    v.push_back(0.f);
    fields.push_back("float __padding" + std::to_string(v.size()));
  }
  v.push_back(x);
  v.push_back(y);
  v.push_back(z);
  v.push_back(w);
  fields.push_back("float4 " + name);
}

void appendScalarParam(std::vector<float>& v, std::vector<std::string>& fields,
                       const std::string& name, float value) {
  // scalar: no padding (TiXL AddScalarParameter).
  v.push_back(value);
  fields.push_back("float " + name);
}

void appendMat4Param(std::vector<float>& v, std::vector<std::string>& fields, const std::string& name,
                     const float m[16]) {
  // A float4x4 is a 64-byte (4×16B) block; an MSL `float4x4` struct member has alignment 16. We pad the
  // buffer up to a 16-byte (4-float) boundary FIRST (the same discipline as the vec3 padForVec3 rule),
  // then push the 16 floats verbatim and declare a `float4x4` member. TransformField is the sole matrix
  // consumer and its matrix lands after a 4-float-aligned run (the depth-first child params before it
  // total a multiple of 4 in the golden), so the pad is a no-op there; the pad keeps the helper correct
  // for any future graph whose preceding params leave a non-aligned offset. The 16 floats are in the
  // order MSL's float4x4 STRUCT MEMBER reads them (column-major; see field_ops_transformfield.cpp fork (2)).
  const int rem = static_cast<int>(v.size()) % 4;
  if (rem != 0) {
    for (int i = rem; i < 4; ++i) {
      v.push_back(0.f);
      fields.push_back("float __padding" + std::to_string(v.size()));
    }
  }
  for (int i = 0; i < 16; ++i) v.push_back(m[i]);
  fields.push_back("float4x4 " + name);
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
  collectFieldCode(*root, cac);

  // 2. Float params (single buffer) + their field declarations (depth-first, parity order).
  std::vector<std::string> paramFields;
  collectAllParams(*root, out.floatParams, paramFields);

  // 2b. Fragment textures (Seam A), depth-first — index in `textures` == [[texture(N)]] slot. Empty
  //     for every existing SDF leaf (collectTextures default no-op) -> the TEXTURES hook is empty.
  collectAllTextures(*root, out.textures);

  // 2c. Structured-buffer SRVs (point-buffer→field seam), depth-first — index in `buffers` == offset
  //     from the buffer BASE slot. Empty for every existing SDF leaf -> the BUFFERS hook is empty.
  collectAllBuffers(*root, out.buffers);

  // 3. Build the GLOBALS block (helper functions, de-duped by key, in map order).
  std::string globalsBlock = "// --- globals -------------------\n";
  for (const auto& [key, code] : cac.globals) {
    globalsBlock += code;
    globalsBlock += "\n\n";
  }
  globalsBlock += cac.definitions;

  // 3b. Build the STRUCT_DEFS block (resource element-type struct definitions, de-duped by type name,
  //     in map order). Emitted ABOVE the FieldParams struct so a `device const <elemType>*` buffer arg
  //     resolves its element type. EMPTY for every field with no structured buffer -> hook collapses.
  std::string structDefsBlock;
  for (const auto& [key, code] : cac.resourceTypes) {
    structDefsBlock += code;
    structDefsBlock += "\n\n";
  }

  // 4. Build the FLOAT_PARAMS block (struct fields, one per line, tab-indented like TiXL).
  std::string paramsBlock;
  for (const auto& f : paramFields) {
    paramsBlock += "\t";
    paramsBlock += f;
    paramsBlock += ";\n";
  }

  // 4b. Build the TEXTURES block (Seam A): one trailing fragment-arg per collected texture,
  //     `, texture2d<float> P_<declName> [[texture(N)]]`. N = depth-first index (== bind slot). EMPTY
  //     for a zero-texture field, so the hook collapses to nothing and the MSL is byte-identical to a
  //     pre-seam field (the existing 13+ SDF leaves regress not at all). HLSL->MSL FORK (named): TiXL's
  //     SRV `Texture2D<float> {node}SdfImage` at register(tN) becomes an MSL fragment ARG with the
  //     `[[texture(N)]]` attribute — same depth-first 0-based index allocation, only the attribute
  //     syntax differs (same fork class as the cbuffer->`constant Params&` header fork). The `P_`
  //     prefix keeps the arg name off the param struct `P` and unique per leaf (declName already
  //     carries the leaf prefix).
  std::string texturesBlock;   // FRAGMENT signature args (carry the [[texture(N)]] bind attribute).
  std::string texParamsBlock;  // evalField signature params (typed, NO attribute — a plain function arg).
  std::string texArgsBlock;    // evalField call args (forward the fragment's texture into evalField).
  for (size_t i = 0; i < out.textures.size(); ++i) {
    const std::string argName = "P_" + out.textures[i].declName;
    texturesBlock += ",\n                                  texture2d<float> " + argName +
                     " [[texture(" + std::to_string(i) + ")]]";
    texParamsBlock += ", texture2d<float> " + argName;
    texArgsBlock += ", " + argName;
  }

  // 4c. Build the BUFFER blocks (point-buffer→field seam): one trailing fragment-arg per collected
  //     structured buffer, `, device const <elemType>* P_<declName> [[buffer(BASE+i)]]`. BASE = 3 is
  //     PAST the reserved fragment buffers (0=FieldParams in both templates; 1=RaymarchParams,
  //     2=Transforms in the raymarch template). Using a fixed BASE lets ONE assembled arg list serve
  //     BOTH the 2D template (where slots 1,2 are simply unused — Metal permits gaps) and the raymarch
  //     template (where 1,2 are taken). HLSL->MSL FORK (named): TiXL's SRV
  //     `StructuredBuffer<PointTransform> {node}PointTransforms` at register(tN) becomes an MSL fragment
  //     ARG `device const PointTransform* ...` with `[[buffer(BASE+N)]]` — same depth-first index
  //     allocation, only the SRV/buffer syntax differs. The `P_` prefix keeps it off the param struct P
  //     and unique per leaf. EMPTY for a zero-buffer field -> byte-identical to the pre-seam MSL.
  constexpr int kBufferBaseSlot = 3;
  std::string buffersBlock;   // FRAGMENT signature args (carry the [[buffer(BASE+N)]] bind attribute).
  std::string bufParamsBlock; // evalField/getDistance signature params (typed, NO attribute).
  std::string bufArgsBlock;   // evalField/getDistance call args (forward the fragment's buffer inward).
  for (size_t i = 0; i < out.buffers.size(); ++i) {
    const std::string argName = "P_" + out.buffers[i].declName;
    const std::string ty = "device const " + out.buffers[i].elemType + "* " + argName;
    buffersBlock += ",\n                                  " + ty + " [[buffer(" +
                    std::to_string(kBufferBaseSlot + static_cast<int>(i)) + ")]]";
    bufParamsBlock += ", " + ty;
    bufArgsBlock += ", " + argName;
  }

  // 5. Inject the hooks. The three TEXTURE* hooks all derive from the SAME depth-first list, so the
  //    fragment arg, the evalField param, and the evalField call arg name+order line up exactly. All
  //    are EMPTY for a zero-texture field -> byte-identical to the pre-seam MSL.
  std::string code = templateMsl;
  code = injectHook(code, "GLOBALS", globalsBlock);
  code = injectHook(code, "STRUCT_DEFS", structDefsBlock);
  code = injectHook(code, "FLOAT_PARAMS", paramsBlock);
  code = injectHook(code, "FIELD_CALL", cac.calls);
  code = injectHook(code, "TEXTURES", texturesBlock);
  code = injectHook(code, "TEXTURE_PARAMS", texParamsBlock);
  code = injectHook(code, "TEXTURE_ARGS", texArgsBlock);
  code = injectHook(code, "BUFFERS", buffersBlock);
  code = injectHook(code, "BUFFER_PARAMS", bufParamsBlock);
  code = injectHook(code, "BUFFER_ARGS", bufArgsBlock);

  out.msl = code;
  out.srcHash = fnv1a64(out.msl);
  return out;
}

}  // namespace sw
