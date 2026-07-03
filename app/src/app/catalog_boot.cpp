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
#include <sstream>
#include <string>
#include <vector>

#include "runtime/t3_import.h"       // importT3Symbol / symbolIdOfT3 (append + cheap id peek)
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

  int imported = 0, skipped = 0;
  for (const fs::path& p : files) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { std::fprintf(stderr, "[catalog] cannot read: %s\n", p.string().c_str()); continue; }
    std::ostringstream ss; ss << f.rdbuf();
    const std::string json = ss.str();

    // IDEMPOTENT pre-check: peek the .t3's own Symbol id; if the live lib already has it, skip WITHOUT
    // touching it (importT3Symbol would overwrite the definition — unwanted for an already-open doc).
    std::string existingId;
    if (sw::symbolIdOfT3(json, &existingId) && g_lib().find(existingId)) { ++skipped; continue; }

    std::string symId;
    std::vector<std::string> warnings;
    const bool ok = sw::importT3Symbol(json, g_lib(), &symId, &warnings);
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
