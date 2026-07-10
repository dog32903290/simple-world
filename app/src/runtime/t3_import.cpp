// runtime/t3_import — .t3 importer impl (see t3_import.h for the forward-port rationale + honest
// scope). Pure CPU: strip .t3 inline comments, crude_json parse, three Guid→sw maps, fill one Symbol.
#include "runtime/t3_import.h"

#include <functional>
#include <map>
#include <regex>
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
using t3i::stripT3Comments;

// Test-only injection seam (routing RED case). When set, the FIRST MultiInput collision (two wires
// into the same (childId, slotName)) has its connection order REVERSED — corrupting only the order.
bool& t3ImportInjectBug() {
  static bool flag = false;
  return flag;
}

namespace {

// lc / isBoundaryGuid / asStr / stripT3Comments now live in t3_import_internal.h (shared with
// t3_import_collapse.cpp); pulled into this namespace via the `using t3i::…` above (ARCH rule 4 split).

// TOP-LEVEL NAME from the .t3 Id comment (imported-compound-shows-guid-not-name): TiXL writes the
// symbol's readable name as an inline comment on its top-level Id — `"Id": "<guid>"/*RadialGradient*/`.
// stripT3Comments deletes ALL such comments before parsing, so the parsed Symbol.name would be the raw
// GUID and the Cmd+F palette would show a string of hex. Recover the FIRST (= root object's) Id comment
// from the RAW text here, BEFORE stripping. Returns "" when the root Id has no name comment (leave the
// GUID fallback). Anchored to the first `"Id"` key so a child's Id comment can never win.
std::string topLevelName(const std::string& raw) {
  static const std::regex re(R"("Id"\s*:\s*"[^"]+"\s*/\*([^*]+)\*/)");
  std::smatch m;
  if (std::regex_search(raw, m, re) && m.size() >= 2) return m[1].str();
  return std::string();
}

// GUID→NAME TABLE (imported-compound-input-port-shows-guid-not-name): the SAME `/*Name*/` inline comments
// also sit on EVERY identified thing in the .t3 (inputs/outputs/children/slots), not just the root.
// topLevelName only recovers the ROOT name; this sweeps the WHOLE raw text (BEFORE stripT3Comments
// deletes them) so any name-needing endpoint can look up its real name instead of the bare GUID (lowercased
// so a lookup keyed by SlotDef.id hits regardless of the .t3's casing).
std::map<std::string, std::string> guidNameMap(const std::string& raw) {
  std::map<std::string, std::string> out;
  static const std::regex re(R"RE("Id"\s*:\s*"([^"]+)"\s*/\*([^*]+)\*/)RE");
  for (auto it = std::sregex_iterator(raw.begin(), raw.end(), re), end = std::sregex_iterator();
       it != end; ++it) {
    const std::string guid = lc((*it)[1].str());
    if (!guid.empty() && !out.count(guid)) out[guid] = (*it)[2].str();
  }
  return out;
}

// INPUT-PORT TYPE from the boundary input's .t3 DefaultValue SHAPE, mapped onto sw's dataType vocabulary
// (Float/Texture2D/Gradient/Color/String — sw has NO distinct Vec2/Vec3/Int/Bool; a vector is N scalar
// Float rails on the atom, so a vec BOUNDARY port is a Float). FALLBACK type source only — the primary is
// the wire re-anchor below (exact where an input is wired). "" when the shape gives no signal (e.g. null
// Image — the wire re-anchor supplies Texture2D; if that also fails the caller keeps Float, an honest gap).
std::string dataTypeFromDefault(const crude_json::value& dv) {
  if (dv.is_string()) return "String";              // e.g. TextureFormat = "R16G16B16A16_Float"
  if (dv.is_boolean() || dv.is_number()) return "Float";  // bool / int / float all ride the Float rail
  if (dv.is_object()) {
    if (dv.contains("Gradient")) return "Gradient";       // {Gradient:{Steps:[...]}}
    if (dv.contains("R") || dv.contains("G") || dv.contains("B")) return "Color";  // {R,G,B,A}
    return "Float";                                        // {X,Y[,Z,W]} vec → scalar Float in sw
  }
  return std::string();                                    // null / unknown → let the wire decide
}

// INPUT-TYPE RE-ANCHOR (primary type source): each boundary INPUT that feeds a child's port inherits that
// port's EXACT sw dataType (Image→Texture2D, Gradient→Gradient, a scalar→Float) — the truth where wired.
// Runs on the committed `sym` for BOTH the collapse path and the normal path, no path-local maps needed.
// Only OVERWRITES a non-empty DIFFERENT type; an unwired input keeps its DefaultValue-shape type.
void refineInputTypesFromWires(Symbol& sym, const SymbolLibrary& lib) {
  for (const SymbolConnection& c : sym.connections) {
    if (c.srcChild != kSymbolBoundary) continue;           // only wires OUT of a boundary input
    if (c.dstChild == kSymbolBoundary) continue;           // input→output passthrough: no child port
    // Resolve the destination child's referenced symbol → its input port's dataType.
    const SymbolChild* dc = nullptr;
    for (const SymbolChild& ch : sym.children) if (ch.id == c.dstChild) { dc = &ch; break; }
    if (!dc) continue;
    const NodeSpec* ds = findSpec(dc->symbolId);
    if (!ds) continue;
    std::string portType;
    for (const PortSpec& p : ds->ports)
      if (p.isInput && p.id == c.dstSlot) { portType = p.dataType; break; }
    if (portType.empty()) continue;
    for (SlotDef& d : sym.inputDefs)
      if (d.id == c.srcSlot && d.dataType != portType) { d.dataType = portType; break; }
  }
}

}  // namespace

// Cheap top-level Id peek (boot-catalog idempotency): parse ONLY the root Id — same strip+parse+lc(Id)
// the importer uses, so it matches importT3Symbol's sym.id byte-for-byte. false when no usable Id.
bool symbolIdOfT3(const std::string& t3Json, std::string* outId) {
  crude_json::value root = crude_json::value::parse(stripT3Comments(t3Json));
  const std::string id = root.is_object() ? lc(asStr(root, "Id")) : std::string();
  if (id.empty()) return false;
  if (outId) *outId = id;
  return true;
}

// The depth-carrying importer CORE. Public importT3Symbol overloads (bottom of file) forward here with
// depth 0; the nested-compound recursion (t3_import_recurse.cpp) re-enters here with depth+1. `resolve`
// supplies nested compound children's .t3 by guid (empty ⇒ single-layer, byte-identical to pre-seam).
bool importT3SymbolImpl(const std::string& t3Json, SymbolLibrary& lib, std::string* outSymbolId,
                        std::vector<std::string>* warnings, const T3Resolver& resolve,
                        const T3LayoutResolver& layoutResolve, int depth) {
  auto warn = [&](const std::string& m) { if (warnings) warnings->push_back(m); };

  crude_json::value root = crude_json::value::parse(stripT3Comments(t3Json));
  if (!root.is_object()) { warn("t3: not a JSON object"); return false; }

  const std::string symGuid = lc(asStr(root, "Id"));
  if (symGuid.empty()) { warn("t3: missing top-level Id"); return false; }
  // .t3ui LAYOUT: this symbol's OWN sibling .t3ui text (canvas positions), if the caller supplied one.
  std::string t3uiJson; if (layoutResolve) layoutResolve(symGuid, t3uiJson);

  // GUID→NAME table from the RAW text (before the comments are stripped). SSOT for recovering the
  // readable name of every identified endpoint — root symbol, input/output slots, children, sub-slots.
  const std::map<std::string, std::string> nameByGuid = guidNameMap(t3Json);
  auto nameFor = [&](const std::string& guid) -> std::string {
    auto it = nameByGuid.find(lc(guid));
    return it != nameByGuid.end() ? it->second : std::string();
  };

  Symbol sym;
  sym.id = symGuid;
  // Readable name from the root Id's inline comment (else fall back to the GUID) — set BEFORE the
  // collapse branch commits its Symbol, so both the collapse and the normal path carry the real name.
  const std::string readableName = topLevelName(t3Json);
  sym.name = readableName.empty() ? symGuid : readableName;
  sym.atomic = false;

  // Top-level Inputs[] → the symbol's external input SlotDefs (boundary ports). Name recovered from the
  // guid→name table (was the bare GUID — imported-compound-input-port-shows-guid-not-name); type from the
  // DefaultValue SHAPE (was hard-coded Float — a vec/gradient/image/string input mis-labelled Float
  // couldn't be told apart in the Inspector or wired by type). The wire re-anchor sharpens this further
  // below (an input feeding a known atom port inherits that port's exact dataType).
  if (root["Inputs"].is_array()) {
    for (const crude_json::value& iv : root["Inputs"].get<crude_json::array>()) {
      if (!iv.is_object()) continue;
      const std::string sid = lc(asStr(iv, "Id"));
      if (sid.empty()) { warn("t3: input slot missing Id, skipped"); continue; }
      SlotDef d;
      d.id = sid;
      const std::string nm = nameFor(sid);
      d.name = nm.empty() ? sid : nm;  // real name, else GUID fallback (honest gap for an unnamed slot)
      const std::string dt = iv.contains("DefaultValue") ? dataTypeFromDefault(iv["DefaultValue"])
                                                          : std::string();
      d.dataType = dt.empty() ? "Float" : dt;  // shape-derived; wire re-anchor may override below
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
    if (collapseImageFxWrapper(root, collapseType, sym, lib, warn, t3uiJson)) {
      refineInputTypesFromWires(sym, lib);  // sharpen boundary-input types from the atom/helper ports
      lib.symbols[sym.id] = sym;
      if (depth == 0 && lib.rootId.empty()) lib.rootId = sym.id;  // only the TOP import seeds root
      if (outSymbolId) *outSymbolId = sym.id;
      return true;
    }
  }

  // ---- TEXTURE-COMPUTE COLLAPSE (TEXTURE_COMPUTE_SEAM_SPEC §3): a texture-OUT compute .t3 (a
  // ComputeShaderStage whose Uavs is fed by UavFromTexture2d) collapses onto ONE tex-track
  // ComputeShaderStageTex atom. SHAPE-detected (not guid-keyed) → generic across the 66 texture-bound
  // compute compounds. On a non-match (SRV-tex read / extra input children / not the shape) it returns
  // false and we fall through to the normal per-child map path.
  if (collapseTextureComputeStage(root, sym, lib, warn, t3uiJson)) {
    refineInputTypesFromWires(sym, lib);
    lib.symbols[sym.id] = sym;
    if (depth == 0 && lib.rootId.empty()) lib.rootId = sym.id;
    if (outSymbolId) *outSymbolId = sym.id;
    return true;
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
      std::string swType = swTypeForSymbolGuid(symbolId);
      if (swType.empty()) {
        // NESTED COMPOUND: SymbolId is not a mapped atom. A resolver may supply the child's own .t3 (a
        // pure sub-graph compound) → RECURSE it into `lib` as a nested sub-compound, referenced by guid,
        // instead of skipping/flattening. "" ⇒ still unmapped → the honest skip below.
        swType = t3ResolveNestedCompound(symGuid, childGuid, symbolId, lib, warnings, resolve,
                                         layoutResolve, depth, warn);
        if (swType.empty()) {
          warn("t3: child " + childGuid + " unmapped SymbolId " + lc(symbolId) +
               " (no sw atom — e.g. ComputeShaderStage/StructuredBufferWithViews/TransformMatrix), skipped");
          continue;
        }
      }
      // A NESTED COMPOUND swType (guid) is already committed into `lib` and has no atom NodeSpec — skip
      // the atom-registration path (findSpec would miss + drop it). Atoms take the original spec path.
      const Symbol* swDef = lib.find(swType);
      const bool isNestedCompound = swDef && !swDef->atomic;
      if (!isNestedCompound) {
        const NodeSpec* fs = findSpec(swType);
        if (!fs) {
          warn("t3: no NodeSpec in findSpec for type " + swType + ", child skipped");
          continue;
        }
        if (!lib.symbols.count(swType)) lib.symbols[swType] = atomicSymbolFromSpec(*fs);
      }

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
          // §1.3: an override on a NESTED COMPOUND child resolves through the nested Symbol's inputDefs.
          const std::string slotName = t3SlotNameForChildType(lib, swType, slotGuid);
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

      // Outputs[].OutputData → per-instance TimeClip placement (= SymbolJson.cs:112-131). Reads the child's
      // authored TimeRange/SourceRange/LayerIndex into child.clips (split to t3_import_collapse.cpp for the
      // rule-4 line ratchet). Only carrying ops (TimeClip/MidiClip/…) have an OutputData block; the rest skip.
      parseChildTimeClips(cv, swType, child, warn);
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
    // §1.3: a wire into/out of a NESTED COMPOUND child resolves via the nested Symbol's in/out defs.
    return t3SlotNameForChildType(lib, t->second, slotGuid);
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

  // COMPOUND-OUTPUT-DEF (imported-compound-needs-outputdefs-to-be-draggable-child): each wire whose
  // TARGET is the SYMBOL BOUNDARY names one of THIS compound's external output slots (dstSlot = the .t3
  // boundary output guid). Without a matching SlotDef the imported compound has NO output PIN when
  // dragged as a child (specFromSymbol emits output ports from outputDefs) AND viewProducerPath returns
  // "" (`s->outputDefs.empty()`) → dead node / black cook. The port TYPE/name is copied from the SOURCE
  // child atom's OUTPUT PortSpec that feeds this boundary (the terminal producer of that output). One
  // SlotDef per distinct boundary output slot, in first-seen wire order. Metadata only — the flattener
  // already keys outProducers on dstSlot regardless, so the cook product is unchanged.
  for (const SymbolConnection& c : conns) {
    if (c.dstChild != kSymbolBoundary) continue;
    bool have = false;
    for (const SlotDef& od : sym.outputDefs) if (od.id == c.dstSlot) { have = true; break; }
    if (have) continue;
    std::string dtype = "Texture2D", dname = c.dstSlot;
    auto tit = childIdToSwType.find(c.srcChild);
    if (tit != childIdToSwType.end())
      if (const NodeSpec* ss = findSpec(tit->second))
        for (const PortSpec& p : ss->ports)
          if (!p.isInput && p.id == c.srcSlot) { dtype = p.dataType; dname = p.name; break; }
    sym.outputDefs.push_back({c.dstSlot, dname, dtype, 0.0f});
  }

  sym.connections = std::move(conns);
  refineInputTypesFromWires(sym, lib);  // sharpen boundary-input types from the child ports they feed
  applyT3uiPositions(sym, t3uiJson, childGuidToId);  // .t3ui LAYOUT: after outputDefs exist (COMPOUND-OUTPUT-DEF above)

  lib.symbols[sym.id] = sym;
  if (depth == 0 && lib.rootId.empty()) lib.rootId = sym.id;  // only the TOP import seeds root (depth>0
  if (outSymbolId) *outSymbolId = sym.id;                     // nested recursion must not hijack it)
  return true;
}

}  // namespace sw
