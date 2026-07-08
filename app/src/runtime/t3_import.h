// runtime/t3_import — the .t3 IMPORTER, forward-ported from the t3-importer spike onto MAIN and
// widened from the 1-atom spike map to the 6 LIVE buffer atoms (keystone首證). Parse one TiXL .t3
// file (JSON) into ONE Symbol inside a SymbolLibrary, preserving routing fidelity. The importer's
// whole job is the THREE-TABLE Guid→sw mapping (childId / slotName / symbolType), done once.
//
// FORWARD-PORT DELTA vs the spike (t3-importer/.../t3_import.cpp):
//   • Spike depended on a BARE step-1 atom (floats_to_buffer.h, its own floatsToBufferSpec provider
//     NOT in findSpec). Main already has FloatsToBuffer + 5 more buffer atoms LIVE (registered via
//     the BufferOp registrars → findSpec). So here every atom row's provider is NULL: resolve the
//     NodeSpec via findSpec, exactly the production registry the resident cook reads. No bare-atom
//     dependency, no step-3-cook fork — the atom Symbol generated into the lib is the SAME spec the
//     driver cooks.
//   • TABLE ③ widened from {FloatsToBuffer} to the 6 buffer atoms TransformPoints.t3 references that
//     sw actually HAS: FloatsToBuffer / IntsToBuffer / GetBufferComponents / GetSRVProperties /
//     ExecuteBufferUpdate / TransformsConstBuffer.
//   • TABLE ② widened with each atom's t3-slot-guid → sw-slot-NAME rows, hand-verified against the
//     TiXL .cs [Input]/[Output] Guid attributes (IntsToBuffer.cs / GetBufferComponents.cs /
//     GetSRVProperties.cs / ExecuteBufferUpdate.cs / FloatsToBuffer.cs / TransformsConstBuffer.cs).
//
// ★SCOPE (compute-stage keystone — now GREEN): TransformPoints.t3's 11 children ALL map now. The 5
//   formerly-unmapped ones gained sw atoms: ComputeShaderStage (generic bind CB/SRV/UAV + dispatch),
//   StructuredBufferWithViews (allocate the UAV target), TransformMatrix (existing SRT math atom),
//   CalcDispatchCount (existing value op; dispatch folded into the stage), and ComputeShader — which has
//   NO standalone atom: its Source string FOLDS onto the ComputeShaderStage's KernelName (fork
//   computeshader-source-folded-onto-stage, the post-pass in importT3Symbol). A handful of wires still
//   drop honestly (Dispatch/DispatchCount/ThreadGroupSize/CS-handle — folded or derived, no sw slot).
//   The replay reproduces the transform to parity — see t3import_transformpoints_golden.cpp (GREEN).
//
// ZONE: runtime (純計算). Pure CPU: JSON parse (crude_json) + three maps + SymbolLibrary fill.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolLibrary / Symbol / SymbolChild / SymbolConnection

namespace sw {

// COMPOUND-RECURSION SEAM (nested .t3 → nested sub-compound). A child whose SymbolId maps to NO sw
// atom might itself be a TiXL COMPOUND (a pure sub-graph .t3). To build it as a NESTED sub-compound
// (drag/dive-in) instead of skipping/flattening it, the importer must fetch that child's .t3 by guid.
// runtime is a filesystem-free leaf, so the guid→.t3 lookup is an INJECTED callback (= the
// setAssetTextureDecoder seam pattern): the APP zone (catalog_boot) builds a guid→file index and binds
// this resolver. Returns true + fills `outJson` when the guid is a known compound .t3; false = unknown
// (the child then falls back to today's "unmapped, skipped"). An EMPTY resolver ⇒ no recursion at all
// (byte-identical to the pre-seam single-layer importer — zero churn for every existing caller).
using T3Resolver = std::function<bool(const std::string& guid, std::string& outJson)>;

// Import one .t3 (JSON text) into `lib` as a single Symbol. id == the .t3 top-level Id; inputDefs
// from Inputs[]; children/connections = the mapped subgraph. Every atom a child's SymbolId resolves
// to is also generated into `lib` (atomicSymbolFromSpec via findSpec). A child whose SymbolId is a
// COMPOUND (not an atom) is recursively imported via `resolve` as a nested sub-compound (dedup +
// cycle-guard + depth cap); with no resolver it drops with a warning (today's behaviour). Returns
// false only when the JSON has no usable top-level Id. Local problems (unknown SymbolId, unresolvable
// slot/child guid) drop that element with a warning. `outSymbolId` receives the id; `warnings` skips.
bool importT3Symbol(const std::string& t3Json, SymbolLibrary& lib,
                    std::string* outSymbolId = nullptr,
                    std::vector<std::string>* warnings = nullptr);

// Recursion overload: same contract, plus `resolve` supplies nested compound children's .t3 by guid.
bool importT3Symbol(const std::string& t3Json, SymbolLibrary& lib, std::string* outSymbolId,
                    std::vector<std::string>* warnings, const T3Resolver& resolve);

// Test-only injection seam (compound-recursion RED case): force the recursion OFF even when a resolver
// is supplied — the nested compound child reverts to "unmapped, skipped" (single-layer flatten). Off in
// production. The nested-compound golden flips this around importT3Symbol to prove the recursion is the
// load-bearing seam (nested absent → the nested terminal path vanishes → downstream diverges).
bool& t3RecurseDisable();

// Cheap top-level Id peek: parse ONLY the root object's Id (comment-strip + crude_json + lowercase),
// matching the sym.id importT3Symbol would assign. Lets the boot catalog skip a .t3 whose symbol is
// already in the lib WITHOUT a full import. Returns false (outId untouched) when there is no usable Id.
bool symbolIdOfT3(const std::string& t3Json, std::string* outId);

// Test-only injection seam (routing RED case): reverse the connection order of the first MultiInput
// collision (two wires into the same (child,slot)) — corrupts ONLY the order, not the maps. Off in
// production. The keystone golden flips this around importT3Symbol.
bool& t3ImportInjectBug();

// KEYSTONE首證 golden (--selftest-t3-transformpoints): load the REAL TransformPoints.t3, walk the
// PRODUCTION path importT3Symbol → buildEvalGraph → cookResident, read back the point buffer, and
// compare against the焊死 oracle runTransformPointsParityProbe (xfprobe). Lives in
// t3import_transformpoints_golden.cpp. Returns 0 GREEN (parity holds) / 1 RED (seam gap). injectBug
// drives the importer's connection-order RED tooth. See that file for the measured result + the
// exact seam the keystone exposes.
int runT3TransformPointsParity(bool injectBug);

// COMPOUND-RECURSION keystone golden (--selftest-t3-nestedcompound): import the REAL DrawPointsDOF.t3
// (root) whose lone TransformPoints child is itself a compound .t3, via an in-memory T3Resolver so the
// importer RECURSES it into a nested sub-compound. Structural asserts (nested Symbol atomic==false +
// children non-empty, parent SymbolChild.symbolId==TP guid, the DrawPointsDOF→TransformPoints.Points
// cross-layer connection resolved = §1.3), then cook the NESTED TransformPoints terminal buffer through
// buildEvalGraph→cookResident and compare to the焊死 host-matrix oracle. Lives in
// t3import_nestedcompound_golden.cpp. injectBug flips t3RecurseDisable → nested reverts to skip → RED.
int runT3NestedCompoundParity(bool injectBug);

}  // namespace sw
