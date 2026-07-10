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

// .T3UI LAYOUT SEAM (canvas positions — a SIBLING file, not the .t3 itself). TiXL serializes a Symbol's
// child/boundary-port canvas positions into "<Name>.t3ui" next to "<Name>.t3" (SymbolChildUis[]/InputUis[]/
// OutputUis[], keyed by guid — Editor/UiModel/SymbolUiJson.cs). runtime stays a filesystem-free leaf, so
// guid→.t3ui-text is ALSO an injected callback (same shape as T3Resolver, same seam pattern). Keyed by the
// SYMBOL's own guid (== the .t3's top-level Id == the .t3ui's own "Id") — each recursion depth looks up ITS
// OWN sibling .t3ui, not the parent's. Empty resolver / no match ⇒ no layout at all (every position stays
// 0,0 — byte-identical to today, zero churn for every existing caller).
using T3LayoutResolver = std::function<bool(const std::string& symbolGuid, std::string& outJson)>;

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

// Layout overload: same contract, plus `layoutResolve` supplies each imported Symbol's OWN sibling .t3ui
// (child + boundary-port canvas positions) by that Symbol's own guid. Applies to the root AND every
// recursed nested compound (each looks up its own .t3ui at its own guid). No layoutResolve match for a
// given symbol ⇒ that symbol's positions stay 0,0 (today's behaviour) — never blocks the import itself.
bool importT3Symbol(const std::string& t3Json, SymbolLibrary& lib, std::string* outSymbolId,
                    std::vector<std::string>* warnings, const T3Resolver& resolve,
                    const T3LayoutResolver& layoutResolve);

// Test-only injection seam (compound-recursion RED case): force the recursion OFF even when a resolver
// is supplied — the nested compound child reverts to "unmapped, skipped" (single-layer flatten). Off in
// production. The nested-compound golden flips this around importT3Symbol to prove the recursion is the
// load-bearing seam (nested absent → the nested terminal path vanishes → downstream diverges).
bool& t3RecurseDisable();

// Test-only injection seam (layout RED case): force applyT3uiPositions to no-op even with a matching
// .t3ui — every child/boundary-port position reverts to 0,0. Off in production. The layout golden flips
// this to prove the .t3ui read is the load-bearing seam (absent ⇒ every position collapses to the origin,
// = the exact bug 柏為 observed diving into an imported compound).
bool& t3LayoutDisable();

// Test-only injection seam (texture-compute ①takeover RED case): force collapseTextureComputeStage to
// no-op → the texture-OUT compute framework subgraph (Texture2d/UavFromTexture2d/CalcInt2DispatchCount/
// ExecuteTextureUpdate) falls to the normal per-child path (those glue ops become "unmapped skipped")
// and the stage imports as the buffer ComputeShaderStage, not the tex atom. Off in production. The
// brdflookup golden flips this to prove the collapse is the load-bearing takeover seam.
bool& t3TexComputeCollapseDisable();

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

// 187 量產第一波 replay golden (--selftest-t3-combinebuffers): import the REAL CombineBuffers.t3, walk
// importT3Symbol→buildEvalGraph→cookResident, read back the combined Point buffer and compare against an
// INDEPENDENT concatenation oracle (bag0++bag1++bag2). Lives in t3import_combinebuffers_golden.cpp.
// Returns 0 GREEN / 1 RED; injectBug reverses the InputBuffers wire order → concat diverges → RED.
int runT3CombineBuffersParity(bool injectBug);

// COMPOUND-RECURSION keystone golden (--selftest-t3-nestedcompound): import the REAL DrawPointsDOF.t3
// (root) whose lone TransformPoints child is itself a compound .t3, via an in-memory T3Resolver so the
// importer RECURSES it into a nested sub-compound. Structural asserts (nested Symbol atomic==false +
// children non-empty, parent SymbolChild.symbolId==TP guid, the DrawPointsDOF→TransformPoints.Points
// cross-layer connection resolved = §1.3), then cook the NESTED TransformPoints terminal buffer through
// buildEvalGraph→cookResident and compare to the焊死 host-matrix oracle. Lives in
// t3import_nestedcompound_golden.cpp. injectBug flips t3RecurseDisable → nested reverts to skip → RED.
int runT3NestedCompoundParity(bool injectBug);

// .T3UI LAYOUT golden (--selftest-t3-layout): import the REAL TransformPoints.t3 with its REAL sibling
// TransformPoints.t3ui through the PRODUCTION path importT3Symbol(…, layoutResolve) and assert every
// child + boundary pin lands at its EXACT .t3ui Position constant (not sw's own output — the .t3ui file's
// literal numbers), proving the 柏為-observed "dive-in → children all stacked at (0,0)" bug is fixed.
// injectBug flips t3LayoutDisable() → every position reverts to 0,0 → RED. Lives in t3import_layout_golden.cpp.
int runT3LayoutGolden(bool injectBug);

// 廢棄節點退場 PILOT harness (--selftest-t3-transformpoints-retire): four gates proving the RETIRED flat
// TransformPoints atom's references are auto-taken-over by the .t3 compound via the replace-in-place seam
// (refreshCompoundSpecs name alias + findSpec's dynamicSpecs tail). ① takeover polarity (name→compound,
// injectBug=stand-in atom shadows) + ③ reference reachability/cookability (injectBug=drop alias→nullptr) +
// ② parity (delegates runT3TransformPointsParity) + ④ layout (delegates runT3LayoutGolden). Lives in
// t3import_transformpoints_retire_golden.cpp. See docs/agent/RETIREMENT_BATTLE_SPEC.md §7.
int runT3TransformPointsRetireGates(bool injectBug);

// 廢棄節點退場 PILOT #2 harness (--selftest-t3-combinebuffers-retire): the SAME four gates for the RETIRED
// flat CombineBuffers atom, proving the replace-in-place seam is NOT TransformPoints-specific. ① takeover
// polarity (name→compound guid 4dd8a618…, injectBug=stand-in atom shadows) + ③ reference reachability/
// cookability (injectBug=drop alias→nullptr) + ② parity (delegates runT3CombineBuffersParity) + ④ layout
// (inline .t3ui Position constants). Lives in t3import_combinebuffers_retire_golden.cpp. See §7.2.
int runT3CombineBuffersRetireGates(bool injectBug);

}  // namespace sw
