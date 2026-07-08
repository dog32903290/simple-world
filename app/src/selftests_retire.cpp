// app/src/selftests_retire.cpp — RETIREMENT-SWEEP mechanical pre-flight tools (退場戰役 §2 / §5).
//
// Shell-tier (app/src/ root, like selftests.cpp): may name runtime importer + map fns from any zone.
// Two tools, both READ-ONLY over the import/registry machinery (they never mutate a live lib, never
// touch node_registry/graph_bridge/t3_import_maps — those are other lanes' locks; this file only
// FEEDS the production importer and READS its verdict):
//
//   1. runProbeImport(<path.t3>)  — the ready-set scanner (§2). Non-uniform CLI entry dispatched from
//      the selftests.cpp router as `--probe-import <path>`. Feeds ONE .t3 through the production
//      importT3Symbol + a sibling-folder resolver and prints a machine-grep verdict line + exit code:
//         PROBE <name>: READY                       (exit 0)  R1+R2+R3 all pass
//         PROBE <name>: NOT-READY (R2 unmapped: …)  (exit 2)  file missing / import failed / unmapped
//         PROBE <name>: EXCLUDED-COLLAPSE           (exit 3)  root guid ∈ collapse-8 (不可退, §3)
//
//   2. runLintCatalogNames(injectBug) — the catalog name-uniqueness lint (§5 風險#1). A registered
//      selftest (`--lint-catalog-names`): imports every assets/catalog_t3/*.t3 root and hard-fails if
//      any two non-atomic compounds share the readable NAME the §1 name-fallback keys on. injectBug
//      splices a synthetic duplicate so the lint's RED path is a real tooth (--bite).
#include "selftests.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>

#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS (lint row)
#include "runtime/compound_graph.h"     // SymbolLibrary / Symbol
#include "runtime/point_graph.h"        // registerBuiltinPointOps (atoms → findSpec resolves them)
#include "runtime/t3_import.h"          // importT3Symbol / symbolIdOfT3 / T3Resolver / T3LayoutResolver
#include "runtime/t3_import_maps.h"     // swTexOpForCollapseRootGuid (collapse-8 hard-exclude set)

namespace sw {

namespace {

namespace fs = std::filesystem;

// Read a whole file into `out`. false when it cannot be opened (R1 for the probe).
bool readFile(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

// Build the SAME sibling-folder resolver pair catalog_boot.cpp uses, over `dir`: a guid→.t3 index (so a
// nested-compound child resolves + recurses) and a guid→.t3ui index (canvas layout). Scoped to the one
// folder the probed .t3 lives in — identical resolution to the production boot catalog for that folder.
struct FolderIndex {
  std::map<std::string, std::string> jsonByGuid;
  std::map<std::string, std::string> t3uiByGuid;
};

FolderIndex indexFolder(const fs::path& dir) {
  FolderIndex idx;
  std::error_code ec;
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    const fs::path& p = it->path();
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".t3" && ext != ".t3ui") continue;
    std::string json;
    if (!readFile(p, json)) continue;
    std::string id;
    if (!sw::symbolIdOfT3(json, &id) || id.empty()) continue;
    if (ext == ".t3")
      idx.jsonByGuid[id] = std::move(json);
    else
      idx.t3uiByGuid[id] = std::move(json);
  }
  return idx;
}

// Extract the SymbolId guid a "…unmapped SymbolId <guid> (no sw atom…), skipped" warning names (R2).
// "" when the warning is not an unmapped-skip line.
std::string unmappedGuidOf(const std::string& warning) {
  static const char* kKey = "unmapped SymbolId ";
  const size_t k = warning.find(kKey);
  if (k == std::string::npos) return std::string();
  size_t s = k + std::strlen(kKey);
  size_t e = s;
  while (e < warning.size() && warning[e] != ' ') ++e;
  return warning.substr(s, e - s);
}

// Import one catalog root into a fresh lib and return its readable NAME (Symbol.name — what §1's
// name-fallback keys on) when it lands as a non-atomic compound. "" when the import failed or the root
// came back atomic (nothing name-fallback would ever register). `dir` supplies the sibling resolver.
std::string importedRootName(const std::string& json, const fs::path& dir) {
  const FolderIndex idx = indexFolder(dir);
  const sw::T3Resolver resolver = [&idx](const std::string& guid, std::string& out) -> bool {
    auto it = idx.jsonByGuid.find(guid);
    if (it == idx.jsonByGuid.end()) return false;
    out = it->second;
    return true;
  };
  const sw::T3LayoutResolver layoutResolver = [&idx](const std::string& guid, std::string& out) -> bool {
    auto it = idx.t3uiByGuid.find(guid);
    if (it == idx.t3uiByGuid.end()) return false;
    out = it->second;
    return true;
  };
  sw::SymbolLibrary lib;
  std::string symId;
  std::vector<std::string> warnings;
  const bool ok = sw::importT3Symbol(json, lib, &symId, &warnings, resolver, layoutResolver);
  if (!ok || symId.empty()) return std::string();
  const sw::Symbol* s = lib.find(symId);
  if (!s || s->atomic) return std::string();
  return s->name;
}

}  // namespace

// ── §2 ready-set scanner ────────────────────────────────────────────────────────────────────────────
int runProbeImport(const char* t3Path) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();  // populate the atom registry so importT3Symbol's findSpec resolves leaves

  const fs::path path(t3Path ? t3Path : "");
  const std::string name = path.stem().string();  // display name = filename stem (e.g. TransformPoints)
  auto done = [&](int rc) { std::fflush(stdout); pool->release(); return rc; };

  // R1: the .t3 exists + is readable. A missing file is NOT-READY (nothing to retire yet).
  std::string json;
  if (!readFile(path, json)) {
    std::printf("PROBE %s: NOT-READY (R1 file missing: %s)\n", name.c_str(), t3Path ? t3Path : "");
    return done(2);
  }

  // collapse-8 HARD EXCLUDE (§3): a collapse root's flat atom IS the implementation body — retiring it
  // guts the薄殼 compound. Checked BEFORE import (a collapse root DOES import as a non-atomic薄殼, so it
  // would false-READ as READY). swTexOpForCollapseRootGuid is the sweep's SSOT exclude set.
  std::string rootGuid;
  if (sw::symbolIdOfT3(json, &rootGuid) && !rootGuid.empty() &&
      !swTexOpForCollapseRootGuid(rootGuid).empty()) {
    std::printf("PROBE %s: EXCLUDED-COLLAPSE\n", name.c_str());
    return done(3);
  }

  // R2 (+R3 by construction): feed the PRODUCTION importer + sibling-folder resolver. Ready ⇔ the root
  // imports as a non-atomic compound WITH children AND zero "unmapped SymbolId … skipped" warnings. An
  // unmapped child (framework code-op OR an unmapped leaf) leaves an R2/R3 warning → NOT-READY.
  const FolderIndex idx = indexFolder(path.parent_path());
  const sw::T3Resolver resolver = [&idx](const std::string& guid, std::string& out) -> bool {
    auto it = idx.jsonByGuid.find(guid);
    if (it == idx.jsonByGuid.end()) return false;
    out = it->second;
    return true;
  };
  const sw::T3LayoutResolver layoutResolver = [&idx](const std::string& guid, std::string& out) -> bool {
    auto it = idx.t3uiByGuid.find(guid);
    if (it == idx.t3uiByGuid.end()) return false;
    out = it->second;
    return true;
  };
  sw::SymbolLibrary lib;
  std::string symId;
  std::vector<std::string> warnings;
  const bool ok = sw::importT3Symbol(json, lib, &symId, &warnings, resolver, layoutResolver);

  // Collect the unmapped guids the importer skipped (deduped, stable order).
  std::vector<std::string> unmapped;
  for (const std::string& w : warnings) {
    const std::string g = unmappedGuidOf(w);
    if (!g.empty() && std::find(unmapped.begin(), unmapped.end(), g) == unmapped.end())
      unmapped.push_back(g);
  }

  const sw::Symbol* s = (ok && !symId.empty()) ? lib.find(symId) : nullptr;
  const bool nonAtomic = s && !s->atomic && !s->children.empty();
  if (!unmapped.empty() || !nonAtomic) {
    std::string list;
    for (size_t i = 0; i < unmapped.size(); ++i) list += (i ? "," : "") + unmapped[i];
    if (list.empty()) list = !ok ? "import-failed" : (!s ? "no-symbol" : (s->atomic ? "atomic" : "no-children"));
    std::printf("PROBE %s: NOT-READY (R2 unmapped: %s)\n", name.c_str(), list.c_str());
    return done(2);
  }

  std::printf("PROBE %s: READY\n", name.c_str());
  return done(0);
}

// ── §5 catalog name-uniqueness lint ──────────────────────────────────────────────────────────────────
// Imports every assets/catalog_t3/*.t3 root and asserts the readable NAMEs (what §1's name-fallback
// keys on) are all distinct — two same-named non-atomic compounds make the name→compound fallback
// ambiguous the moment BOTH are in the catalog. PASS + exit 0 when unique; hard-fail + exit 1 on a
// collision. injectBug splices a synthetic duplicate of the FIRST name so the RED path is a real tooth.
int runLintCatalogNames(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();  // atoms → findSpec resolves each root's mapped leaves during import

#ifdef SW_CATALOG_T3_DIR
  const fs::path dir(SW_CATALOG_T3_DIR);
#else
  const fs::path dir("assets/catalog_t3");
#endif

  std::error_code ec;
  std::vector<fs::path> files;
  for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    const fs::path& p = it->path();
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".t3") files.push_back(p);
  }
  std::sort(files.begin(), files.end());

  // name → the file(s) that produced it (readable Symbol.name of each non-atomic root).
  std::map<std::string, std::vector<std::string>> byName;
  for (const fs::path& p : files) {
    std::string json;
    if (!readFile(p, json)) continue;
    const std::string nm = importedRootName(json, dir);
    if (nm.empty()) continue;  // atomic / import-failed root registers no name key → not a collision risk
    byName[nm].push_back(p.filename().string());
  }

  // injectBug: splice a synthetic duplicate of the first name under a fake filename → forces a collision
  // so the lint's hard-fail is a real RED tooth (--bite) without mutating the on-disk catalog.
  if (injectBug && !byName.empty())
    byName.begin()->second.push_back("__injected_dup__.t3");

  int collisions = 0;
  for (const auto& kv : byName) {
    if (kv.second.size() < 2) continue;
    ++collisions;
    std::string joined;
    for (size_t i = 0; i < kv.second.size(); ++i) joined += (i ? ", " : "") + kv.second[i];
    std::printf("[lint-catalog-names] CONFLICT name \"%s\" claimed by: %s\n", kv.first.c_str(),
                joined.c_str());
  }

  const bool pass = (collisions == 0);
  std::printf("[lint-catalog-names] %d catalog compound name(s), %d collision(s) -> %s\n",
              (int)byName.size(), collisions, pass ? "PASS" : "FAIL");
  std::fflush(stdout);
  pool->release();
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/960, {"lint-catalog-names", runLintCatalogNames});

}  // namespace sw
