// runtime/t3_import — .t3 importer impl (see t3_import.h for the forward-port rationale + honest
// scope). Pure CPU: strip .t3 inline comments, crude_json parse, three Guid→sw maps, fill one Symbol.
#include "runtime/t3_import.h"

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "crude_json.h"          // same JSON lib compound_save uses (NODE_EDITOR_DIR on -I path)
#include "runtime/graph.h"       // NodeSpec / findSpec (production atom registry)
#include "runtime/graph_bridge.h"  // atomicSymbolFromSpec
#include "runtime/t3_import_internal.h"  // t3i::{lc,asStr,isBoundaryGuid} + collapseImageFxWrapper decl
#include "runtime/t3_import_maps.h" // t3Lc + swTypeForSymbolGuid/swSlotNameForGuid + fold-pass guids

namespace sw {

using t3i::asStr;
using t3i::isBoundaryGuid;
using t3i::lc;

// Test-only injection seam (routing RED case). When set, the FIRST MultiInput collision (two wires
// into the same (childId, slotName)) has its connection order REVERSED — corrupting only the order.
bool& t3ImportInjectBug() {
  static bool flag = false;
  return flag;
}

namespace {

// lc / isBoundaryGuid / asStr now live in t3_import_internal.h (shared with t3_import_collapse.cpp);
// pulled into this namespace via the `using t3i::…` above (ARCHITECTURE rule 4 split).

// Strip TiXL inline `/* ... */` comments so crude_json can parse. Quote state tracked so a literal
// "/*" inside a JSON string value survives verbatim.
std::string stripT3Comments(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  bool inStr = false;
  for (size_t i = 0; i < in.size(); ++i) {
    char c = in[i];
    if (inStr) {
      out.push_back(c);
      if (c == '\\' && i + 1 < in.size()) {
        out.push_back(in[++i]);
      } else if (c == '"') {
        inStr = false;
      }
      continue;
    }
    if (c == '"') { inStr = true; out.push_back(c); continue; }
    if (c == '/' && i + 1 < in.size() && in[i + 1] == '*') {
      size_t end = in.find("*/", i + 2);
      if (end == std::string::npos) break;
      i = end + 1;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

bool importT3Symbol(const std::string& t3Json, SymbolLibrary& lib, std::string* outSymbolId,
                    std::vector<std::string>* warnings) {
  auto warn = [&](const std::string& m) { if (warnings) warnings->push_back(m); };

  crude_json::value root = crude_json::value::parse(stripT3Comments(t3Json));
  if (!root.is_object()) { warn("t3: not a JSON object"); return false; }

  const std::string symGuid = lc(asStr(root, "Id"));
  if (symGuid.empty()) { warn("t3: missing top-level Id"); return false; }

  Symbol sym;
  sym.id = symGuid;
  sym.name = symGuid;
  sym.atomic = false;

  // Top-level Inputs[] → the symbol's external input SlotDefs (boundary ports). Named by guid.
  if (root["Inputs"].is_array()) {
    for (const crude_json::value& iv : root["Inputs"].get<crude_json::array>()) {
      if (!iv.is_object()) continue;
      const std::string sid = lc(asStr(iv, "Id"));
      if (sid.empty()) { warn("t3: input slot missing Id, skipped"); continue; }
      SlotDef d;
      d.id = sid;
      d.name = sid;
      d.dataType = "Float";
      if (iv["DefaultValue"].is_number()) d.def = (float)iv["DefaultValue"].get<crude_json::number>();
      sym.inputDefs.push_back(d);
    }
  }

  // ---- IMAGE-FX COLLAPSE (image-fx-wrapper-collapses-to-tex-atom): if this ROOT is a known image-fx
  // wrapper (④b table), collapse the whole compound to ONE sw tex atom instead of the normal per-child
  // map (whose fx-setup child has no atom → empty root). collapseImageFxWrapper builds the atom + wires;
  // on success we commit the symbol and return. On a shape mismatch it warns and we fall through to the
  // normal path (which reports the same empty-root diagnostic — no silent success).
  const std::string collapseType = swTexOpForCollapseRootGuid(symGuid);
  if (!collapseType.empty()) {
    if (collapseImageFxWrapper(root, collapseType, sym, lib, warn)) {
      lib.symbols[sym.id] = sym;
      if (lib.rootId.empty()) lib.rootId = sym.id;
      if (outSymbolId) *outSymbolId = sym.id;
      return true;
    }
  }

  // ---- TABLE ①: t3 child guid → int childId. Built while walking Children[].
  std::map<std::string, int> childGuidToId;
  std::map<int, std::string> childIdToSwType;
  // Fold pass state (computeshader-source-folded-onto-stage): ComputeShader child guid → its Source string.
  // ComputeShader is NOT emitted as an sw child; its Source rides onto the ComputeShaderStage it feeds.
  std::map<std::string, std::string> computeShaderSource;
  int nextChildId = 1;

  if (root["Children"].is_array()) {
    for (const crude_json::value& cv : root["Children"].get<crude_json::array>()) {
      if (!cv.is_object()) continue;
      const std::string childGuid = lc(asStr(cv, "Id"));
      const std::string symbolId = asStr(cv, "SymbolId");
      if (childGuid.empty()) { warn("t3: child missing Id, skipped"); continue; }
      // Fold: a ComputeShader child contributes its Source (not a child); capture it for the post-pass.
      if (lc(symbolId) == kComputeShaderGuid) {
        if (cv["InputValues"].is_array())
          for (const crude_json::value& ivv : cv["InputValues"].get<crude_json::array>()) {
            if (ivv.is_object() && lc(asStr(ivv, "Id")) == kComputeShaderSourceSlot &&
                ivv["Value"].is_string())
              computeShaderSource[childGuid] = ivv["Value"].get<crude_json::string>();
          }
        continue;  // ComputeShader itself is never an sw child
      }
      const std::string swType = swTypeForSymbolGuid(symbolId);
      if (swType.empty()) {
        warn("t3: child " + childGuid + " unmapped SymbolId " + lc(symbolId) +
             " (no sw atom — e.g. ComputeShaderStage/StructuredBufferWithViews/TransformMatrix), skipped");
        continue;
      }
      const NodeSpec* fs = findSpec(swType);
      if (!fs) {
        warn("t3: no NodeSpec in findSpec for type " + swType + ", child skipped");
        continue;
      }
      if (!lib.symbols.count(swType)) lib.symbols[swType] = atomicSymbolFromSpec(*fs);

      const int childId = nextChildId++;
      childGuidToId[childGuid] = childId;
      childIdToSwType[childId] = swType;

      SymbolChild child;
      child.id = childId;
      child.symbolId = swType;

      // InputValues[] → constant overrides on THIS instance (non-default only).
      if (cv["InputValues"].is_array()) {
        for (const crude_json::value& ivv : cv["InputValues"].get<crude_json::array>()) {
          if (!ivv.is_object()) continue;
          const std::string slotGuid = lc(asStr(ivv, "Id"));
          const std::string slotName = swSlotNameForGuid(swType, slotGuid);
          if (slotName.empty()) {
            warn("t3: child " + childGuid + " InputValue unknown slot " + slotGuid + ", skipped");
            continue;
          }
          const std::string vtype = asStr(ivv, "Type");
          if (vtype == "System.Single" || vtype == "System.Int32") {
            if (ivv["Value"].is_number())
              child.overrides[slotName] = (float)ivv["Value"].get<crude_json::number>();
          } else if (vtype == "System.String") {
            if (ivv["Value"].is_string())
              child.strOverrides[slotName] = ivv["Value"].get<crude_json::string>();
          } else if (vtype == "System.Boolean") {
            if (ivv["Value"].is_boolean())
              child.overrides[slotName] = ivv["Value"].get<crude_json::boolean>() ? 1.0f : 0.0f;
          } else {
            // Int3 (Dispatch) etc. — no scalar sw slot; drop with a warning (honest gap).
            warn("t3: child " + childGuid + " InputValue type " + vtype + " unsupported, skipped");
          }
        }
      }
      sym.children.push_back(child);
    }
  }
  sym.nextChildId = nextChildId;

  // ---- FOLD PASS (computeshader-source-folded-onto-stage): for each raw connection ComputeShader.CS →
  // ComputeShaderStage.ComputeShader, set the STAGE child's KernelName strOverride to the ComputeShader's
  // Source. ComputeShader has no sw child, so this is the ONLY way its Source reaches the dispatch.
  if (!computeShaderSource.empty() && root["Connections"].is_array()) {
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      const std::string srcGuid = lc(asStr(wv, "SourceParentOrChildId"));
      const std::string srcSlot = lc(asStr(wv, "SourceSlotId"));
      const std::string dstGuid = lc(asStr(wv, "TargetParentOrChildId"));
      const std::string dstSlot = lc(asStr(wv, "TargetSlotId"));
      auto cs = computeShaderSource.find(srcGuid);
      if (cs == computeShaderSource.end() || srcSlot != kComputeShaderCsOutSlot) continue;
      // Accept EITHER CS-in slot: ComputeShaderStage.ComputeShader OR _ExecuteCombineBuffers.ComputeShader
      // (187 量產第一波 — a code-op compound folds its ComputeShader.Source onto KernelName like a stage).
      if (dstSlot != kComputeStageCsInSlot && dstSlot != kCombineBuffersCsInSlot) continue;
      auto dit = childGuidToId.find(dstGuid);
      if (dit == childGuidToId.end()) continue;  // stage not mapped (shouldn't happen)
      for (SymbolChild& ch : sym.children)
        if (ch.id == dit->second) { ch.strOverrides["KernelName"] = cs->second; break; }
    }
  }

  // Connections[] → SymbolConnection 4-tuples, ARRAY ORDER PRESERVED (MultiInput order).
  auto slotNameForEndpoint = [&](int childId, const std::string& slotGuid) -> std::string {
    if (childId == kSymbolBoundary) return lc(slotGuid);  // matches SlotDef.id from Inputs[]
    auto t = childIdToSwType.find(childId);
    if (t == childIdToSwType.end()) return std::string();
    return swSlotNameForGuid(t->second, slotGuid);
  };

  std::vector<SymbolConnection> conns;
  if (root["Connections"].is_array()) {
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      const std::string srcChildGuid = lc(asStr(wv, "SourceParentOrChildId"));
      const std::string dstChildGuid = lc(asStr(wv, "TargetParentOrChildId"));
      const std::string srcSlotGuid = asStr(wv, "SourceSlotId");
      const std::string dstSlotGuid = asStr(wv, "TargetSlotId");

      int srcChild = kSymbolBoundary, dstChild = kSymbolBoundary;
      if (!isBoundaryGuid(srcChildGuid)) {
        auto it = childGuidToId.find(srcChildGuid);
        if (it == childGuidToId.end()) {
          warn("t3: wire src child " + srcChildGuid + " unmapped (skipped op), dropped");
          continue;
        }
        srcChild = it->second;
      }
      if (!isBoundaryGuid(dstChildGuid)) {
        auto it = childGuidToId.find(dstChildGuid);
        if (it == childGuidToId.end()) {
          warn("t3: wire dst child " + dstChildGuid + " unmapped (skipped op), dropped");
          continue;
        }
        dstChild = it->second;
      }
      const std::string srcSlot = slotNameForEndpoint(srcChild, srcSlotGuid);
      const std::string dstSlot = slotNameForEndpoint(dstChild, dstSlotGuid);
      if (srcSlot.empty() || dstSlot.empty()) {
        warn("t3: wire slot unresolved (src=" + srcSlotGuid + " dst=" + dstSlotGuid + "), dropped");
        continue;
      }
      SymbolConnection c;
      c.srcChild = srcChild;
      c.srcSlot = srcSlot;
      c.dstChild = dstChild;
      c.dstSlot = dstSlot;
      conns.push_back(c);
    }
  }

  // Routing RED tooth: reverse the FIRST MultiInput collision's order.
  if (t3ImportInjectBug()) {
    for (size_t i = 0; i + 1 < conns.size(); ++i) {
      for (size_t j = i + 1; j < conns.size(); ++j) {
        if (conns[i].dstChild == conns[j].dstChild && conns[i].dstSlot == conns[j].dstSlot) {
          std::swap(conns[i], conns[j]);
          goto doneSwap;
        }
      }
    }
  doneSwap:;
  }

  sym.connections = std::move(conns);

  lib.symbols[sym.id] = sym;
  if (lib.rootId.empty()) lib.rootId = sym.id;
  if (outSymbolId) *outSymbolId = sym.id;
  return true;
}

}  // namespace sw
