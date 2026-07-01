// runtime/t3_import_collapse — the IMAGE-FX-WRAPPER → tex-atom collapse pass, split from t3_import.cpp
// (ARCHITECTURE.md rule 4: the combined file crossed the ≤400 line ratchet; the collapse is a distinct,
// self-contained pass). Pure CPU. See t3_import_internal.h for the shared helpers + the contract.
//
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
#include "runtime/t3_import_internal.h"

#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "runtime/graph.h"          // NodeSpec / findSpec
#include "runtime/graph_bridge.h"   // atomicSymbolFromSpec
#include "runtime/t3_import_maps.h" // ④a-d collapse tables + fold-pass guids

namespace sw {

using t3i::asStr;
using t3i::isBoundaryGuid;
using t3i::lc;

namespace {
// GRADIENT-FED collapse (named fork gradientstotexture-elided-to-gradient-port): a whole class of
// image-fx wrappers (BubbleZoom, RemapColor, …) feed the fx-setup child's ImageB (t1) from a
// GradientsToTexture child (SymbolId 2c53eee7) that renders the root's Gradient boundary into a 1D
// texture. sw's collapsed atoms (e.g. BubbleZoom) instead consume the Gradient DIRECTLY (they sample it
// in-shader — see point_ops_bubblezoom.cpp), so the intermediate GradientsToTexture render is redundant.
// The collapse ELIDES the GradientsToTexture child: it is NOT emitted, and any wire off its Texture2D
// output re-anchors to the SOURCE feeding its Gradients input (588be11f). So the .t3 chain
//   <gradient src> → GTT.Gradients ; GTT.out → fxSetup.ImageB
// collapses to  <gradient src> → atom.Gradient  (ImageB→Gradient via ④c). This is the ONLY sanctioned
// elision — a GTT whose output does NOT feed a collapsing fx child is not reached by this pass.
const char* const kGradientsToTextureGuid = "2c53eee7-eb38-449b-ad2a-d7a674952e5b";  // GradientsToTexture.cs:9
const char* const kGradientsToTextureGradientsSlot =
    "588be11f-d0db-4e51-8dbb-92a25408511c";  // GradientsToTexture.Gradients MultiInput (.cs:134-135)

// BOUNDARY-VEC-DEFAULT PLUMB (collapse-boundary-typed-default-plumbed-through-kept-helper): the .t3
// root's Inputs[] carry TYPED defaults. A scalar default (System.Single) is captured by the importer's
// SlotDef.def; a VECTOR default is a JSON object {X,Y[,Z,W]}. When a kept decompose helper's Value input
// (Vector2/3/4Components) is fed by a boundary input that is NOT wired to an external producer, the cook
// drops that boundary→helper wire (buildEvalGraph injects boundary consts only for CALLER-supplied inputs),
// so the helper reads its OWN port default (0) for Value — decomposing to all-zeros and clobbering the atom
// scalar rail through the kept helper.X/.Y wires. Root cause of the BubbleZoom hollow-green (GainAndBias
// default (0.5,0.5) never reached the GPU → near-binary field). Fix: author the boundary's typed vec default
// onto the helper child's Value.x/.y/.z/.w overrides so the UNWIRED case decomposes the REAL default. A
// top-level driver still wins (the override is the KEPT fallback under a wire — resident_eval_flatten loop 3).
// The head port a decompose helper exposes for its Vector input is "Value.x" (fork-vecN-as-N-floats).
const char* const kDecomposeValueHead = "Value.x";
// Read a boundary DefaultValue object's {X,Y,Z,W} into an ordered [x,y,z,w] list (as many as present).
std::vector<std::pair<std::string, float>> readVecDefault(const crude_json::value& dv) {
  std::vector<std::pair<std::string, float>> out;
  if (!dv.is_object()) return out;
  for (const char* comp : {"X", "Y", "Z", "W"}) {
    if (dv.contains(comp) && dv[comp].is_number())
      out.push_back({std::string("Value.") + (char)std::tolower(comp[0]),
                     (float)dv[comp].get<crude_json::number>()});
  }
  return out;
}
}  // namespace

bool collapseImageFxWrapper(const crude_json::value& root, const std::string& swType, Symbol& sym,
                            SymbolLibrary& lib,
                            const std::function<void(const std::string&)>& warn) {
  const NodeSpec* fs = findSpec(swType);
  if (!fs) { warn("t3: collapse target " + swType + " has no NodeSpec, aborting collapse"); return false; }

  // Pass 1: walk Children. Emit each HELPER child (non-fx-setup) as a normal sw child via the standard
  // guid→atom map (same path as the main importer). Record the ONE fx-setup child guid; it collapses.
  std::string fxChildGuid;
  int fxSetupCount = 0;
  std::map<std::string, int> childGuidToId;    // helper .t3 guid → sw childId
  std::map<int, std::string> childIdToSwType;  // sw childId → sw type (for slot resolution)
  std::map<std::string, std::pair<std::string, std::string>> gttGradientSrc;  // GTT guid → its Gradients wire src (guid,slot)
  std::map<std::string, bool> gttChildGuids;   // .t3 guids of ELIDED GradientsToTexture children
  int nextChildId = 1;

  // Pre-scan Connections for each GradientsToTexture child's Gradients-input SOURCE (the endpoint feeding
  // 588be11f). The elision re-anchors GTT.out consumers onto this source. Done before Pass 1 so the child
  // walk can skip GTT children knowing their source is captured. (A GTT with NO wired Gradients source is
  // still elided; its consumers resolve to the empty source → dropped, mirroring an unwired atom.Gradient.)
  if (root["Connections"].is_array())
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      if (lc(asStr(wv, "TargetSlotId")) != t3Lc(kGradientsToTextureGradientsSlot)) continue;
      gttGradientSrc[lc(asStr(wv, "TargetParentOrChildId"))] = {lc(asStr(wv, "SourceParentOrChildId")),
                                                                asStr(wv, "SourceSlotId")};
    }

  // BOUNDARY-VEC-DEFAULT PLUMB pre-scan (collapse-boundary-typed-default-plumbed-through-kept-helper):
  //   (a) index each root boundary input's raw DefaultValue object by its slot guid (only vec objects
  //       matter; scalar/null are ignored — those defaults ride the atom's own port default already).
  //   (b) find every wire boundary → <helper child>.<its Vector Value HEAD slot guid>. That helper's
  //       Value input is boundary-fed; its typed default is the boundary's vec default. Keyed by the
  //       helper .t3 child guid so Pass 1 can author it as the helper's Value.x/.y/.z/.w overrides.
  std::map<std::string, const crude_json::value*> boundaryDefaultByGuid;  // boundary slot guid → DefaultValue
  if (root["Inputs"].is_array())
    for (const crude_json::value& iv : root["Inputs"].get<crude_json::array>()) {
      if (!iv.is_object()) continue;
      const std::string sid = lc(asStr(iv, "Id"));
      if (!sid.empty() && iv.contains("DefaultValue") && iv["DefaultValue"].is_object())
        boundaryDefaultByGuid[sid] = &iv["DefaultValue"];
    }
  // helper .t3 child guid → (target slot guid, raw boundary DefaultValue object) for a boundary→helper
  // wire. Pass 1 confirms the slot resolves to the helper's Value HEAD before authoring the default.
  std::map<std::string, std::pair<std::string, const crude_json::value*>> helperValueBoundaryDefault;
  if (root["Connections"].is_array())
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      const std::string srcGuid = lc(asStr(wv, "SourceParentOrChildId"));
      if (!isBoundaryGuid(srcGuid)) continue;                       // only boundary-fed Value inputs
      auto bd = boundaryDefaultByGuid.find(lc(asStr(wv, "SourceSlotId")));
      if (bd == boundaryDefaultByGuid.end()) continue;              // boundary has no vec default
      helperValueBoundaryDefault[lc(asStr(wv, "TargetParentOrChildId"))] =
          {lc(asStr(wv, "TargetSlotId")), bd->second};              // slot resolved in Pass 1
    }

  if (root["Children"].is_array()) {
    for (const crude_json::value& cv : root["Children"].get<crude_json::array>()) {
      if (!cv.is_object()) continue;
      const std::string childGuid = lc(asStr(cv, "Id"));
      const std::string sid = lc(asStr(cv, "SymbolId"));
      if (isImageFxSetupGuid(sid)) { fxChildGuid = childGuid; fxSetupCount++; continue; }
      // GradientsToTexture: ELIDED (gradientstotexture-elided-to-gradient-port). Not emitted as a child;
      // recorded so resolveEndpoint substitutes its Gradients source for any wire off its output.
      if (t3Lc(sid) == t3Lc(kGradientsToTextureGuid)) { gttChildGuids[childGuid] = true; continue; }
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
      // BOUNDARY-VEC-DEFAULT PLUMB: if this helper's Value HEAD is fed by a boundary carrying a typed vec
      // default, author each component onto the helper's Value.x/.y/.z/.w overrides (the KEPT fallback for
      // the UNWIRED-at-top case). Only when the wire truly targets the Value head (a boundary feeding some
      // OTHER helper slot must not hijack the Value default). An explicit InputValue on Value.* (rare) is
      // NOT overwritten — the .cs authored constant wins over the boundary default.
      if (auto hv = helperValueBoundaryDefault.find(childGuid); hv != helperValueBoundaryDefault.end())
        if (swSlotNameForGuid(helperType, hv->second.first) == kDecomposeValueHead && hv->second.second)
          for (const auto& [comp, val] : readVecDefault(*hv->second.second))
            if (!ch.overrides.count(comp)) ch.overrides[comp] = val;
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
  std::function<bool(const std::string&, const std::string&, int&, std::string&)> resolveEndpoint =
      [&](const std::string& guid, const std::string& slotGuid, int& outChild,
          std::string& outSlot) -> bool {
    // GradientsToTexture output → substitute its Gradients source (elision). GTT.out is the ONLY output
    // consumers wire, so ANY endpoint on a GTT child re-anchors to that source (recurse once — the source
    // is a boundary or a real helper, never another GTT in the wrappers we cover).
    auto gtt = gttChildGuids.find(guid);
    if (gtt != gttChildGuids.end()) {
      auto src = gttGradientSrc.find(guid);
      if (src == gttGradientSrc.end()) return false;  // GTT with unwired Gradients → consumer drops
      return resolveEndpoint(src->second.first, src->second.second, outChild, outSlot);
    }
    if (isBoundaryGuid(guid)) { outChild = kSymbolBoundary; outSlot = lc(slotGuid); return true; }
    auto it = childGuidToId.find(guid);
    if (it == childGuidToId.end()) return false;  // unknown (fx-setup handled by caller) → skip
    outChild = it->second;
    outSlot = swSlotNameForGuid(childIdToSwType[it->second], slotGuid);
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

      // Wire INTO an elided GradientsToTexture child (e.g. <gradient src> → GTT.Gradients) is dropped:
      // its source is re-anchored directly onto the atom when GTT.out → fxSetup.ImageB is processed
      // (resolveEndpoint substitutes it there). Keeping it here would create a source→source self-wire.
      if (gttChildGuids.count(dstGuid)) continue;

      if (dstIsFx) {
        // wire INTO the fx-setup child → re-anchor its TARGET onto the atom, keeping the SOURCE.
        int sChild; std::string sSlot;
        if (!resolveEndpoint(srcGuid, srcSlot, sChild, sSlot)) {
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
        if (!resolveEndpoint(dstGuid, dstSlot, dChild, dSlot)) {
          warn("t3: collapse fx-output target unresolved (dst=" + dstGuid + "), dropped");
          continue;
        }
        conns.push_back({atomId, port, dChild, dSlot});
        continue;
      }
      // Neither endpoint is the fx-setup child (boundary→helper, helper→helper) — keep the wire as-is,
      // resolving both endpoints in the collapsed graph.
      int sChild; std::string sSlot; int dChild; std::string dSlot;
      if (!resolveEndpoint(srcGuid, srcSlot, sChild, sSlot) ||
          !resolveEndpoint(dstGuid, dstSlot, dChild, dSlot)) {
        warn("t3: collapse non-fx wire unresolved (src=" + srcGuid + " dst=" + dstGuid + "), dropped");
        continue;
      }
      conns.push_back({sChild, sSlot, dChild, dSlot});
    }
  }
  sym.connections = std::move(conns);
  return true;
}

}  // namespace sw
