// runtime/t3_import_texcompute — the TEXTURE-COMPUTE COLLAPSE pass (TEXTURE_COMPUTE_SEAM_SPEC.md §3).
//
// A texture-OUT compute .t3 (e.g. _ComputeBRDFLookup) is a FIXED framework subgraph: a
// ComputeShaderStage whose Uavs is fed by a UavFromTexture2d that wraps a Texture2d allocator, plus a
// CalcInt2DispatchCount (dispatch = ceil(Size/numthreads)), an ExecuteTextureUpdate forwarder, and a
// ComputeShader (the HLSL Source). None of that framework plumbing exists in sw's tex currency — the
// texture IS its own SRV/UAV, the stage allocates + dispatches + outputs in one cook. So this pass
// COLLAPSES the whole subgraph onto ONE tex-track `ComputeShaderStageTex` atom (fork
// computeshaderstage-splits-by-uav-currency + computestage-allocates-uav-texture), exactly the
// collapse philosophy collapseImageFxWrapper uses for image-fx wrappers.
//
// SHAPE-DETECTED, NOT GUID-KEYED: the match is "a ComputeShaderStage child whose Uavs comes from a
// UavFromTexture2d" — so it is GENERIC across the 66 texture-bound compute compounds, not one root
// guid. STAGE 1 scope: PURE UAV-tex generators only (no SRV-tex read, no extra input children) — an
// SRV-tex compound (SrvFromTexture2d present) or one with non-glue children falls through (return
// false → the normal per-child path), leaving it for stage 2+ of the seam.
//
// runtime leaf (pure CPU JSON walk); split from t3_import.cpp for the rule-4 line ratchet.
#include "runtime/t3_import_internal.h"  // t3i::{lc,asStr,isBoundaryGuid} + collapse decl + applyT3uiPositions

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "crude_json.h"
#include "runtime/compound_graph.h"  // Symbol / SymbolChild / SymbolConnection / SlotDef / kSymbolBoundary
#include "runtime/graph.h"           // NodeSpec / PortSpec / findSpec
#include "runtime/graph_bridge.h"    // atomicSymbolFromSpec
#include "runtime/t3_import_maps.h"  // the tex-compute glue guids

namespace sw {

using t3i::asStr;
using t3i::isBoundaryGuid;
using t3i::lc;

// Test seam (①takeover RED tooth): when set, the collapse is DISABLED → the glue ops fall to the
// normal per-child path (Texture2d/UavFromTexture2d/CalcInt2DispatchCount/ExecuteTextureUpdate are
// unmapped → "unmapped skipped") and the stage imports as the BUFFER ComputeShaderStage, not the tex
// atom → the takeover assertion (zero unmapped + a ComputeShaderStageTex child) reddens. Mirrors
// t3LayoutDisable(). Default false in production.
bool& t3TexComputeCollapseDisable() {
  static bool flag = false;
  return flag;
}

namespace {

// A child's InputValue string by slot guid (Texture2d.Format / ComputeShader.Source).
std::string childInputValueStr(const crude_json::value& child, const char* slotGuid) {
  if (!child["InputValues"].is_array()) return std::string();
  for (const crude_json::value& iv : child["InputValues"].get<crude_json::array>()) {
    if (iv.is_object() && lc(asStr(iv, "Id")) == lc(slotGuid) && iv["Value"].is_string())
      return iv["Value"].get<crude_json::string>();
  }
  return std::string();
}

// ── STAGE 2 CB-trace guids (the value ops that feed FloatsToBuffer.Params in the SRV-tex shape). Baking
//    the b0 CB = tracing each Params wire (in order == cbuffer layout) back to a boundary scalar through
//    these 1-hop value ops. File-local: a tracing detail, not part of the shared guid map. ─────────────
constexpr const char* kIntToFloatSym = "17db8a36-079d-4c83-8a2a-7ea4c1aa49e6";
constexpr const char* kIntToFloatInSlot = "01809b63-4b4a-47be-9588-98d5998ddb0c";       // IntValue
constexpr const char* kBoolToFloatSym = "9db2fcbf-54b9-4222-878b-80d1a0dc6edf";
constexpr const char* kBoolToFloatInSlot = "253b9ae4-fac5-4641-bf0c-d8614606a840";       // BoolValue
constexpr const char* kBoolToFloatFalseSlot = "24ffa0a7-9195-4b38-9c88-37cf4c3afc36";    // ForFalse (def 0)
constexpr const char* kBoolToFloatTrueSlot = "0a53a4ff-4dfb-455a-b70b-0d7eed5e5f22";     // ForTrue (def 1)
constexpr const char* kVec2ComponentsSym = "0946c48b-85d8-4072-8f21-11d17cc6f6cf";
constexpr const char* kVec2ComponentsInSlot = "36f14238-5bb8-4521-9533-f4d1e8fb802b";    // Value (vec2)
constexpr const char* kVec2ComponentsYOut = "305d321d-3334-476a-9fa3-4847912a4c58";      // Y output (X = anything else)

// Read a boundary Input[].DefaultValue as a float. comp: 0 = scalar/vec.X, 1 = vec.Y. bool → 0/1.
float boundaryDefaultFloat(const crude_json::value& root, const std::string& slotId, int comp) {
  if (!root["Inputs"].is_array()) return 0.0f;
  for (const crude_json::value& iv : root["Inputs"].get<crude_json::array>()) {
    if (!iv.is_object() || lc(asStr(iv, "Id")) != lc(slotId)) continue;
    const crude_json::value& dv = iv["DefaultValue"];
    if (dv.is_number()) return (float)dv.get<crude_json::number>();
    if (dv.is_boolean()) return dv.get<crude_json::boolean>() ? 1.0f : 0.0f;
    if (dv.is_object()) {
      const char* k = comp == 1 ? "Y" : "X";
      if (dv[k].is_number()) return (float)dv[k].get<crude_json::number>();
    }
    return 0.0f;
  }
  return 0.0f;
}

// The FIRST wire feeding (dstGuid, dstSlot): out (srcGuid, srcSlot). false if none.
bool wireInto(const crude_json::value& root, const std::string& dstGuid, const std::string& dstSlot,
              std::string& srcGuid, std::string& srcSlot) {
  if (!root["Connections"].is_array()) return false;
  for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
    if (!wv.is_object()) continue;
    if (lc(asStr(wv, "TargetParentOrChildId")) == dstGuid && lc(asStr(wv, "TargetSlotId")) == dstSlot) {
      srcGuid = lc(asStr(wv, "SourceParentOrChildId"));
      srcSlot = lc(asStr(wv, "SourceSlotId"));
      return true;
    }
  }
  return false;
}

// A child's InputValue as a float (numbers/bools), else `def` (BoolToFloat ForTrue/ForFalse fallback).
float childInputValueFloat(const crude_json::value& child, const char* slotGuid, float def) {
  if (!child["InputValues"].is_array()) return def;
  for (const crude_json::value& iv : child["InputValues"].get<crude_json::array>()) {
    if (!iv.is_object() || lc(asStr(iv, "Id")) != lc(slotGuid)) continue;
    if (iv["Value"].is_number()) return (float)iv["Value"].get<crude_json::number>();
    if (iv["Value"].is_boolean()) return iv["Value"].get<crude_json::boolean>() ? 1.0f : 0.0f;
  }
  return def;
}

// The child object with lowercased Id == guid (for reading a value op's InputValues).
const crude_json::value* childByGuid(const crude_json::value& root, const std::string& guid) {
  if (!root["Children"].is_array()) return nullptr;
  for (const crude_json::value& cv : root["Children"].get<crude_json::array>())
    if (cv.is_object() && lc(asStr(cv, "Id")) == guid) return &cv;
  return nullptr;
}

// Resolve ONE FloatsToBuffer.Params source (srcGuid, srcSlot) to a baked CB float, tracing the standard
// scalar value ops one hop to a boundary Input default (fork computestagetex-cb-defaults-baked):
//   • boundary            → the boundary scalar default
//   • IntToFloat          → (float) its IntValue source's boundary int
//   • BoolToFloat         → its BoolValue source's boundary bool ? ForTrue : ForFalse
//   • Vector2Components    → its Value source's boundary vec2 [X|Y] (by which output slot fed Params)
float resolveCbScalar(const crude_json::value& root,
                      const std::map<std::string, std::string>& childSym, const std::string& srcGuid,
                      const std::string& srcSlot) {
  if (isBoundaryGuid(srcGuid)) return boundaryDefaultFloat(root, srcSlot, 0);
  auto sit = childSym.find(srcGuid);
  if (sit == childSym.end()) return 0.0f;
  const std::string& s = sit->second;
  std::string bg, bs;
  if (s == kIntToFloatSym) {
    if (wireInto(root, srcGuid, kIntToFloatInSlot, bg, bs) && isBoundaryGuid(bg))
      return boundaryDefaultFloat(root, bs, 0);
  } else if (s == kBoolToFloatSym) {
    const crude_json::value* cv = childByGuid(root, srcGuid);
    const float fTrue = cv ? childInputValueFloat(*cv, kBoolToFloatTrueSlot, 1.0f) : 1.0f;
    const float fFalse = cv ? childInputValueFloat(*cv, kBoolToFloatFalseSlot, 0.0f) : 0.0f;
    if (wireInto(root, srcGuid, kBoolToFloatInSlot, bg, bs) && isBoundaryGuid(bg))
      return boundaryDefaultFloat(root, bs, 0) != 0.0f ? fTrue : fFalse;
    return fFalse;
  } else if (s == kVec2ComponentsSym) {
    const int comp = (lc(srcSlot) == std::string(kVec2ComponentsYOut)) ? 1 : 0;
    if (wireInto(root, srcGuid, kVec2ComponentsInSlot, bg, bs) && isBoundaryGuid(bg))
      return boundaryDefaultFloat(root, bs, comp);
  }
  return 0.0f;
}

}  // namespace

bool collapseTextureComputeStage(const crude_json::value& root, Symbol& sym, SymbolLibrary& lib,
                                 const std::function<void(const std::string&)>& warn,
                                 const std::string& t3uiJson) {
  if (t3TexComputeCollapseDisable()) return false;  // ①takeover RED tooth
  if (!root["Children"].is_array() || !root["Connections"].is_array()) return false;

  // ── Pass 1: classify children by SymbolId; capture Format + kernel Source; screen out stage 2+. ──
  std::string stageGuid, texture2dGuid, uavGuid, execGuid;
  std::string kernelSource, formatStr;
  bool sawSrv = false;      // SRV-tex read (SrvFromTexture2d) = stage 2 — excludes stage-1 collapse
  bool sawUnknown = false;  // a non-glue child (param/input node) = not a pure generator — excludes
  for (const crude_json::value& cv : root["Children"].get<crude_json::array>()) {
    if (!cv.is_object()) continue;
    const std::string g = lc(asStr(cv, "Id"));
    const std::string sid = lc(asStr(cv, "SymbolId"));
    if (sid == lc(kComputeShaderStageGuid)) stageGuid = g;
    else if (sid == lc(kComputeShaderGuid)) kernelSource = childInputValueStr(cv, kComputeShaderSourceSlot);
    else if (sid == lc(kTexture2dGuid)) { texture2dGuid = g; formatStr = childInputValueStr(cv, kTexture2dFormatSlot); }
    else if (sid == lc(kUavFromTexture2dGuid)) uavGuid = g;
    else if (sid == lc(kCalcInt2DispatchCountGuid)) { /* elide (dispatch derived from output W×H) */ }
    else if (sid == lc(kExecuteTextureUpdateGuid)) execGuid = g;
    else if (sid == lc(kSrvFromTexture2dGuid)) sawSrv = true;
    else sawUnknown = true;
  }
  if (stageGuid.empty() || uavGuid.empty() || texture2dGuid.empty()) return false;  // not this shape
  if (sawSrv || sawUnknown) return false;  // SRV-tex / extra inputs = stage 2+ (not this seam)

  // ── Pass 2: confirm the rail-split signature (UavFromTexture2d.out → Stage.Uavs) + find the compound
  //    output boundary slot (ExecuteTextureUpdate.Output → boundary) + the Size boundary input. ──
  bool uavFeedsStage = false;
  std::string boundaryOutSlot, sizeBoundarySlot;
  for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
    if (!wv.is_object()) continue;
    const std::string sg = lc(asStr(wv, "SourceParentOrChildId"));
    const std::string ss = lc(asStr(wv, "SourceSlotId"));
    const std::string dg = lc(asStr(wv, "TargetParentOrChildId"));
    const std::string ds = lc(asStr(wv, "TargetSlotId"));
    if (sg == uavGuid && ss == lc(kUavFromTexture2dOutSlot) && dg == stageGuid &&
        ds == lc(kComputeStageUavsSlot))
      uavFeedsStage = true;
    if (!execGuid.empty() && sg == execGuid && ss == lc(kExecuteTextureUpdateOutSlot) && isBoundaryGuid(dg))
      boundaryOutSlot = ds;
    if (isBoundaryGuid(sg) && dg == texture2dGuid && ds == lc(kTexture2dSizeSlot)) sizeBoundarySlot = ss;
  }
  if (!uavFeedsStage) return false;  // buffer-UAV stage (or malformed) — not the tex rail
  // Fallback: no ExecuteTextureUpdate forwarder → the stage.Output wires straight to the boundary.
  if (boundaryOutSlot.empty()) {
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      if (lc(asStr(wv, "SourceParentOrChildId")) == stageGuid &&
          lc(asStr(wv, "SourceSlotId")) == lc(kComputeStageOutputSlot) &&
          isBoundaryGuid(lc(asStr(wv, "TargetParentOrChildId")))) {
        boundaryOutSlot = lc(asStr(wv, "TargetSlotId"));
        break;
      }
    }
  }
  if (boundaryOutSlot.empty()) { warn("t3 texcompute: no compound output wire, not collapsed"); return false; }

  // OutW/OutH baked from the boundary Size DefaultValue {X,Y} (fork computestagetex-size-baked-from-
  // texture2d-default: stage-1 resolution is the authored default; live Size re-wiring = stage 4).
  float outW = 512.0f, outH = 512.0f;
  if (!sizeBoundarySlot.empty() && root["Inputs"].is_array()) {
    for (const crude_json::value& iv : root["Inputs"].get<crude_json::array>()) {
      if (!iv.is_object() || lc(asStr(iv, "Id")) != sizeBoundarySlot) continue;
      const crude_json::value& dv = iv["DefaultValue"];
      if (dv.is_object()) {
        if (dv["X"].is_number()) outW = (float)dv["X"].get<crude_json::number>();
        if (dv["Y"].is_number()) outH = (float)dv["Y"].get<crude_json::number>();
      }
    }
  }

  // ── Build the collapsed compound: ONE ComputeShaderStageTex child + one output wire. ──
  const NodeSpec* fs = findSpec("ComputeShaderStageTex");
  if (!fs) { warn("t3 texcompute: no ComputeShaderStageTex spec registered"); return false; }
  if (!lib.symbols.count("ComputeShaderStageTex"))
    lib.symbols["ComputeShaderStageTex"] = atomicSymbolFromSpec(*fs);

  SymbolChild stage;
  stage.id = 1;
  stage.symbolId = "ComputeShaderStageTex";
  if (!kernelSource.empty()) stage.strOverrides["KernelName"] = kernelSource;  // texKernelNameFor at cook
  if (!formatStr.empty()) stage.strOverrides["Format"] = formatStr;            // texFormatFor at cook
  stage.overrides["OutW"] = outW;
  stage.overrides["OutH"] = outH;
  sym.children.push_back(stage);
  sym.nextChildId = 2;

  // Output wire: stage.Output → the boundary output slot (= ExecuteTextureUpdate.Output degenerated
  // to passthrough; fork computeshaderstage-dispatch-in-cook — the stage outputs the written texture).
  SymbolConnection oc;
  oc.srcChild = 1;
  oc.srcSlot = "Output";
  oc.dstChild = kSymbolBoundary;
  oc.dstSlot = boundaryOutSlot;
  sym.connections.push_back(oc);

  // outputDef (Texture2D) so the imported compound has an output PIN + a viewProducerPath (else black cook).
  bool haveOut = false;
  for (const SlotDef& od : sym.outputDefs) if (od.id == boundaryOutSlot) { haveOut = true; break; }
  if (!haveOut) sym.outputDefs.push_back({boundaryOutSlot, "out", "Texture2D", 0.0f});

  // .t3ui LAYOUT: the ONE child inherits the ComputeShaderStage position; boundary Size/output from
  // Input/OutputUis (applyT3uiPositions no-ops when t3uiJson is empty → ④layout N/A, not RED).
  std::map<std::string, int> childGuidToId;
  childGuidToId[stageGuid] = 1;
  applyT3uiPositions(sym, t3uiJson, childGuidToId);
  return true;
}

// SRV-TEX + b0 CB collapse (STAGE 2, TEXTURE_COMPUTE_SEAM_SPEC §5 row 2, _ComputeDepthToLinear shape).
// Unlike the generator shape above, this compound has NO Texture2d allocator — its OutputTexture is a
// BOUNDARY input (the stage self-allocates at cook from the SRV dims), it reads an SRV via SrvFromTexture2d
// (DepthBuffer → Stage.ShaderResources), and carries a b0 CB assembled by FloatsToBuffer from boundary
// floats through value ops. sw collapses the WHOLE subgraph onto ONE ComputeShaderStageTex atom: the SRV
// re-anchors boundary→stage.ShaderResourceTextures, the CB bakes onto CB0..CB{n-1} (fork
// computestagetex-cb-defaults-baked), and GetTextureSize/CalcInt2DispatchCount/SamplerState are ELIDED
// (dispatch derived from the SRV GetDimensions; the kernel indexes the SRV directly, no sampler).
bool collapseTextureComputeStageSrv(const crude_json::value& root, Symbol& sym, SymbolLibrary& lib,
                                    const std::function<void(const std::string&)>& warn,
                                    const std::string& t3uiJson) {
  if (t3TexComputeCollapseDisable()) return false;  // ①takeover RED tooth (shared with the generator)
  if (!root["Children"].is_array() || !root["Connections"].is_array()) return false;

  // ── Pass 1: classify children by SymbolId; every child must be a recognized glue op (else not this
  //    shape → fall through). childSym maps guid → SymbolId for the CB trace. ──
  std::map<std::string, std::string> childSym;
  std::string stageGuid, uavGuid, srvGuid, fbGuid, kernelSource;
  bool sawUnknown = false;
  for (const crude_json::value& cv : root["Children"].get<crude_json::array>()) {
    if (!cv.is_object()) continue;
    const std::string g = lc(asStr(cv, "Id"));
    const std::string sid = lc(asStr(cv, "SymbolId"));
    childSym[g] = sid;
    if (sid == lc(kComputeShaderStageGuid)) stageGuid = g;
    else if (sid == lc(kComputeShaderGuid)) kernelSource = childInputValueStr(cv, kComputeShaderSourceSlot);
    else if (sid == lc(kUavFromTexture2dGuid)) uavGuid = g;
    else if (sid == lc(kSrvFromTexture2dGuid)) srvGuid = g;
    else if (sid == lc(kFloatsToBufferGuid)) fbGuid = g;
    else if (sid == lc(kGetTextureSizeGuid)) { /* elide (dispatch from SRV GetDimensions) */ }
    else if (sid == lc(kCalcInt2DispatchCountGuid)) { /* elide */ }
    else if (sid == lc(kSamplerStateGuid)) { /* elide (kernel indexes SRV directly, no sampler) */ }
    else if (sid == kIntToFloatSym || sid == kBoolToFloatSym || sid == kVec2ComponentsSym) { /* CB source */ }
    else sawUnknown = true;
  }
  if (stageGuid.empty() || uavGuid.empty() || srvGuid.empty()) return false;  // not the SRV-tex shape
  if (sawUnknown) return false;  // an unrecognized child → not this fixed shape

  // ── Pass 2: confirm the signature (SRV.out→Stage.ShaderResources + UAV.out→Stage.Uavs), find the
  //    boundary output slot (Stage.Output→boundary) + the SRV's boundary input (boundary→SrvFromTexture2d). ──
  bool srvFeedsStage = false, uavFeedsStage = false;
  std::string boundaryOutSlot, srvBoundaryInSlot;
  for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
    if (!wv.is_object()) continue;
    const std::string sg = lc(asStr(wv, "SourceParentOrChildId"));
    const std::string ss = lc(asStr(wv, "SourceSlotId"));
    const std::string dg = lc(asStr(wv, "TargetParentOrChildId"));
    const std::string ds = lc(asStr(wv, "TargetSlotId"));
    if (sg == srvGuid && ss == lc(kSrvFromTexture2dOutSlot) && dg == stageGuid &&
        ds == lc(kComputeStageShaderResourcesSlot))
      srvFeedsStage = true;
    if (sg == uavGuid && ss == lc(kUavFromTexture2dOutSlot) && dg == stageGuid &&
        ds == lc(kComputeStageUavsSlot))
      uavFeedsStage = true;
    if (sg == stageGuid && ss == lc(kComputeStageOutputSlot) && isBoundaryGuid(dg)) boundaryOutSlot = ds;
    if (isBoundaryGuid(sg) && dg == srvGuid && ds == lc(kSrvFromTexture2dTexSlot)) srvBoundaryInSlot = ss;
  }
  if (!srvFeedsStage || !uavFeedsStage) return false;  // not the SRV-tex rail signature
  if (boundaryOutSlot.empty()) { warn("t3 texcompute(srv): no compound output wire, not collapsed"); return false; }
  if (srvBoundaryInSlot.empty()) { warn("t3 texcompute(srv): SRV not fed from a boundary input, not collapsed"); return false; }

  // ── CB trace: bake FloatsToBuffer.Params (in wire order == the cbuffer layout) onto CB0..CB{n-1}. ──
  std::vector<float> cb;
  if (!fbGuid.empty()) {
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      if (lc(asStr(wv, "TargetParentOrChildId")) == fbGuid &&
          lc(asStr(wv, "TargetSlotId")) == lc(kFloatsToBufferParamsSlot))
        cb.push_back(resolveCbScalar(root, childSym, lc(asStr(wv, "SourceParentOrChildId")),
                                     lc(asStr(wv, "SourceSlotId"))));
    }
  }

  // ── Build the collapsed compound: ONE ComputeShaderStageTex child + SRV wire + output wire. ──
  const NodeSpec* fs = findSpec("ComputeShaderStageTex");
  if (!fs) { warn("t3 texcompute(srv): no ComputeShaderStageTex spec registered"); return false; }
  if (!lib.symbols.count("ComputeShaderStageTex"))
    lib.symbols["ComputeShaderStageTex"] = atomicSymbolFromSpec(*fs);

  SymbolChild stage;
  stage.id = 1;
  stage.symbolId = "ComputeShaderStageTex";
  if (!kernelSource.empty()) stage.strOverrides["KernelName"] = kernelSource;  // texKernelNameFor at cook
  for (size_t i = 0; i < cb.size() && i < 8; ++i) stage.overrides["CB" + std::to_string(i)] = cb[i];
  sym.children.push_back(stage);
  sym.nextChildId = 2;

  // SRV wire: boundary DepthBuffer → stage.ShaderResourceTextures (SrvFromTexture2d elided). The cook's
  // tex driver gathers this into inputTextures[0]; its GetDimensions drives dispatch + output size.
  SymbolConnection sc;
  sc.srcChild = kSymbolBoundary; sc.srcSlot = srvBoundaryInSlot;
  sc.dstChild = 1; sc.dstSlot = "ShaderResourceTextures";
  sym.connections.push_back(sc);

  // Output wire: stage.Output → boundary output (ExecuteTextureUpdate-less; the stage outputs the texture).
  SymbolConnection oc;
  oc.srcChild = 1; oc.srcSlot = "Output";
  oc.dstChild = kSymbolBoundary; oc.dstSlot = boundaryOutSlot;
  sym.connections.push_back(oc);

  bool haveOut = false;
  for (const SlotDef& od : sym.outputDefs) if (od.id == boundaryOutSlot) { haveOut = true; break; }
  if (!haveOut) sym.outputDefs.push_back({boundaryOutSlot, "out", "Texture2D", 0.0f});

  std::map<std::string, int> childGuidToId;
  childGuidToId[stageGuid] = 1;
  applyT3uiPositions(sym, t3uiJson, childGuidToId);
  return true;
}

// Try every ROOT-collapse path in order (image-fx wrapper first, then texture-compute generator, then the
// SRV-tex + CB shape). Returns true if ANY fired (the caller commits `sym` + returns); false if this root
// is a normal per-child compound. One entry point keeps the importer's collapse hook a single block.
bool tryCollapseRoot(const crude_json::value& root, const std::string& symGuid, Symbol& sym,
                     SymbolLibrary& lib, const std::function<void(const std::string&)>& warn,
                     const std::string& t3uiJson) {
  const std::string collapseType = swTexOpForCollapseRootGuid(symGuid);
  if (!collapseType.empty() && collapseImageFxWrapper(root, collapseType, sym, lib, warn, t3uiJson))
    return true;
  if (collapseTextureComputeStage(root, sym, lib, warn, t3uiJson)) return true;
  return collapseTextureComputeStageSrv(root, sym, lib, warn, t3uiJson);
}

}  // namespace sw
