// runtime/t3_import_collapse — the IMAGE-FX-WRAPPER → tex-atom collapse pass, split from t3_import.cpp
// (ARCHITECTURE.md rule 4: the combined file crossed the ≤400 line ratchet; the collapse is a distinct,
// self-contained pass). Pure CPU. See t3_import_internal.h for the shared helpers + the contract.
//
// A whole family of image .t3 ops are THIN wrappers around a TiXL image-fx-setup FRAMEWORK symbol
// (_multiImageFxSetupStatic/…); sw's op is ONE flat tex atom (ports 1:1 w/ the .cs) — collapses to that.
//
// TWO shapes are covered, unified by the SAME re-anchoring:
//   (1) SINGLE-fx-setup-child (HSE): the fx-setup child is the only child. Boundary Inputs wire straight
//       into it. Collapse → 1 atom child; boundary→fx wires re-anchor onto atom ports.
//   (2) MULTI-child (Blend, …): the fx-setup child is fed by HELPER VALUE ops (Vector4Components /
//       IntToFloat / BoolToFloat …) that decompose boundary vec/int/bool inputs into the scalar rails.
//       Those helpers are KEPT as real sw children; only the fx-setup child collapses. A wire INTO it
//       re-anchors onto the atom, keeping its SOURCE — a helper's X output feeding FloatParams[k] becomes
//       helperChild.X → atom.<④d order[k]> (non-fx wires are kept as-is).
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
// REDUNDANT-SUBGRAPH ELISION guids (GTT-gradient / TransformImage-passthrough / PickFloat-offset-routing) —
// each fork + its re-anchoring rule is documented at the extern declarations in t3_import_maps.h. In every
// case sw's collapsed atom already does the elided subgraph's work internally, so the wrapper's helper chain
// is redundant: a wire off the elided child's output re-anchors (via resolveEndpoint) to the SOURCE feeding
// its own input, and a wire INTO it is dropped (source lands on the atom when the output is followed). See
// kGradientsToTextureGuid / kTransformImageGuid / kPickFloatGuid & friends in the header.
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
// BOUNDARY-BOOL-DEFAULT PLUMB (collapse-boundary-bool-default-plumbed-through-kept-booltofloat): the SAME
// dropped-wire hazard as the vec plumb, but for a BOOL boundary default routed through a KEPT BoolToFloat
// helper. A .t3 bool boundary default is a SCALAR (JSON true/false), NOT an object, so the vec index above
// skips it. sw's BoolToFloat BoolValue port defaults to 0 (false); when the boundary→BoolValue wire drops
// (unwired at top), the helper emits ForFalse → the atom's bool scalar (e.g. PingPong) is forced to the
// false branch even when the .t3 boundary default is TRUE. This bit BoxGradient (PingPong default=TRUE →
// its center field collapsed to the PingPong=false projection). Fix: author the boundary's bool default onto
// the BoolToFloat helper's "BoolValue" override so the UNWIRED case emits the REAL default branch. The head
// port the BoolToFloat helper exposes for its bool input is "BoolValue".
const char* const kBoolToFloatBoolHead = "BoolValue";
// BOUNDARY-INT-DEFAULT PLUMB (collapse-boundary-int-default-plumbed-through-kept-inttofloat): the SAME
// dropped-wire hazard again, for an INT boundary default routed through a KEPT IntToFloat helper. A .t3 int
// boundary default is a SCALAR number (not object, not boolean). sw's IntToFloat IntValue port defaults to
// 0; when the boundary→IntValue wire drops (unwired at top), the helper emits 0 → the atom's int scalar
// (e.g. RemapColor.Mode, whose .t3 default is 1 = IndividualChannels) is forced to 0 (UseGrayScale) even
// when the .t3 boundary default is 1 → the field collapses to the wrong-mode projection. Fix: author the
// boundary's int default onto the IntToFloat helper's "IntValue" override. Head port = "IntValue".
const char* const kIntToFloatIntHead = "IntValue";
// Read a boundary DefaultValue object's {X,Y,Z,W} into an ordered [x,y,z,w] list (as many as present).
std::vector<std::pair<std::string, float>> readVecDefault(const crude_json::value& dv) {
  std::vector<std::pair<std::string, float>> out;
  if (!dv.is_object()) return out;
  for (const char* comp : {"X", "Y", "Z", "W"})
    if (dv.contains(comp) && dv[comp].is_number())
      out.push_back({std::string("Value.") + (char)std::tolower(comp[0]),
                     (float)dv[comp].get<crude_json::number>()});
  return out;
}
}  // namespace
bool collapseImageFxWrapper(const crude_json::value& root, const std::string& swType, Symbol& sym,
                            SymbolLibrary& lib, const std::function<void(const std::string&)>& warn,
                            const std::string& t3uiJson) {
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
  std::map<std::string, std::pair<std::string, std::string>> tiImageSrc;  // TransformImage guid → its Image wire src (guid,slot)
  std::map<std::string, bool> passthroughChildGuids;  // .t3 guids of ELIDED TransformImage/GenerateMips children
  std::map<std::string, std::pair<std::string, std::string>> pickValuesSrc;  // PickFloat guid → its FIRST FloatValues wire src
  std::pair<std::string, std::string> pickIndexSrc;  // the boundary src (guid,slot) feeding any PickFloat.Index (OffsetMode)
  std::map<std::string, bool> offsetRoutingChildGuids;  // .t3 guids of ELIDED PickFloat/Multiply offset-routing children
  int nextChildId = 1;

  // Pre-scan Connections for each GradientsToTexture child's Gradients-input SOURCE (the endpoint feeding
  // 588be11f) AND each TransformImage child's Image-input SOURCE (feeding 3aab9b12). Both elisions re-anchor
  // output consumers onto these sources. Done before Pass 1 so the child walk can skip them knowing the
  // source is captured. (A GTT/TransformImage with NO wired source is still elided; its consumers resolve
  // to the empty source → dropped, mirroring an unwired atom.Gradient.)
  if (root["Connections"].is_array())
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      const std::string tslot = lc(asStr(wv, "TargetSlotId"));
      if (tslot == t3Lc(kGradientsToTextureGradientsSlot))
        gttGradientSrc[lc(asStr(wv, "TargetParentOrChildId"))] = {lc(asStr(wv, "SourceParentOrChildId")),
                                                                  asStr(wv, "SourceSlotId")};
      else if (tslot == t3Lc(kTransformImageImageSlot))
        tiImageSrc[lc(asStr(wv, "TargetParentOrChildId"))] = {lc(asStr(wv, "SourceParentOrChildId")),
                                                              asStr(wv, "SourceSlotId")};
      else if (tslot == t3Lc(kPickFloatValuesSlot)) {
        // Capture ONLY the FIRST FloatValues wire per PickFloat (the raw Offset / RelativeToImage=0 branch;
        // the 2nd is the elided Multiply). PickFloat.out re-anchors to this raw source.
        const std::string pg = lc(asStr(wv, "TargetParentOrChildId"));
        if (!pickValuesSrc.count(pg))
          pickValuesSrc[pg] = {lc(asStr(wv, "SourceParentOrChildId")), asStr(wv, "SourceSlotId")};
      } else if (tslot == t3Lc(kPickFloatIndexSlot)) {
        // The OffsetMode boundary feeding PickFloat.Index → re-anchored onto the atom's OffsetMode port so
        // the atom re-selects itself. (One PickFloat in these wrappers → a single index source is enough.)
        pickIndexSrc = {lc(asStr(wv, "SourceParentOrChildId")), asStr(wv, "SourceSlotId")};
      }
    }

  // BOUNDARY-VEC-DEFAULT PLUMB pre-scan (collapse-boundary-typed-default-plumbed-through-kept-helper):
  //   (a) index each root boundary input's raw DefaultValue object by its slot guid (only vec objects
  //       matter; scalar/null are ignored — those defaults ride the atom's own port default already).
  //   (b) find every wire boundary → <helper child>.<its Vector Value HEAD slot guid>. That helper's
  //       Value input is boundary-fed; its typed default is the boundary's vec default. Keyed by the
  //       helper .t3 child guid so Pass 1 can author it as the helper's Value.x/.y/.z/.w overrides.
  std::map<std::string, const crude_json::value*> boundaryDefaultByGuid;  // boundary slot guid → vec DefaultValue
  std::map<std::string, float> boundaryBoolDefaultByGuid;                 // boundary slot guid → bool default (0/1)
  std::map<std::string, float> boundaryNumDefaultByGuid;                  // boundary slot guid → scalar number default (for IntToFloat)
  if (root["Inputs"].is_array())
    for (const crude_json::value& iv : root["Inputs"].get<crude_json::array>()) {
      if (!iv.is_object()) continue;
      const std::string sid = lc(asStr(iv, "Id"));
      if (sid.empty() || !iv.contains("DefaultValue")) continue;
      if (iv["DefaultValue"].is_object()) boundaryDefaultByGuid[sid] = &iv["DefaultValue"];
      else if (iv["DefaultValue"].is_boolean())                          // scalar bool boundary default
        boundaryBoolDefaultByGuid[sid] = iv["DefaultValue"].get<crude_json::boolean>() ? 1.0f : 0.0f;
      else if (iv["DefaultValue"].is_number())                           // scalar number default (int→IntToFloat)
        boundaryNumDefaultByGuid[sid] = (float)iv["DefaultValue"].get<crude_json::number>();
    }
  // helper .t3 child guid → (target slot guid, raw boundary DefaultValue object) for a boundary→helper
  // wire. Pass 1 confirms the slot resolves to the helper's Value HEAD before authoring the default.
  std::map<std::string, std::pair<std::string, const crude_json::value*>> helperValueBoundaryDefault;
  // helper .t3 child guid → (target slot guid, bool default 0/1) for a boundary→BoolToFloat.BoolValue wire.
  std::map<std::string, std::pair<std::string, float>> helperBoolBoundaryDefault;
  // helper .t3 child guid → (target slot guid, number default) for a boundary→IntToFloat.IntValue wire.
  std::map<std::string, std::pair<std::string, float>> helperIntBoundaryDefault;
  if (root["Connections"].is_array())
    for (const crude_json::value& wv : root["Connections"].get<crude_json::array>()) {
      if (!wv.is_object()) continue;
      const std::string srcGuid = lc(asStr(wv, "SourceParentOrChildId"));
      if (!isBoundaryGuid(srcGuid)) continue;                       // only boundary-fed Value inputs
      const std::string srcSlot = lc(asStr(wv, "SourceSlotId"));
      if (auto bd = boundaryDefaultByGuid.find(srcSlot); bd != boundaryDefaultByGuid.end())
        helperValueBoundaryDefault[lc(asStr(wv, "TargetParentOrChildId"))] =
            {lc(asStr(wv, "TargetSlotId")), bd->second};            // slot resolved in Pass 1
      else if (auto bb = boundaryBoolDefaultByGuid.find(srcSlot); bb != boundaryBoolDefaultByGuid.end())
        helperBoolBoundaryDefault[lc(asStr(wv, "TargetParentOrChildId"))] =
            {lc(asStr(wv, "TargetSlotId")), bb->second};            // slot resolved in Pass 1
      else if (auto bn = boundaryNumDefaultByGuid.find(srcSlot); bn != boundaryNumDefaultByGuid.end())
        helperIntBoundaryDefault[lc(asStr(wv, "TargetParentOrChildId"))] =
            {lc(asStr(wv, "TargetSlotId")), bn->second};            // slot resolved in Pass 1 (IntToFloat.IntValue only)
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
      // TransformImage / GenerateMips: ELIDED (transformimage-identity-passthrough-elided). TransformImage
      // is a no-op identity copy on the gradient row → its output re-anchors to its Image source (chained in
      // resolveEndpoint). GenerateMips is a dead bypassed branch → elided with no source (any consumer drops).
      if (t3Lc(sid) == t3Lc(kTransformImageGuid) || t3Lc(sid) == t3Lc(kGenerateMipsGuid)) {
        passthroughChildGuids[childGuid] = true; continue;
      }
      // PickFloat / Multiply: ELIDED (offset-routing-subgraph-elided-atom-reimplements). PickFloat.out
      // re-anchors to its raw-Offset FIRST FloatValues source; Multiply fed only PickFloat → dead → dropped.
      if (t3Lc(sid) == t3Lc(kPickFloatGuid) || t3Lc(sid) == t3Lc(kMultiplyOffsetGuid)) {
        offsetRoutingChildGuids[childGuid] = true; continue;
      }
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
      // OTHER helper slot must not hijack the Value default). The boundary WIRE supersedes any embedded
      // InputValue the .t3 left on the head slot (in TiXL a wired slot's embedded value is DEAD), so this
      // OVERWRITES the just-authored InputValue for those head components — the boundary default is the truth.
      if (auto hv = helperValueBoundaryDefault.find(childGuid); hv != helperValueBoundaryDefault.end())
        if (swSlotNameForGuid(helperType, hv->second.first) == kDecomposeValueHead && hv->second.second)
          for (const auto& [comp, val] : readVecDefault(*hv->second.second))
            ch.overrides[comp] = val;
      // BOUNDARY-BOOL-DEFAULT PLUMB: same rule for a BoolToFloat helper whose BoolValue HEAD is boundary-fed.
      // The boundary wire supersedes the child's embedded BoolValue InputValue (which BoxGradient.t3 leaves as
      // a STALE False on the PingPong BoolToFloat even though the boundary default is True) → OVERWRITE it.
      if (auto hb = helperBoolBoundaryDefault.find(childGuid); hb != helperBoolBoundaryDefault.end())
        if (swSlotNameForGuid(helperType, hb->second.first) == kBoolToFloatBoolHead)
          ch.overrides[kBoolToFloatBoolHead] = hb->second.second;
      // BOUNDARY-INT-DEFAULT PLUMB: same rule for an IntToFloat helper whose IntValue HEAD is boundary-fed.
      // The unwired-at-top case would otherwise emit 0; author the .t3 boundary int default (e.g.
      // RemapColor.Mode=1) so the kept IntToFloat feeds the atom's int scalar with the REAL default.
      if (auto hi = helperIntBoundaryDefault.find(childGuid); hi != helperIntBoundaryDefault.end())
        if (swSlotNameForGuid(helperType, hi->second.first) == kIntToFloatIntHead)
          ch.overrides[kIntToFloatIntHead] = hi->second.second;
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
  childGuidToId[fxChildGuid] = atomId;  // .t3ui LAYOUT: the atom stands in for the collapsed fx-setup child

  const std::vector<std::string>& floatOrder = swFloatParamOrderForCollapse(swType);
  int floatWireIdx = 0;  // positional index into FloatParams (2929c4c9) wires, in .t3 array order.

  // OFFSET-ROUTING re-anchor: if an offset-routing subgraph was elided, wire the OffsetMode boundary
  // (which fed the elided PickFloat.Index) onto the atom's OWN OffsetMode port so the atom re-selects the
  // offset itself. Only fires when a PickFloat was elided AND the atom exposes an "OffsetMode" port.
  auto atomHasPort = [&](const char* pid) {
    for (const PortSpec& ps : fs->ports) if (ps.id == pid) return true;
    return false;
  };
  std::vector<SymbolConnection> preConns;
  if (!offsetRoutingChildGuids.empty() && !pickIndexSrc.first.empty() &&
      isBoundaryGuid(pickIndexSrc.first) && atomHasPort("OffsetMode"))
    preConns.push_back({kSymbolBoundary, lc(pickIndexSrc.second), atomId, "OffsetMode"});

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
    // TransformImage output → substitute its Image source (transformimage-identity-passthrough-elided).
    // The identity copy has one output consumers wire, so ANY endpoint on it re-anchors to its Image source
    // (chains once more into the GTT above, then the Gradient boundary). GenerateMips (also in the set) has
    // no recorded source → its consumers drop (dead bypassed branch).
    auto pt = passthroughChildGuids.find(guid);
    if (pt != passthroughChildGuids.end()) {
      auto src = tiImageSrc.find(guid);
      if (src == tiImageSrc.end()) return false;  // TransformImage w/ unwired Image or a GenerateMips → drop
      return resolveEndpoint(src->second.first, src->second.second, outChild, outSlot);
    }
    // PickFloat/Multiply output → substitute the raw-Offset source (offset-routing-subgraph elision).
    // PickFloat.out re-anchors to its FIRST FloatValues source (the raw Offset). Multiply (which fed only
    // PickFloat) has no recorded source here → its consumers drop (its output already consumed by PickFloat).
    auto orte = offsetRoutingChildGuids.find(guid);
    if (orte != offsetRoutingChildGuids.end()) {
      auto src = pickValuesSrc.find(guid);
      if (src == pickValuesSrc.end()) return false;  // Multiply (or unwired PickFloat) → consumer drops
      return resolveEndpoint(src->second.first, src->second.second, outChild, outSlot);
    }
    if (isBoundaryGuid(guid)) { outChild = kSymbolBoundary; outSlot = lc(slotGuid); return true; }
    auto it = childGuidToId.find(guid);
    if (it == childGuidToId.end()) return false;  // unknown (fx-setup handled by caller) → skip
    outChild = it->second;
    outSlot = swSlotNameForGuid(childIdToSwType[it->second], slotGuid);
    return !outSlot.empty();
  };

  std::vector<SymbolConnection> conns = std::move(preConns);  // seed with the OffsetMode re-anchor (if any)
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
      // Same for a wire INTO an elided TransformImage/GenerateMips (e.g. GTT.out → TransformImage.Image):
      // the source is re-anchored when the passthrough's OUTPUT is followed (resolveEndpoint chains it).
      if (passthroughChildGuids.count(dstGuid)) continue;
      // Same for a wire INTO an elided PickFloat/Multiply (Offset/OffsetMode/Multiply.out → PickFloat.*,
      // Width/Offset → Multiply.*): dropped — the raw Offset is re-anchored via PickFloat.out, and the
      // OffsetMode boundary was already wired onto the atom's OffsetMode port (preConns above).
      if (offsetRoutingChildGuids.count(dstGuid)) continue;

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
        // COMPOUND-OUTPUT-DEF (imported-compound-needs-outputdefs-to-be-draggable-child): fx→BOUNDARY ⇒
        // dSlot is this compound's external output; give it a SlotDef from the terminal atom's OUTPUT port
        // so the dragged child gets an output PIN + viewProducerPath doesn't bail on empty defs (black cook).
        if (dChild == kSymbolBoundary && sym.outputDefs.empty())
          for (const PortSpec& p : fs->ports)
            if (!p.isInput) { sym.outputDefs.push_back({dSlot, p.name, p.dataType, p.def}); break; }
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
  applyT3uiPositions(sym, t3uiJson, childGuidToId);  // .t3ui LAYOUT: helpers + the collapsed atom
  return true;
}

}  // namespace sw
