// runtime/compound_save — .swproj v2: the SymbolLibrary on disk (契約 3, 照 TiXL .t3 + 兩階段
// load). Runtime leaf (compound_graph + graph_bridge + crude_json only).
//
// Format decisions (the load-bearing ones, spec 健檢修正 S15-S20):
//   • v2 serializes ONLY compound symbols. Atomic operators live in the binary's NodeSpec
//     registry and are referenced by a FIXED UUID (承重決策 4) — the exact shape of TiXL,
//     where operator classes live in C# and .t3 references them by Guid. The loader
//     regenerates atomic Symbols from the registry (atomicSymbolFromSpec), so an atomic's
//     defs can never drift from the kernels that consume them.
//   • Compound symbols are SELF-DESCRIBING (S16): inputDefs/outputDefs carry id+name+
//     dataType+default, and ARRAY ORDER == DEFINITION ORDER (TiXL keeps display order in
//     C# member order; we own it in the file).
//   • Connections: array order == multi-input order (stable sort on write — S字段 multi-input
//     保序; today all inputs are single-cardinality, the order contract is pinned now).
//   • Load tolerance (S15, 世界觀級): bad data is dropped LOCALLY with a warning — missing
//     symbolId drops the child, a dangling/unresolvable wire drops the wire, a NEWER
//     formatVersion warns and proceeds. Load only fails when nothing usable parses. The next
//     save self-heals the file. (The v1 "malformed -> whole file false" philosophy is dead.)
//   • Version fields: v1 has NO formatVersion (recognized by absence) -> migrated through
//     fromJson + libFromGraph (the bridge IS the forever importer). v2 writes formatVersion 2.
//   • S20: single-file library ({formatVersion, rootSymbolId, symbols[]}) — named fork from
//     TiXL's one-symbol-one-file; revisit when versioning/diff pain arrives.
#pragma once
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolLibrary

namespace sw {

// Fixed UUID for an atomic operator type (stable across files/versions/renames — the
// indirection 承重決策 4 wants). Types not in the table get the derived stable id
// "sw-type:<type>" (assign a fixed UUID when the type ships). Returns "" for empty type.
std::string atomicUuidForType(const std::string& type);
// Inverse: "" when the id is not an atomic reference (i.e. it names a compound symbol).
std::string typeForAtomicUuid(const std::string& uuid);

std::string libToJsonV2(const SymbolLibrary& lib);
// Two-phase tolerant loader for ANY .swproj: v2 (formatVersion present) or legacy flat v1
// (no formatVersion -> migrate via libFromGraph, auto-upgrades on next save). Local problems
// are dropped with a human-readable line appended to `warnings`. Returns false only when the
// input yields no usable library.
bool libFromJsonAny(const std::string& json, SymbolLibrary& out,
                    std::vector<std::string>* warnings = nullptr);

bool saveLibToFile(const std::string& path, const SymbolLibrary& lib);
bool loadLibFromFile(const std::string& path, SymbolLibrary& out,
                     std::vector<std::string>* warnings = nullptr);

// Headless RED->GREEN proof: v2 roundtrip is byte-stable AND evaluation-identical (resident
// eval pre == post) for a compound lib with reuse; a legacy flat file migrates (counts +
// overrides intact); a doctored v2 (missing symbolId child + dangling wire + future
// formatVersion) loads with exactly the bad parts dropped + warnings. injectBug tampers an
// override after reload -> the evaluation-identical assertion FAILS (teeth).
int runSaveV2SelfTest(bool injectBug);

// Guards the checked-in N4 drill asset (testdata/compound_smoke.swproj): loads with zero
// repair warnings; the two Emitter instances resolve Radius 2.0 (definition default through
// the boundary wire) and 4.0 (instance override) — reuse isolation through the REAL file.
// injectBug pollutes the definition so isolation breaks -> FAILS (teeth).
int runTestProjSelfTest(bool injectBug);

// REFUTER-F probe (批次16 Lane F assertion 2): does the pinless `_ForceKind` discriminator leak
// to a user-corruptable surface via .swproj? Asserts it is NOT serialized at the spawn default,
// that a hand-corrupted `_ForceKind=99` override loads UNCLAMPED (documents the load gap), and
// that a non-numeric override is dropped. See compound_save_selftest.cpp for the three sub-checks.
int runForceKindCorruptProbe(bool injectBug);

}  // namespace sw
