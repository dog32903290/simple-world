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
#include "runtime/compound_graph.h"  // SymbolLibrary / Symbol
#include "runtime/graph.h"           // Graph / NodeSpec

namespace sw {

// An atomic Symbol generated from a NodeSpec: inputDefs = every input port (Float ports
// carry the spec default; buffer/Command ports carry their dataType), outputDefs = the
// output ports. The symbol id IS the op type (atomic contract, compound_graph.h).
Symbol atomicSymbolFromSpec(const NodeSpec& s);

// Mirror a flat Graph into a SymbolLibrary: one atomic symbol per node type present (from
// the registry; unknown types are skipped) + one compound root. Wires whose endpoints or
// port indices don't resolve are skipped (same tolerance as the flat cook).
SymbolLibrary libFromGraph(const Graph& g, const std::string& rootId = "Root");

// Headless RED->GREEN proof (REAL ops, real metallib): the default particle graph + a
// Const->Radius value wire + an AudioReaction->Speed wire, cooked N frames flat AND
// resident-via-bridge, must produce byte-identical target textures (stateful sim included);
// plus a direct probe that the resident AudioReaction extOut mirror resolves through the
// wire. injectBug drops a bridged connection -> the images diverge -> FAIL.
int runGraphBridgeSelfTest(bool injectBug);

}  // namespace sw
