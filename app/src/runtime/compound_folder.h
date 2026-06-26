// runtime/compound_folder — ADDITIVE folder-package (.swpkg) save/load. A SECOND on-disk shape
// for a SymbolLibrary, alongside the single-file .swproj (compound_save.h). Runtime leaf.
//
// Why it exists (file-management.md, the S20 named fork, compound_save.h:21): TiXL stores ONE
// symbol per .t3 file inside a package folder (Core/Model/SymbolPackage.cs:54-58 — `*.t3` enumerated
// recursively under the `Symbols` subfolder), NOT a single monolith. The recovery condition the
// spec names for that fork is "collaboration / large-project merge-conflict pain". This unit ENABLES
// that capability without forking the single-file format: the per-symbol bytes are produced by the
// SAME builder as .swproj (compound_save_internal.h::symbolToJsonObject), so a symbol diffs cleanly
// in its own file. Making the folder the DEFAULT (migrating existing projects) is 柏為's call and is
// deliberately NOT done here — .swproj stays the default; this is a strictly additive second path.
//
// Layout (sw, parity basis in the .cpp):
//   <name>.swpkg/
//     metadata.json          {formatVersion, rootSymbolId, composition}   (the root-level fields)
//     symbols/<symbolId>.t3  one compound symbol per file (= symbolToJsonObject's bytes)
// Atomic symbols are NEVER written (regenerated from the registry on load, same as .swproj).
//
// FORK (named, low-risk, justified in the .cpp): filename stem = symbol `id` (sw already forked
// TiXL's Guid into a stable readable string id), not TiXL's `Name` — gives filesystem uniqueness
// for free, no namespace-folder machinery, no name-collision handling. Extension `.t3` matches TiXL.
#pragma once
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolLibrary

namespace sw {

// Write `lib` as a folder package at `dir` (the .swpkg directory; created if absent). One .t3 per
// compound symbol under dir/symbols/, plus dir/metadata.json. The per-symbol bytes are identical to
// what the single-file writer emits for that symbol. Returns false on any filesystem failure.
bool saveLibToFolder(const std::string& dir, const SymbolLibrary& lib);

// Load a folder package written by saveLibToFolder: read metadata.json + every dir/symbols/*.t3,
// reassemble the equivalent single-file root JSON, and run it through the SAME tolerant two-phase
// loader as .swproj (libFromJsonAny) — so the cross-format invariant (folder load == file load)
// holds by construction and there is no second parser to drift. Local problems drop to `warnings`
// exactly as the file loader does. Returns false only when nothing usable parses (e.g. the dir or
// metadata.json is missing, or no symbol survives).
bool loadLibFromFolder(const std::string& dir, SymbolLibrary& out,
                       std::vector<std::string>* warnings = nullptr);

// Headless RED->GREEN proof (--selftest-folder-package): a ≥2-symbol lib with reuse round-trips
// through saveLibToFolder -> loadLibFromFolder graph-identical (symbol count + per-symbol nodes/
// connections/overrides), AND the CROSS-FORMAT invariant holds (.swproj-roundtrip graph ==
// .swpkg-roundtrip graph, byte-identical re-serialization). injectBug drops/corrupts one symbol
// file so the load detects the mismatch and the assertion FAILS (teeth).
int runFolderPackageSelfTest(bool injectBug);

}  // namespace sw
