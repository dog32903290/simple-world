// runtime/t3_import_internal — shared internals between t3_import.cpp (the JSON walk + normal per-child
// fill) and t3_import_collapse.cpp (the image-fx-wrapper→tex-atom collapse). Split out ONLY because the
// combined file crossed the ARCHITECTURE.md rule-4 line ratchet (≤400): the collapse is a distinct,
// self-contained pass. Tiny pure helpers are inline here so both TUs share one definition (no ODR risk).
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "crude_json.h"
#include "runtime/compound_graph.h"   // Symbol / SymbolLibrary
#include "runtime/t3_import.h"        // T3Resolver (compound-recursion callback type)
#include "runtime/t3_import_maps.h"   // t3Lc

namespace sw {
namespace t3i {

// guid normalization: .cs writes UPPERCASE, .t3 lowercase. Lowercase so the maps (keyed lc) match either.
inline std::string lc(std::string s) { return t3Lc(std::move(s)); }

constexpr const char* kGuidEmpty = "00000000-0000-0000-0000-000000000000";
inline bool isBoundaryGuid(const std::string& g) { return g.empty() || g == kGuidEmpty; }

inline std::string asStr(const crude_json::value& v, const char* key) {
  return v[key].is_string() ? v[key].get<crude_json::string>() : std::string();
}

// Strip TiXL inline `/* ... */` comments so crude_json can parse. Quote state tracked so a literal
// "/*" inside a JSON string value survives verbatim. Shared by t3_import.cpp (full walk) and the
// symbolIdOfT3 peek below (one definition here — no ODR risk, the reason this header exists).
inline std::string stripT3Comments(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  bool inStr = false;
  for (size_t i = 0; i < in.size(); ++i) {
    char c = in[i];
    if (inStr) {
      out.push_back(c);
      if (c == '\\' && i + 1 < in.size()) out.push_back(in[++i]);
      else if (c == '"') inStr = false;
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

}  // namespace t3i

// IMAGE-FX COLLAPSE (image-fx-wrapper-collapses-to-tex-atom): if `root` is a known image-fx wrapper
// (swType from swTexOpForCollapseRootGuid), collapse the whole compound to ONE sw tex atom (+ any helper
// value-op children kept as real children), filling `sym` (which already has id/name/inputDefs set) and
// registering atoms into `lib`. Returns true on success; false on a shape the tables don't yet cover
// (caller warns + falls through to the normal per-child path). Impl in t3_import_collapse.cpp.
// `t3uiJson` is the ROOT symbol's own sibling .t3ui text (may be empty) — the collapse pass applies it
// itself (its helper-child + collapsed-atom childGuidToId map is local to this function).
bool collapseImageFxWrapper(const crude_json::value& root, const std::string& swType, Symbol& sym,
                            SymbolLibrary& lib, const std::function<void(const std::string&)>& warn,
                            const std::string& t3uiJson);

// TEXTURE-COMPUTE COLLAPSE (TEXTURE_COMPUTE_SEAM_SPEC.md §3, t3_import_texcompute.cpp): if `root` is a
// texture-OUT compute compound (a ComputeShaderStage whose Uavs is fed by UavFromTexture2d — shape-
// detected, not guid-keyed), collapse the whole framework subgraph onto ONE tex-track
// ComputeShaderStageTex atom, filling `sym` (id/name/inputDefs already set by the caller) + registering
// the atom into `lib`. Returns true on a collapse (caller commits + returns), false on any non-match
// (SRV-tex read / extra input children / not the shape) → caller falls through to the normal per-child
// path. `t3uiJson` = the ROOT's own .t3ui (applied here; empty → ④layout N/A).
bool collapseTextureComputeStage(const crude_json::value& root, Symbol& sym, SymbolLibrary& lib,
                                 const std::function<void(const std::string&)>& warn,
                                 const std::string& t3uiJson);

// DataSet-timeline seam: read a child's Outputs[].OutputData TimeClip blocks (= SymbolJson.cs:112-131,
// Type "T3.Core.Animation.TimeClip") into `child.clips`, keyed by the sw output slot name. Split out of
// t3_import.cpp's Children loop ONLY for the rule-4 line ratchet (self-contained parse). No-op when the
// child has no Outputs array / no OutputData. Impl in t3_import_collapse.cpp.
void parseChildTimeClips(const crude_json::value& cv, const std::string& swType, SymbolChild& child,
                         const std::function<void(const std::string&)>& warn);

// ── COMPOUND-RECURSION SEAM internals (t3_import_recurse.cpp) ─────────────────────────────────────────
// Split out of t3_import.cpp for the rule-4 line ratchet (the walk+fill already sits at ~384 lines).

// The depth-carrying importer CORE (defined in t3_import.cpp). Both public importT3Symbol overloads
// forward here with depth 0; the recursion below re-enters it with depth+1. Declared here so the
// recursion helper (a separate TU) can call back into the core without a header cycle.
bool importT3SymbolImpl(const std::string& t3Json, SymbolLibrary& lib, std::string* outSymbolId,
                        std::vector<std::string>* warnings, const T3Resolver& resolve,
                        const T3LayoutResolver& layoutResolve, int depth);

// §1.3 CROSS-LAYER SLOT FALLBACK: resolve a t3 slot guid → sw slot NAME for a child whose swType may be
// a NESTED COMPOUND. An atom resolves via the maps (swSlotNameForGuid). A compound (atomic==false in
// lib) has NO map row — its external ports ARE its inputDefs/outputDefs, keyed by the very .t3 slot guid
// (Inputs[].Id / outputDefs). Without this, swSlotNameForGuid returns "" for a compound and the parent's
// overrides + cross-layer connections silently vanish (nested compound imported but父參數/接線全空). "" if
// neither an atom map row NOR a compound port matches.
std::string t3SlotNameForChildType(const SymbolLibrary& lib, const std::string& swType,
                                    const std::string& slotGuid);

// NESTED-COMPOUND RESOLUTION: a child's SymbolId mapped to NO atom. If `resolve` supplies its .t3 (a
// pure sub-graph compound) and it does not already exist / would not cycle / is within the depth cap,
// recursively import it into `lib` and return its guid (the swType the parent SymbolChild references).
// Returns "" when it should stay "unmapped, skipped" (no resolver, recursion disabled, resolver miss,
// cycle, depth cap, or nested import failure — `warn` records the reason). `parentGuid` is the symbol
// currently being built (cycle guard subject); `depth` is the current recursion depth (cap 64).
std::string t3ResolveNestedCompound(const std::string& parentGuid, const std::string& childGuid,
                                    const std::string& childSymbolId, SymbolLibrary& lib,
                                    std::vector<std::string>* warnings, const T3Resolver& resolve,
                                    const T3LayoutResolver& layoutResolve, int depth,
                                    const std::function<void(const std::string&)>& warn);

// ── .t3ui CANVAS-POSITION SEAM (t3_import_layout.cpp) ──────────────────────────────────────────────────
// Apply a symbol's OWN sibling .t3ui's SymbolChildUis[]/InputUis[]/OutputUis[] Position onto its already-
// built children + boundary defs. `childGuidToId` is THIS import's t3-child-guid→childId map (built by the
// walk in t3_import.cpp / t3_import_collapse.cpp — each has its own, scoped to what it actually committed).
// No-op when t3uiJson is empty (no resolver hit — today's all-0,0 behaviour, zero churn) or when
// t3LayoutDisable() is set (the layout golden's RED tooth).
void applyT3uiPositions(Symbol& sym, const std::string& t3uiJson,
                        const std::map<std::string, int>& childGuidToId);

}  // namespace sw
