// app/catalog_boot — the BOOT CATALOG loader (TiXL operator-library parity): on startup scan a managed
// folder of clean-replay `.t3` compounds and APPEND each into the live lib, so they are ALWAYS in the
// Add-Node / Cmd+F menu with no per-session re-import. Split out of document_io (rule 4: one file one
// duty — document_io owns new/open/save; this owns the one-shot boot-time catalog seed). Zone: app
// (product behaviour). Depends on runtime (t3 importer) + app/document only (never ui/platform).
#include "app/document.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/t3_import.h"       // importT3Symbol / symbolIdOfT3 / T3Resolver (append + peek + recurse)
#include "runtime/compound_graph.h"  // SymbolLibrary::find

namespace sw::doc {

// Scan `dir` for `.t3` files and APPEND each into the live lib. Mirrors the .swpkg multi-file loader in
// shape (fs enumerate → per-file import → warnings-not-crashes) but appends into g_lib() instead of
// swapping — nothing is discarded, so there is no unsaved-guard.
//
// IDEMPOTENT: a .t3 whose Symbol id is ALREADY in the lib is SKIPPED (never re-imports, never clobbers
// something being edited). This makes it safe to run after --open (a loaded project may already carry
// some of these). FAIL-SOFT: an unreadable / half-ported .t3 is skipped with a stderr warning — one bad
// file never aborts the batch and never crashes the editor. Bumps the revision only when the lib grew.
int loadCatalogFromFolder(const std::string& dir, bool quiet) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    std::fprintf(stderr, "[catalog] no catalog folder: %s\n", dir.c_str());
    return 0;
  }
  // Stable order (deterministic boot log regardless of the filesystem's enumeration order).
  std::vector<fs::path> files;
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    const fs::path& p = it->path();
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".t3") files.push_back(p);
  }
  std::sort(files.begin(), files.end());

  // guid→.t3 INDEX for the COMPOUND-RECURSION resolver (app zone does the filesystem; runtime stays a
  // leaf). Peek each file's top-level Symbol id once, so a catalog compound whose child is ANOTHER
  // catalog compound is RECURSED into a nested sub-compound instead of skipped. Scoped to THIS folder
  // (no 925-wide Lib scan — that is an independent production-scale decision); a nested-compound child
  // whose guid is not one of these files still drops with today's "unmapped, skipped" warning.
  std::map<std::string, std::string> jsonByGuid;
  std::map<fs::path, std::string> jsonByFile;
  for (const fs::path& p : files) {
    std::ifstream f(p, std::ios::binary);
    if (!f) continue;
    std::ostringstream ss; ss << f.rdbuf();
    std::string json = ss.str();
    std::string id;
    if (sw::symbolIdOfT3(json, &id) && !id.empty()) jsonByGuid[id] = json;
    jsonByFile[p] = std::move(json);
  }
  const sw::T3Resolver resolver = [&jsonByGuid](const std::string& guid, std::string& out) -> bool {
    auto it = jsonByGuid.find(guid);
    if (it == jsonByGuid.end()) return false;
    out = it->second;
    return true;
  };

  // .t3ui LAYOUT index (柏為 07-08 "鑽入複合子節點全擠在原點" fix): a SIBLING "<Name>.t3ui" carries the
  // SAME .t3's canvas positions (SymbolChildUis[]/InputUis[]/OutputUis[]), keyed by its OWN top-level "Id"
  // (== the .t3's Id — same peek). Missing sibling for a given .t3 is fine (that compound's children just
  // stay at the pre-seam default 0,0 — no import ever fails for a missing .t3ui).
  std::map<std::string, std::string> t3uiByGuid;
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    const fs::path& p = it->path();
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".t3ui") continue;
    std::ifstream f(p, std::ios::binary);
    if (!f) continue;
    std::ostringstream ss; ss << f.rdbuf();
    std::string json = ss.str();
    std::string id;
    if (sw::symbolIdOfT3(json, &id) && !id.empty()) t3uiByGuid[id] = std::move(json);
  }
  const sw::T3LayoutResolver layoutResolver =
      [&t3uiByGuid](const std::string& guid, std::string& out) -> bool {
    auto it = t3uiByGuid.find(guid);
    if (it == t3uiByGuid.end()) return false;
    out = it->second;
    return true;
  };

  int imported = 0, skipped = 0;
  for (const fs::path& p : files) {
    auto jit = jsonByFile.find(p);
    if (jit == jsonByFile.end()) { std::fprintf(stderr, "[catalog] cannot read: %s\n", p.string().c_str()); continue; }
    const std::string& json = jit->second;

    // IDEMPOTENT pre-check: peek the .t3's own Symbol id; if the live lib already has it, skip WITHOUT
    // touching it (importT3Symbol would overwrite the definition — unwanted for an already-open doc).
    std::string existingId;
    if (sw::symbolIdOfT3(json, &existingId) && g_lib().find(existingId)) { ++skipped; continue; }

    std::string symId;
    std::vector<std::string> warnings;
    const bool ok = sw::importT3Symbol(json, g_lib(), &symId, &warnings, resolver, layoutResolver);
    for (const std::string& w : warnings)
      std::fprintf(stderr, "[catalog] %s: %s\n", p.filename().string().c_str(), w.c_str());
    if (!ok || symId.empty()) {
      std::fprintf(stderr, "[catalog] import failed (skipped): %s\n", p.filename().string().c_str());
      continue;
    }
    ++imported;
  }
  if (imported > 0) {
    bumpLibRevision();       // frame_cook → refreshCompoundSpecs registers the new symbols next frame
    invalidateDirtyCache();  // the lib grew → re-light the • against the saved snapshot
  }
  if (!quiet)
    g_status = "catalog: " + std::to_string(imported) + " loaded, " +
               std::to_string(skipped) + " already present";
  std::fprintf(stderr, "[catalog] %s -> %d imported, %d already present\n",
               dir.c_str(), imported, skipped);
  return imported;
}

}  // namespace sw::doc
