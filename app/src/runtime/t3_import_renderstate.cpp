// runtime/t3_import_renderstate — the Seam 2 RENDER-STATE importer glue (SEAM2_RENDERSTATE_BUILD_PLAN.md,
// node_registry_draw_renderstate.cpp / point_ops_renderstate.cpp). Two jobs, split from t3_import_maps.cpp
// / t3_import.cpp purely for the ARCHITECTURE rule-4 line ratchet:
//
//   1. TABLE ③b/②b (swTypeForRenderStateGuid / swSlotNameForRenderStateGuid): guid->type and (type,slot)->
//      name rows for the 4 DX11 render-state WRAPPER ops (Rasterizer/OutputMergerStage/InputAssemblerStage/
//      Draw — each already has a real sw atom, node_registry_draw_renderstate.cpp) + the S2a MultiInput
//      Command collector (Execute, point_ops_execute.cpp) every render island composes through. Consulted
//      as a FALLBACK from t3_import_maps.cpp's swTypeForSymbolGuid/swSlotNameForGuid — same row shape,
//      just a second table so the big one doesn't cross 400 lines.
//
//   2. THE FOLD (captureIfRenderStateValue / foldRenderStateOntoStages): RasterizerState/DepthStencilState/
//      BlendState/RenderTargetBlendDescription are DX11 "value-authoring" ops with NO matching sw atom —
//      sw folds their fields directly onto the Rasterizer/OutputMerger op that consumes them (the SAME
//      "no separate currency" posture as the ComputeShader Source->KernelName fold already in
//      t3_import.cpp). These 4 guids have NO matching .cs in this vendored TiXL snapshot (external/tixl
//      @395c4c55 — Core/Editor/Operators only, no Types/Gfx assembly); they were recovered EMPIRICALLY by
//      grepping the real .t3 corpus for their inline `/*Name*/` comments + cross-checking every sub-slot
//      guid's wired value shape against 20-70 real consumers (DrawMesh*/GridPlane/Layer2d/postfx/... —
//      external/tixl/Operators/examples ground-truth precedent). RasterizerState/DepthStencilState fold
//      ONE hop (their Output wires straight into Rasterizer.RasterizerState / OutputMergerStage.
//      DepthStencilState); BlendState/RenderTargetBlendDescription fold TWO hops (RTBD --MultiInput-->
//      BlendState --BlendState slot--> OutputMergerStage; BlendState's own AlphaToCoverage/IndependentBlend
//      fields are dormant per census — no sw port — so BlendState itself contributes no fields, only the
//      2-hop PATH).
//
// ★STRUCTURAL FINDING (not fixed here, "如實記錄不硬修" per the batch's scope): a real TiXL render .t3
// does NOT wire Rasterizer/OutputMergerStage/InputAssemblerStage/Draw as a nested Command->Command wrap
// (there is no Command input on any of the 4 — confirmed from their real .cs). It wires their Command
// OUTPUTS as flat SIBLINGS into Execute's MultiInputSlot<Command> (Execute.cs:16 `Command.CollectedInputs`,
// iterated in wire order: prepare-all -> execute-all -> restore-all). So after this batch, Rasterizer/
// OutputMerger/InputAssemblerStage import with an ALWAYS-EMPTY "command" input (cookRasterizer/
// cookOutputMerger's `if (!c.inputCommand) return RenderCommand{};` fires every time) — their stamp never
// reaches Draw's item (a sibling, not a wrapped child) even though Draw's own item DOES survive Execute's
// concat. Import is honest (no crash, no drop) but COOK is a no-op stamp — exactly the "structural gap,
// documented" the batch scoped for. A future seam would need to either teach the S2a cook-core collector
// to treat a MultiInput<Command> subtree as a state-accumulation CHAIN (not just concat), or re-wire the
// importer to synthesize a nested chain from Execute's sibling order. Out of scope here.
//
// runtime leaf (pure CPU JSON walk); split from t3_import_maps.cpp / t3_import.cpp for the line ratchet.
#include "runtime/t3_import_internal.h"  // t3i::{lc,asStr} + RenderStateFoldState + this file's decls

#include <map>
#include <string>
#include <vector>

#include "crude_json.h"
#include "runtime/compound_graph.h"  // SymbolChild

namespace sw {

using t3i::asStr;
using t3i::lc;

// ── guids (empirically recovered — see header rationale above; no .cs source in this TiXL snapshot) ────
namespace {
// The 4 WRAPPER ops + Execute (Table ③b).
const char* const kRasterizerGuid = "fbd7f0f0-36a3-4fbb-91e1-cb33d4666d09";
const char* const kOutputMergerStageGuid = "5efaf208-ba62-42ce-b3df-059b37fc1382";
const char* const kInputAssemblerStageGuid = "9d1266c5-23db-439f-a475-8000fdd1c318";
const char* const kDrawGuid = "9b28e6b9-1d1f-42d8-8a9e-33497b1df820";
const char* const kExecuteGuid = "936e4324-bea2-463a-b196-6064a2d8a6b2";
// The 4 VALUE-AUTHORING ops (never an sw child — folded).
const char* const kRasterizerStateGuid = "c7283335-ef57-46ad-9538-abbade65845a";
const char* const kDepthStencilStateGuid = "04858a08-f0fe-4536-9152-686659f0ab58";
const char* const kBlendStateGuid = "064ca51f-47ab-4cb7-95f2-e537b68e137e";
const char* const kRenderTargetBlendDescriptionGuid = "38ee7546-8d7d-463c-aeea-e482d7ca3f30";
// Load-bearing SLOT guids the fold traces through Connections[] (guid-stable across the whole corpus).
const char* const kRasterizerRasterizerStateSlot = "35a52074-1e82-4352-91c3-d8e464f73bc7";
const char* const kOutputMergerBlendStateSlot = "e0bc9cf8-42c8-4632-b958-7a96f6d03ba2";
const char* const kOutputMergerDepthStencilStateSlot = "1d5faad5-3be5-426c-b464-ad490ea3d1aa";
const char* const kBlendStateRenderTargetsSlot = "63d0e4e8-fa00-4059-a11b-6a31e66757dc";

// RasterizerState sub-slot guids (RasterizerStateDescription fields). Dormant fields (DepthClipEnabled/
// ScissorEnabled/MultiSampleEnabled/AntialiasedLineEnabled — census: no sw port, SEAM2 plan §4 forks) are
// deliberately NOT read here.
const char* const kRsCullMode = "03f3bc7f-3949-4a97-88cf-04e162cfa2f7";
const char* const kRsFillMode = "78c9b432-a2b8-4aea-81e4-0cd5086b3b94";
const char* const kRsFrontCCW = "31319fb4-8663-4908-95b8-e5d5a95f15b2";
const char* const kRsDepthBias = "a2193aa0-e217-4248-a8dc-360cb89a613b";
const char* const kRsSlopeScaledDepthBias = "03c80c25-b0b1-45c2-b67b-60906fe47fbe";
const char* const kRsDepthBiasClamp = "2b53507e-24c3-4895-8928-3400c6acccb6";
// DepthStencilState sub-slots.
const char* const kDsEnableZTest = "956b735b-c38a-4e8e-8186-caf4d36d4d20";
const char* const kDsEnableZWrite = "2342df71-a162-4db7-afc3-514916239897";
const char* const kDsComparison = "27f1f703-7333-49e5-a024-4606e34e8427";
// RenderTargetBlendDescription sub-slots (SourceAlphaBlend/DestinationAlphaBlend/AlphaBlendOperation are
// NOT read: cookOutputMerger hardcodes the alpha channel per the census fork — point_ops_renderstate.cpp).
const char* const kRtbdBlendEnabled = "7f535169-8f65-4186-866d-59c2b89d7da2";
const char* const kRtbdSourceBlend = "56c398ce-fe71-47eb-a33f-11eec8f82e79";
const char* const kRtbdDestinationBlend = "8dc53fe4-79bb-43e4-9d4a-4e06f9a3214c";
const char* const kRtbdBlendOperation = "f56e4281-356a-451a-88f1-9cd8ad95d1a5";
}  // namespace

// ── TABLE ③b / ②b: the 5 wrapper-op rows (fallback for t3_import_maps.cpp) ─────────────────────────────
std::string swTypeForRenderStateGuid(const std::string& guid) {
  static const std::map<std::string, std::string> kTable = {
      {kRasterizerGuid, "Rasterizer"},
      {kOutputMergerStageGuid, "OutputMerger"},
      {kInputAssemblerStageGuid, "InputAssemblerStage"},
      {kDrawGuid, "Draw"},
      {kExecuteGuid, "Execute"},
  };
  auto it = kTable.find(lc(guid));
  return it != kTable.end() ? it->second : std::string();
}

std::string swSlotNameForRenderStateGuid(const std::string& swType, const std::string& slotGuid) {
  // Only slots that HAVE an sw port are rows. RasterizerState/BlendState/DepthStencilState input slots
  // are the FOLD's own target guids (above) and are NOT rows: a wire into them is meant to drop (benign
  // "wire slot unresolved" — the fold already copied the VALUE as an override before this table is ever
  // consulted). Viewports/ScissorRectangles (Rasterizer), InputLayout/VertexBuffers/IndexBuffer
  // (InputAssemblerStage), RTV/UAV/DepthStencilView/DepthStencilReference/BlendFactor/BlendSampleMask
  // (OutputMergerStage) are named FORKS (SEAM2_RENDERSTATE_BUILD_PLAN.md §4) — no sw port, drop.
  static const std::map<std::string, std::map<std::string, std::string>> kTable = {
      {"Rasterizer", {{"c723ad69-ff0c-47b2-9327-bd27c0d7b6d1", "out"}}},
      {"OutputMerger", {{"cee8c3f0-64ea-4e4d-b967-ec7e3688dd03", "out"}}},
      {"InputAssemblerStage",
       {{"18cae035-c050-4f98-9e5e-b3a6db70dda7", "out"},
        {"1ea95430-b853-4a60-a981-f316905995e8", "PrimitiveTopology"}}},
      {"Draw",
       {{"49b28dc3-fcd1-4067-bc83-e1cc848ae55c", "out"},
        {"8716b11a-ef71-437e-9930-bb747da818a7", "VertexCount"},
        {"b381b3ed-f043-4001-9bbc-3e3915f38235", "VertexStartLocation"}}},
      {"Execute",
       {{"e81c99ce-fcee-4e7c-a1c7-0aa3b352b7e1", "out"},
        {"5d73ebe6-9aa0-471a-ae6b-3f5bfd5a0f9c", "Command"},
        {"d68b5569-b43d-4a0d-9524-35289ce08098", "IsEnabled"}}},
  };
  auto t = kTable.find(swType);
  if (t == kTable.end()) return std::string();
  auto s = t->second.find(lc(slotGuid));
  return s != t->second.end() ? s->second : std::string();
}

// ── enum-string / raw-value -> sw scalar-index conversion (closed-form, DX11_METAL_CONVERSION_TABLE.md
// authority; the SAME ordinals cookRasterizer/cookOutputMerger's Dx11* index tables already expect) ────
namespace {
float cullModeIndex(const std::string& s) {  // Rasterizer.CullMode Enum {None,Front,Back}
  if (s == "Front") return 1.0f;
  if (s == "Back") return 2.0f;
  return 0.0f;  // None
}
float fillModeIndexFromInt(double raw) {  // SharpDX FillMode: Wireframe=2,Solid=3 -> sw {Solid,Wireframe}
  return raw == 2.0 ? 1.0f : 0.0f;
}
float compareIndex(const std::string& s) {  // Enum {Never,Less,Equal,LessEqual,Greater,NotEqual,GreaterEqual,Always}
  static const std::map<std::string, float> m = {
      {"Never", 0}, {"Less", 1}, {"Equal", 2}, {"LessEqual", 3},
      {"Greater", 4}, {"NotEqual", 5}, {"GreaterEqual", 6}, {"Always", 7}};
  auto it = m.find(s);
  return it != m.end() ? it->second : 1.0f;  // default Less
}
float blendOptionIndex(const std::string& s) {  // Enum {Zero,One,SrcAlpha,InvSrcAlpha,SrcColor,InvSrcColor,DestColor,InvDestColor,DestAlpha,InvDestAlpha}
  static const std::map<std::string, float> m = {
      {"Zero", 0}, {"One", 1}, {"SourceAlpha", 2}, {"InverseSourceAlpha", 3},
      {"SourceColor", 4}, {"InverseSourceColor", 5}, {"DestinationColor", 6},
      {"InverseDestinationColor", 7}, {"DestinationAlpha", 8}, {"InverseDestinationAlpha", 9}};
  auto it = m.find(s);
  // Bucket-C-ish residuals (SourceAlphaSaturate/BlendFactor/InverseBlendFactor/Source1*) never census-
  // wired (SEAM2_RENDERSTATE_BUILD_PLAN.md §1) -> Zero (the .t3 opaque default) rather than a false pick.
  return it != m.end() ? it->second : 0.0f;
}
float blendOpIndex(const std::string& s) {  // Enum {Add,Subtract,ReverseSubtract,Min,Max}
  static const std::map<std::string, float> m = {
      {"Add", 0}, {"Subtract", 1}, {"ReverseSubtract", 2}, {"Minimum", 3}, {"Maximum", 4}};
  auto it = m.find(s);
  return it != m.end() ? it->second : 0.0f;
}
bool boolVal(const crude_json::value& v) { return v.is_boolean() && v.get<crude_json::boolean>(); }
std::string strVal(const crude_json::value& v) { return v.is_string() ? v.get<crude_json::string>() : std::string(); }
double numVal(const crude_json::value& v) { return v.is_number() ? v.get<crude_json::number>() : 0.0; }

// One child's InputValues[] -> lowercased-slot-guid -> raw Value (each field reads its own shape).
std::map<std::string, crude_json::value> inputValuesByGuid(const crude_json::value& cv) {
  std::map<std::string, crude_json::value> out;
  if (cv["InputValues"].is_array())
    for (const crude_json::value& iv : cv["InputValues"].get<crude_json::array>())
      if (iv.is_object()) out[lc(asStr(iv, "Id"))] = iv["Value"];
  return out;
}
}  // namespace

bool captureIfRenderStateValue(const std::string& symbolId, const std::string& childGuid,
                               const crude_json::value& cv, RenderStateFoldState& state) {
  const std::string sid = lc(symbolId);
  if (sid == lc(kRasterizerStateGuid)) {
    const auto iv = inputValuesByGuid(cv);
    std::map<std::string, float>& f = state.rasterizer[childGuid];
    if (auto it = iv.find(lc(kRsCullMode)); it != iv.end()) f["CullMode"] = cullModeIndex(strVal(it->second));
    if (auto it = iv.find(lc(kRsFillMode)); it != iv.end()) f["FillMode"] = fillModeIndexFromInt(numVal(it->second));
    if (auto it = iv.find(lc(kRsFrontCCW)); it != iv.end()) f["FrontCounterClockwise"] = boolVal(it->second) ? 1.0f : 0.0f;
    if (auto it = iv.find(lc(kRsDepthBias)); it != iv.end()) f["DepthBias"] = (float)numVal(it->second);
    if (auto it = iv.find(lc(kRsSlopeScaledDepthBias)); it != iv.end()) f["SlopeScaledDepthBias"] = (float)numVal(it->second);
    if (auto it = iv.find(lc(kRsDepthBiasClamp)); it != iv.end()) f["DepthBiasClamp"] = (float)numVal(it->second);
    return true;
  }
  if (sid == lc(kDepthStencilStateGuid)) {
    const auto iv = inputValuesByGuid(cv);
    std::map<std::string, float>& f = state.depthStencil[childGuid];
    if (auto it = iv.find(lc(kDsEnableZTest)); it != iv.end()) f["DepthEnable"] = boolVal(it->second) ? 1.0f : 0.0f;
    if (auto it = iv.find(lc(kDsEnableZWrite)); it != iv.end()) f["DepthWrite"] = boolVal(it->second) ? 1.0f : 0.0f;
    if (auto it = iv.find(lc(kDsComparison)); it != iv.end()) f["DepthCompare"] = compareIndex(strVal(it->second));
    return true;
  }
  if (sid == lc(kBlendStateGuid)) {
    state.blendState[childGuid] = true;  // 2-hop pivot only; AlphaToCoverage/IndependentBlend dormant (no sw port)
    return true;
  }
  if (sid == lc(kRenderTargetBlendDescriptionGuid)) {
    const auto iv = inputValuesByGuid(cv);
    std::map<std::string, float>& f = state.rtbd[childGuid];
    if (auto it = iv.find(lc(kRtbdBlendEnabled)); it != iv.end()) f["BlendEnable"] = boolVal(it->second) ? 1.0f : 0.0f;
    if (auto it = iv.find(lc(kRtbdSourceBlend)); it != iv.end()) f["SourceBlend"] = blendOptionIndex(strVal(it->second));
    if (auto it = iv.find(lc(kRtbdDestinationBlend)); it != iv.end()) f["DestinationBlend"] = blendOptionIndex(strVal(it->second));
    if (auto it = iv.find(lc(kRtbdBlendOperation)); it != iv.end()) f["BlendOp"] = blendOpIndex(strVal(it->second));
    return true;
  }
  return false;
}

namespace {
void mergeOverrides(std::vector<SymbolChild>& children, int childId, const std::map<std::string, float>& fields) {
  for (SymbolChild& ch : children)
    if (ch.id == childId) {
      for (const auto& kv : fields) ch.overrides[kv.first] = kv.second;
      return;
    }
}
}  // namespace

void foldRenderStateOntoStages(const crude_json::value& connections,
                               const std::map<std::string, int>& childGuidToId,
                               const std::map<int, std::string>& childIdToSwType,
                               std::vector<SymbolChild>& children, const RenderStateFoldState& state) {
  if (!connections.is_array()) return;

  // BlendState 2-hop trace tables, built in ONE pass over Connections[] (file/wire order — "first RTBD
  // wired into a given BlendState wins" = rt[0], matching TiXL's single-RT-in-practice census).
  std::map<std::string, std::string> blendStateFirstRtbd;  // BlendState guid -> its rt[0] RTBD guid
  std::map<std::string, int> blendStateOMChildId;          // BlendState guid -> the OutputMerger child it feeds

  auto swTypeOfChild = [&](const std::string& guid) -> std::string {
    auto it = childGuidToId.find(guid);
    if (it == childGuidToId.end()) return std::string();
    auto t = childIdToSwType.find(it->second);
    return t != childIdToSwType.end() ? t->second : std::string();
  };

  for (const crude_json::value& wv : connections.get<crude_json::array>()) {
    if (!wv.is_object()) continue;
    const std::string src = lc(asStr(wv, "SourceParentOrChildId"));
    const std::string dst = lc(asStr(wv, "TargetParentOrChildId"));
    const std::string dstSlot = lc(asStr(wv, "TargetSlotId"));

    if (dstSlot == lc(kBlendStateRenderTargetsSlot) && state.blendState.count(dst) && state.rtbd.count(src)) {
      if (!blendStateFirstRtbd.count(dst)) blendStateFirstRtbd[dst] = src;
      continue;
    }
    if (dstSlot == lc(kOutputMergerBlendStateSlot) && state.blendState.count(src)) {
      if (swTypeOfChild(dst) == "OutputMerger") blendStateOMChildId[src] = childGuidToId.at(dst);
      continue;
    }
    if (dstSlot == lc(kRasterizerRasterizerStateSlot) && state.rasterizer.count(src)) {
      if (swTypeOfChild(dst) == "Rasterizer")
        mergeOverrides(children, childGuidToId.at(dst), state.rasterizer.at(src));
      continue;
    }
    if (dstSlot == lc(kOutputMergerDepthStencilStateSlot) && state.depthStencil.count(src)) {
      if (swTypeOfChild(dst) == "OutputMerger")
        mergeOverrides(children, childGuidToId.at(dst), state.depthStencil.at(src));
      continue;
    }
  }

  // Resolve the 2-hop RTBD -> BlendState -> OutputMerger chain now both trace tables are complete.
  for (const auto& kv : blendStateFirstRtbd) {
    auto omIt = blendStateOMChildId.find(kv.first);
    if (omIt == blendStateOMChildId.end()) continue;  // BlendState never reached an OutputMerger — honest drop
    mergeOverrides(children, omIt->second, state.rtbd.at(kv.second));
  }
}

}  // namespace sw
