// runtime/graph_bridge — flat editor Graph -> SymbolLibrary mirror (the production-swap
// seam). The app keeps editing the flat Graph; this bridge mirrors it into a SymbolLibrary
// whose root compound holds one child per node (child id == node id, overrides == stored
// params) so buildEvalGraph yields resident paths == the flat node id as a string. Paths
// are therefore frame-stable ACROSS rebuilds — per-path GPU buffers and stateful op state
// survive rebuild-on-edit, which is what makes the rebuild mirror correct as a first cut
// (incremental patchLib*/patch* wiring replaces the rebuild later, semantics already pinned
// by the patch goldens).
//
// This is NOT throwaway: libFromGraph is also the flat-.swproj importer — batch 2's v2
// loader uses it to migrate old files, forever.
#pragma once
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolLibrary / Symbol
#include "runtime/graph.h"           // Graph / NodeSpec

namespace sw {

// An atomic Symbol generated from a NodeSpec: inputDefs = every input port (Float ports
// carry the spec default; buffer/Command ports carry their dataType), outputDefs = the
// output ports. The symbol id IS the op type (atomic contract, compound_graph.h).
Symbol atomicSymbolFromSpec(const NodeSpec& s);

// The inverse for COMPOUNDS (批次 3, the lib-native canvas): a NodeSpec whose ports mirror
// the symbol's input/output defs, so a compound child gets pins/inspector exactly like an
// atomic node (= TiXL: a Symbol IS an operator). evaluate stays null — compounds evaluate by
// INLINE in the resident graph, never atomically. Float inputs get a placeholder ±10 slider
// range until the per-input Min/Max sidecar lands (S19, 批次 3 對表).
NodeSpec specFromSymbol(const Symbol& s);

// Rebuild the registry's DYNAMIC spec table from every compound in the lib (call after any
// edit that changes a compound's defs — cheap, a handful of compounds). findSpec resolves
// built-ins first, then these; stale entries vanish wholesale on each refresh.
void refreshCompoundSpecs(const SymbolLibrary& lib);

// Headless RED->GREEN proof: a compound's spec mirrors its defs (ports/dataType/def), atomic
// lookups stay registry-served (a compound named like an atomic cannot shadow it), refresh
// drops stale entries. injectBug skips the refresh after a def edit -> stale-port assertion
// FAILS (teeth).
int runCompoundSpecSelfTest(bool injectBug);

// Mirror a flat Graph into a SymbolLibrary: one atomic symbol per node type present (from
// the registry; unknown types are skipped) + one compound root. Wires whose endpoints or
// port indices don't resolve are skipped (same tolerance as the flat cook).
SymbolLibrary libFromGraph(const Graph& g, const std::string& rootId = "Root");

// Inverse: the lib's ROOT symbol back to a flat editor Graph (the TRANSITIONAL leg — the
// editor still edits flat; dies when the editor goes lib-native in 批次 3). Children become
// nodes (id/type/params=overrides/x,y), wires become pin connections. Returns false when the
// root contains a COMPOUND child (a flat graph cannot represent it — the caller refuses the
// file honestly instead of silently flattening). Boundary-sentinel wires (root's own
// input/output defs) have no flat equivalent and are skipped with a warning.
//
// ⚠ Contract is SEMANTIC, not byte-level (refuter-savev2 Q/R): v2 stores TiXL-shape 4-tuple
// wires with NO connection ids (rightly — flat conn ids are a v1 artifact), so this inverse
// REGENERATES conn ids + nextId deterministically. Topology/params/positions roundtrip
// exactly; identifiers normalize. Within a session nothing observes them across a save
// (doOpen re-snapshots + clears undo); the wart dies with the flat editor.
bool graphFromLib(const SymbolLibrary& lib, Graph& out,
                  std::vector<std::string>* warnings = nullptr);

// Headless RED->GREEN proof (REAL ops, real metallib): the default particle graph + a
// Const->Radius value wire + an AudioReaction->Speed wire, cooked N frames flat AND
// resident-via-bridge, must produce byte-identical target textures (stateful sim included);
// plus a direct probe that the resident AudioReaction extOut mirror resolves through the
// wire. injectBug drops a bridged connection -> the images diverge -> FAIL.
int runGraphBridgeSelfTest(bool injectBug);

}  // namespace sw
