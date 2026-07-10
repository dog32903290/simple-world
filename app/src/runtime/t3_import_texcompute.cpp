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

}  // namespace sw
