// runtime/t3_import_srvtexfold — BUFFER-RAIL SrvFromTexture2d fold (TEXTURE_COMPUTE_SEAM_SPEC §5 row 3 /
// §2). Split from t3_import.cpp / t3_import_texcompute.cpp for the ≤400-line ratchet (ARCHITECTURE rule 4);
// a distinct importer responsibility (the tex-compute COLLAPSE folds a whole subgraph to ONE atom, this
// fold KEEPS the buffer stage as a child and only re-anchors its texture-SRV input). Pure CPU JSON walk.
//
// A buffer-OUT ComputeShaderStage that reads a Texture2D SRV wires SrvFromTexture2d.out →
// Stage.ShaderResources (DX11 packs buffer-SRV + texture-SRV into ONE t-space). sw has no SRV-view currency
// (the texture IS its own SRV) and splits the two onto separate Metal spaces, so the importer ELIDES
// SrvFromTexture2d and re-anchors its TEXTURE source onto the stage's ShaderResourceTextures port (fork
// srvfromtexture2d-folds-onto-shaderresourcetextures). The cook side already gathers ShaderResourceTextures
// cross-currency (getimagebrightness stage-3 seal) — this closes the missing JSON collapse.
#include "runtime/t3_import_internal.h"  // t3i::{lc,asStr,isBoundaryGuid} + appendSrvFromTexFold decl

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "crude_json.h"
#include "runtime/compound_graph.h"  // SymbolConnection / kSymbolBoundary
#include "runtime/t3_import.h"        // t3SrvTexFoldDisable()
#include "runtime/t3_import_maps.h"   // kSrvFromTexture2d* guids + swSlotNameForGuid

namespace sw {

using t3i::asStr;
using t3i::isBoundaryGuid;
using t3i::lc;

// Test seam (buffer-rail SrvFromTexture2d fold RED tooth): when set, appendSrvFromTexFold is a no-op →
// SrvFromTexture2d stays an unmapped child, its texture-SRV wire drops, the buffer stage never gets a
// ShaderResourceTextures input. Mirrors t3TexComputeCollapseDisable(). Default false in production.
bool& t3SrvTexFoldDisable() {
  static bool flag = false;
  return flag;
}

void appendSrvFromTexFold(const crude_json::value& root,
                          const std::map<std::string, int>& childGuidToId,
                          const std::map<int, std::string>& childIdToSwType,
                          std::vector<SymbolConnection>& conns,
                          const std::function<void(const std::string&)>& warn) {
  if (t3SrvTexFoldDisable()) return;
  if (!root["Children"].is_array() || !root["Connections"].is_array()) return;

  std::set<std::string> srvGuids;  // the SrvFromTexture2d children (elided glue)
  for (const crude_json::value& cv : root["Children"].get<crude_json::array>())
    if (cv.is_object() && lc(asStr(cv, "SymbolId")) == lc(kSrvFromTexture2dGuid))
      srvGuids.insert(lc(asStr(cv, "Id")));
  if (srvGuids.empty()) return;

  // Trace each SrvFromTexture2d's TEXTURE source (the wire into its .Texture slot).
  std::map<std::string, std::pair<std::string, std::string>> texSrc;  // srvGuid → (srcGuid, srcSlot)
  for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
    if (!wv.is_object()) continue;
    if (srvGuids.count(lc(asStr(wv, "TargetParentOrChildId"))) &&
        lc(asStr(wv, "TargetSlotId")) == lc(kSrvFromTexture2dTexSlot))
      texSrc[lc(asStr(wv, "TargetParentOrChildId"))] = {lc(asStr(wv, "SourceParentOrChildId")),
                                                        lc(asStr(wv, "SourceSlotId"))};
  }

  // For each SrvFromTexture2d.out → ComputeShaderStage.ShaderResources wire, append texSrc →
  // stage.ShaderResourceTextures.
  for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
    if (!wv.is_object()) continue;
    const std::string sg = lc(asStr(wv, "SourceParentOrChildId"));
    if (!srvGuids.count(sg) || lc(asStr(wv, "SourceSlotId")) != lc(kSrvFromTexture2dOutSlot)) continue;
    auto dstIt = childGuidToId.find(lc(asStr(wv, "TargetParentOrChildId")));
    auto srcIt = texSrc.find(sg);
    if (dstIt == childGuidToId.end() || srcIt == texSrc.end()) {
      warn("t3: SrvFromTexture2d " + sg + " fold incomplete (stage/texsrc unresolved), texture dropped");
      continue;
    }
    const int stageChild = dstIt->second;
    auto swt = childIdToSwType.find(stageChild);
    if (swt == childIdToSwType.end() || swt->second != "ComputeShaderStage") {
      warn("t3: SrvFromTexture2d fold target is not a ComputeShaderStage, texture dropped");
      continue;
    }
    const std::string& tsGuid = srcIt->second.first;
    const std::string& tsSlot = srcIt->second.second;
    SymbolConnection c;
    c.dstChild = stageChild;
    c.dstSlot = "ShaderResourceTextures";
    if (isBoundaryGuid(tsGuid)) {
      c.srcChild = kSymbolBoundary;
      c.srcSlot = tsSlot;  // boundary input slot id (matches SlotDef.id from Inputs[])
    } else {
      auto tsIt = childGuidToId.find(tsGuid);
      if (tsIt == childGuidToId.end()) {
        warn("t3: SrvFromTexture2d texture source " + tsGuid + " unmapped (glue chain), texture dropped");
        continue;
      }
      c.srcChild = tsIt->second;
      auto pt = childIdToSwType.find(c.srcChild);
      c.srcSlot = (pt != childIdToSwType.end()) ? swSlotNameForGuid(pt->second, tsSlot) : std::string();
      if (c.srcSlot.empty()) {
        warn("t3: SrvFromTexture2d texture source slot " + tsSlot + " unresolved, texture dropped");
        continue;
      }
    }
    conns.push_back(c);
  }
}

}  // namespace sw
