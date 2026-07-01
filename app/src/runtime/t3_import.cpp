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
#include "runtime/t3_import_maps.h" // t3Lc + swTypeForSymbolGuid/swSlotNameForGuid + fold-pass guids

namespace sw {

// Test-only injection seam (routing RED case). When set, the FIRST MultiInput collision (two wires
// into the same (childId, slotName)) has its connection order REVERSED — corrupting only the order.
bool& t3ImportInjectBug() {
  static bool flag = false;
  return flag;
}

namespace {

// guid normalization + the three-table Guid→sw maps live in t3_import_maps.{h,cpp} (ARCHITECTURE rule 4/7:
// the mapping DATA is a distinct data-driven job — add an atom = add a row there). `lc` here aliases t3Lc.
inline std::string lc(std::string s) { return t3Lc(std::move(s)); }

constexpr const char* kGuidEmpty = "00000000-0000-0000-0000-000000000000";
bool isBoundaryGuid(const std::string& g) { return g.empty() || g == kGuidEmpty; }

std::string asStr(const crude_json::value& v, const char* key) {
  return v[key].is_string() ? v[key].get<crude_json::string>() : std::string();
}

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

// ── IMAGE-FX COLLAPSE (image-fx-wrapper-collapses-to-tex-atom) ───────────────────────────────────────
// A whole family of image .t3 ops are THIN wrappers around a TiXL image-fx-setup FRAMEWORK symbol
// (_multiImageFxSetupStatic / …), parameterized by a ShaderPath pixel shader. In sw the op's BEHAVIOR is
// the one .hlsl → ONE flat tex atom (ports 1:1 with the op's .cs). The wrapper collapses to that atom.
//
// TWO shapes are covered, unified by the SAME re-anchoring:
//   (1) SINGLE-fx-setup-child (HSE): the fx-setup child is the only child. Boundary Inputs wire straight
//       into it. Collapse → 1 atom child; boundary→fx wires re-anchor onto atom ports.
//   (2) MULTI-child (Blend, …): the fx-setup child is fed by HELPER VALUE ops (Vector4Components /
//       IntToFloat / BoolToFloat …) that decompose boundary vec/int/bool inputs into the scalar rails.
//       Those helpers are KEPT as real sw children (they already have atoms + map rows); only the
//       fx-setup child collapses. A wire INTO the fx-setup child re-anchors onto the atom, keeping its
//       SOURCE (boundary or helper child) — so a helper's X output feeding FloatParams[k] becomes
//       helperChild.X → atom.<④d order[k]>. Non-fx wires (boundary→helper, helper→helper) are kept as-is.
//
// Re-anchoring of the fx-setup child's slots:
//   *  → fxchild.ImageA/ImageB       ⇒ *  → atom.Image/FxTexture   (④c fixed-slot map; * = kept source)
//   *  → fxchild.FloatParams[k]      ⇒ *  → atom.<④d order[k]>     (positional scalar rail, wire order)
//   fxchild.Output → boundary out    ⇒ atom.out → boundary out
//   *  → fxchild.<other slot>        ⇒ dropped (atom uses its port defaults; e.g. Resolution/WrapMode)
// Returns true on a collapsed symbol; false if the shape is not a recognized wrapper (caller reports it).
bool collapseImageFxWrapper(const crude_json::value& root, const std::string& swType, Symbol& sym,
                            SymbolLibrary& lib,
                            const std::function<void(const std::string&)>& warn) {
  const NodeSpec* fs = findSpec(swType);
  if (!fs) { warn("t3: collapse target " + swType + " has no NodeSpec, aborting collapse"); return false; }

  // Pass 1: walk Children. Emit each HELPER child (non-fx-setup) as a normal sw child via the standard
  // guid→atom map (same path as the main importer). Record the ONE fx-setup child guid; it collapses.
  std::string fxChildGuid;
  int fxSetupCount = 0;
  std::map<std::string, int> childGuidToId;   // helper .t3 guid → sw childId
  std::map<int, std::string> childIdToSwType;  // sw childId → sw type (for slot resolution)
  int nextChildId = 1;
  if (root["Children"].is_array()) {
    for (const crude_json::value& cv : root["Children"].get<crude_json::array>()) {
      if (!cv.is_object()) continue;
      const std::string childGuid = lc(asStr(cv, "Id"));
      const std::string sid = lc(asStr(cv, "SymbolId"));
      if (isImageFxSetupGuid(sid)) { fxChildGuid = childGuid; fxSetupCount++; continue; }
      const std::string helperType = swTypeForSymbolGuid(sid);
      if (helperType.empty()) {
        warn("t3: collapse helper child " + childGuid + " has unmapped SymbolId " + sid +
             " (no sw atom) — multi-child fx wrapper 子類 needs this helper mapped, aborting collapse");
        return false;
      }
      const NodeSpec* hs = findSpec(helperType);
      if (!hs) { warn("t3: collapse helper type " + helperType + " has no NodeSpec, aborting"); return false; }
      if (!lib.symbols.count(helperType)) lib.symbols[helperType] = atomicSymbolFromSpec(*hs);
      const int cid = nextChildId++;
      childGuidToId[childGuid] = cid;
      childIdToSwType[cid] = helperType;
      SymbolChild ch; ch.id = cid; ch.symbolId = helperType;
      // Helper InputValues → constant overrides (non-default), same rule as the main importer.
      if (cv["InputValues"].is_array())
        for (const crude_json::value& ivv : cv["InputValues"].get<crude_json::array>()) {
          if (!ivv.is_object()) continue;
          const std::string slotName = swSlotNameForGuid(helperType, lc(asStr(ivv, "Id")));
          if (slotName.empty()) continue;
          const std::string vtype = asStr(ivv, "Type");
          if ((vtype == "System.Single" || vtype == "System.Int32") && ivv["Value"].is_number())
            ch.overrides[slotName] = (float)ivv["Value"].get<crude_json::number>();
          else if (vtype == "System.Boolean" && ivv["Value"].is_boolean())
            ch.overrides[slotName] = ivv["Value"].get<crude_json::boolean>() ? 1.0f : 0.0f;
        }
      sym.children.push_back(ch);
    }
  }
  if (fxSetupCount != 1) {
    warn("t3: collapse root maps to " + swType + " but has fxSetupCount=" +
         std::to_string(fxSetupCount) + " (expected exactly 1), aborting collapse");
    return false;
  }

  if (!lib.symbols.count(swType)) lib.symbols[swType] = atomicSymbolFromSpec(*fs);
  const int atomId = nextChildId++;
  { SymbolChild atom; atom.id = atomId; atom.symbolId = swType; sym.children.push_back(atom); }
  sym.nextChildId = nextChildId;

  const std::vector<std::string>& floatOrder = swFloatParamOrderForCollapse(swType);
  int floatWireIdx = 0;  // positional index into FloatParams (2929c4c9) wires, in .t3 array order.

  // Resolve a wire endpoint (guid+slotGuid) to a (childId, slotName) in the collapsed graph. Boundary →
  // (kSymbolBoundary, lowercased slot guid = SlotDef.id). Helper child → (its childId, sw slot name).
  // Returns false if the endpoint is a helper child whose slot has no sw name (wire then dropped).
  auto resolveEndpoint = [&](const std::string& guid, const std::string& slotGuid, bool asOutput,
                             int& outChild, std::string& outSlot) -> bool {
    if (isBoundaryGuid(guid)) { outChild = kSymbolBoundary; outSlot = lc(slotGuid); return true; }
    auto it = childGuidToId.find(guid);
    if (it == childGuidToId.end()) return false;  // unknown (fx-setup handled by caller) → skip
    outChild = it->second;
    outSlot = swSlotNameForGuid(childIdToSwType[it->second], slotGuid);
    (void)asOutput;
    return !outSlot.empty();
  };

  std::vector<SymbolConnection> conns;
  if (root["Connections"].is_array()) {
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      const std::string srcGuid = lc(asStr(wv, "SourceParentOrChildId"));
      const std::string dstGuid = lc(asStr(wv, "TargetParentOrChildId"));
      const std::string srcSlot = asStr(wv, "SourceSlotId");
      const std::string dstSlot = asStr(wv, "TargetSlotId");
      const bool srcIsFx = (srcGuid == fxChildGuid);
      const bool dstIsFx = (dstGuid == fxChildGuid);

      if (dstIsFx) {
        // wire INTO the fx-setup child → re-anchor its TARGET onto the atom, keeping the SOURCE.
        int sChild; std::string sSlot;
        if (!resolveEndpoint(srcGuid, srcSlot, /*asOutput=*/true, sChild, sSlot)) {
          warn("t3: collapse source into fx child unresolved (src=" + srcGuid + "), dropped");
          if (lc(dstSlot) == t3Lc(kFxSetupFloatParamsSlot)) floatWireIdx++;  // keep positional count
          continue;
        }
        const std::string fxSlot = lc(dstSlot);
        if (fxSlot == t3Lc(kFxSetupFloatParamsSlot)) {
          if (floatWireIdx < (int)floatOrder.size())
            conns.push_back({sChild, sSlot, atomId, floatOrder[floatWireIdx]});
          else
            warn("t3: collapse FloatParams wire #" + std::to_string(floatWireIdx) + " beyond " + swType +
                 " scalar order (" + std::to_string(floatOrder.size()) + "), dropped");
          floatWireIdx++;
          continue;
        }
        const std::string port = swCollapseSlotNameForGuid(swType, fxSlot);
        if (port.empty()) continue;  // e.g. Resolution/WrapMode/GenerateMips/IntParameters → atom defaults
        conns.push_back({sChild, sSlot, atomId, port});
        continue;
      }
      if (srcIsFx) {
        // fx-setup child output → its target. Re-anchor the atom's output onto that target.
        const std::string port = swCollapseSlotNameForGuid(swType, lc(srcSlot));
        if (port.empty()) { warn("t3: collapse fx output slot " + lc(srcSlot) + " has no port, dropped"); continue; }
        int dChild; std::string dSlot;
        if (!resolveEndpoint(dstGuid, dstSlot, /*asOutput=*/false, dChild, dSlot)) {
          warn("t3: collapse fx-output target unresolved (dst=" + dstGuid + "), dropped");
          continue;
        }
        conns.push_back({atomId, port, dChild, dSlot});
        continue;
      }
      // Neither endpoint is the fx-setup child (boundary→helper, helper→helper) — keep the wire as-is,
      // resolving both endpoints in the collapsed graph.
      int sChild; std::string sSlot; int dChild; std::string dSlot;
      if (!resolveEndpoint(srcGuid, srcSlot, true, sChild, sSlot) ||
          !resolveEndpoint(dstGuid, dstSlot, false, dChild, dSlot)) {
        warn("t3: collapse non-fx wire unresolved (src=" + srcGuid + " dst=" + dstGuid + "), dropped");
        continue;
      }
      conns.push_back({sChild, sSlot, dChild, dSlot});
    }
  }
  sym.connections = std::move(conns);
  return true;
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
