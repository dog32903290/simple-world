// runtime/compound_folder — the ADDITIVE folder-package (.swpkg) writer/reader. Contract +
// parity rationale: compound_folder.h. Runtime leaf (compound_graph + compound_save + crude_json
// + std::filesystem only; the same I/O footprint compound_save/load already carry).
//
// Parity basis (TiXL, READ-ONLY external/tixl):
//   • One symbol per file under a `Symbols` subfolder, enumerated `*.t3` RECURSIVELY:
//     Core/Model/SymbolPackage.cs:54-58 (SymbolSearchFiles). Extension `.t3`:
//     Core/Model/SymbolPackage.cs:390 (SymbolExtension = ".t3"). Subfolder name `Symbols`:
//     Core/Settings/FileLocations.cs:114 (ReleaseSymbolsSubfolder). We lowercase to `symbols/`
//     (cosmetic; the dir is ours).
//   • TiXL ALSO splits the UI metadata into a parallel `SymbolUis` subfolder (.t3ui) and the C#
//     source into `SourceCode` — sw's v2 already folds UI/boundary fields INLINE into the one
//     per-symbol object (the existing single-file inline fork, compound_graph.h:40-43) and has no
//     C# source, so a .swpkg symbol file is the WHOLE symbol (one `.t3`, no `.t3ui` sibling). That
//     is the pre-existing inline fork carried over, not a new divergence.
//
// FORK (named, justified): the per-symbol FILENAME stem is the symbol `id`, not TiXL's `Name`
// (TiXL: SymbolPathHandler.GetCorrectPath(symbol.Name, …) + namespace subfolders). sw already
// forked TiXL's Guid into a stable, human-readable string id — using it as the filename gives
// filesystem uniqueness for free (ids are unique by the lib's map key), needs no namespace-folder
// machinery, and no name-collision handling. Two safety wrinkles handled below: (1) an id may
// contain a path separator or be otherwise FS-hostile → we percent-style-escape to a safe stem and
// keep the real id INSIDE the file (the loader keys off the in-file id, never the filename), so a
// hostile id can never escape `symbols/`; (2) enumeration is recursive (TiXL parity) but the in-file
// id is the authority, so subfolder placement is irrelevant to load.
#include "runtime/compound_folder.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "crude_json.h"
#include "runtime/compound_save.h"           // libToJsonV2 / libFromJsonAny (the shared loader)
#include "runtime/compound_save_internal.h"  // symbolToJsonObject (byte-identical per-symbol bytes)

namespace sw {
namespace {
namespace fs = std::filesystem;

constexpr const char* kSymbolsSubfolder = "symbols";
constexpr const char* kSymbolExt = ".t3";
constexpr const char* kMetadataFile = "metadata.json";

// Map a symbol id to a SAFE filename stem. Most ids are already clean (e.g. "c-scale",
// "Compound-1"); only an id carrying a path separator or control/reserved char is rewritten. The
// real id always lives INSIDE the file, so this is purely about keeping the bytes on disk; load
// never trusts the filename. Deterministic (sorted by id on write → stable file set).
std::string safeStem(const std::string& id) {
  std::string out;
  out.reserve(id.size());
  for (char c : id) {
    unsigned char u = (unsigned char)c;
    // Allow ASCII letters/digits and a small safe punctuation set; escape everything else
    // (path separators, ':', control bytes, and — conservatively — any non-ASCII) as _XX hex.
    bool safe = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
                c == '-' || c == '_' || c == '.';
    if (safe && c != '.') {  // never leave a bare '.' that could form ".."/"." path tokens
      out += c;
    } else if (c == '.') {
      out += '.';  // a literal dot mid-id is fine (e.g. "Foo.Bar"); leading/standalone handled below
    } else {
      const char* hex = "0123456789abcdef";
      out += '_';
      out += hex[(u >> 4) & 0xF];
      out += hex[u & 0xF];
    }
  }
  // Refuse "" / "." / ".." stems (would collide or escape) — fall back to a hex of the raw id.
  if (out.empty() || out == "." || out == "..") {
    out = "sym";
    const char* hex = "0123456789abcdef";
    for (char c : id) { out += hex[((unsigned char)c >> 4) & 0xF]; out += hex[(unsigned char)c & 0xF]; }
  }
  return out;
}

std::string readFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

bool saveLibToFolder(const std::string& dir, const SymbolLibrary& lib) {
  std::error_code ec;
  fs::path root(dir);
  fs::path symbolsDir = root / kSymbolsSubfolder;
  fs::create_directories(symbolsDir, ec);
  if (ec) return false;

  // metadata.json: the root-level fields (everything libToJsonV2 puts OUTSIDE "symbols[]").
  // Same shape/keys so a human reads one file and knows the format version + root + composition.
  {
    crude_json::object meta;
    meta["formatVersion"] = (crude_json::number)2;
    meta["rootSymbolId"] = lib.rootId;
    crude_json::object comp;
    // finiteOr0 lives in compound_save.cpp's anon namespace; bpm/volume here come straight from the
    // in-memory model (already finite — the writer gate clamps on the value rail, not these).
    comp["bpm"] = (crude_json::number)lib.composition.bpm;
    comp["soundtrackPath"] = lib.composition.soundtrackPath;
    comp["soundtrackVolume"] = (crude_json::number)lib.composition.soundtrackVolume;
    meta["composition"] = crude_json::value(comp);
    std::ofstream mf(root / kMetadataFile, std::ios::binary);
    if (!mf) return false;
    mf << crude_json::value(meta).dump(2);
    if (!mf.good()) return false;
  }

  // One file per COMPOUND symbol (atomics regenerate from the registry on load, never written —
  // identical to the single-file path). Sorted by id for a deterministic file set. Each file's
  // bytes == symbolToJsonObject (the SAME builder libToJsonV2 uses), so a symbol diffs cleanly.
  std::vector<const Symbol*> compounds;
  for (const auto& kv : lib.symbols)
    if (!kv.second.atomic) compounds.push_back(&kv.second);
  std::sort(compounds.begin(), compounds.end(),
            [](const Symbol* a, const Symbol* b) { return a->id < b->id; });

  for (const Symbol* s : compounds) {
    fs::path file = symbolsDir / (safeStem(s->id) + kSymbolExt);
    std::ofstream sf(file, std::ios::binary);
    if (!sf) return false;
    sf << symbolToJsonObject(*s, lib).dump(2);
    if (!sf.good()) return false;
  }
  return true;
}

bool loadLibFromFolder(const std::string& dir, SymbolLibrary& out,
                       std::vector<std::string>* warnings) {
  std::error_code ec;
  fs::path root(dir);
  if (!fs::is_directory(root, ec)) return false;

  // metadata.json supplies the root-level fields. Absent/garbage metadata is tolerated the same way
  // the file loader tolerates an absent composition segment (S15): we fall back to formatVersion 2
  // and an empty root (the parser then picks the first compound as root, with a warning).
  crude_json::value meta = crude_json::value::parse(readFile(root / kMetadataFile));
  crude_json::object rootObj;
  rootObj["formatVersion"] = meta.is_object() && meta["formatVersion"].is_number()
                                 ? meta["formatVersion"]
                                 : crude_json::value((crude_json::number)2);
  if (meta.is_object() && meta["rootSymbolId"].is_string()) rootObj["rootSymbolId"] = meta["rootSymbolId"];
  if (meta.is_object() && meta["composition"].is_object()) rootObj["composition"] = meta["composition"];

  // Enumerate symbols/ recursively (TiXL SearchOption.AllDirectories parity). Each *.t3 parses to
  // one symbol object; bad/non-object files drop with a warning (S15) — one corrupt file never
  // sinks the load. The in-FILE id is the authority, so the filename is never trusted. Sorted by
  // path for a deterministic assembly order (the loader re-sorts by id on write anyway).
  fs::path symbolsDir = root / kSymbolsSubfolder;
  crude_json::array symbols;
  if (fs::is_directory(symbolsDir, ec)) {
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(symbolsDir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file(ec) && it->path().extension() == kSymbolExt) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
      crude_json::value sv = crude_json::value::parse(readFile(f));
      if (!sv.is_object() || !sv["id"].is_string()) {
        if (warnings) warnings->push_back("symbol file '" + f.filename().string() +
                                          "' is not a valid symbol object — dropped");
        continue;
      }
      symbols.push_back(sv);
    }
  }
  rootObj["symbols"] = crude_json::value(symbols);

  // Reassemble + hand off to the ONE tolerant parser (no second parser to drift). This is exactly
  // the JSON libToJsonV2 would have emitted for this lib, so folder-load == file-load by construction.
  return libFromJsonAny(crude_json::value(rootObj).dump(2), out, warnings);
}

}  // namespace sw
