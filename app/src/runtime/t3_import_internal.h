// runtime/t3_import_internal — shared internals between t3_import.cpp (the JSON walk + normal per-child
// fill) and t3_import_collapse.cpp (the image-fx-wrapper→tex-atom collapse). Split out ONLY because the
// combined file crossed the ARCHITECTURE.md rule-4 line ratchet (≤400): the collapse is a distinct,
// self-contained pass. Tiny pure helpers are inline here so both TUs share one definition (no ODR risk).
#pragma once
#include <functional>
#include <string>
#include <utility>

#include "crude_json.h"
#include "runtime/compound_graph.h"   // Symbol / SymbolLibrary
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
bool collapseImageFxWrapper(const crude_json::value& root, const std::string& swType, Symbol& sym,
                            SymbolLibrary& lib,
                            const std::function<void(const std::string&)>& warn);

}  // namespace sw
