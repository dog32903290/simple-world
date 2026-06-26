// runtime/compound_save_internal — PRIVATE shared seam between the single-file writer
// (compound_save.cpp) and the folder-package writer (compound_folder.cpp). NOT part of the
// public contract (compound_save.h): it leaks crude_json, so it stays out of the many headers
// that include compound_save.h (main.cpp, ui/*). Both .swproj and .swpkg writers build a symbol's
// on-disk JSON through the SAME builder here, so a symbol's bytes are IDENTICAL in both formats
// (the diff-friendly invariant the folder-package gap is for).
#pragma once
#include "crude_json.h"
#include "runtime/compound_graph.h"  // Symbol, SymbolLibrary

namespace sw {

// Build the per-symbol on-disk object — the EXACT element libToJsonV2 puts in its "symbols[]"
// array (id/name/nextChildId/inputDefs/outputDefs/children/connections/animator/symbolTags/
// annotations, all with the omit-at-default discipline). `lib` is needed only to resolve a
// child's symbolId to an atomic UUID vs a compound id (refOf). Atomic symbols are NEVER passed
// here (the caller filters to compounds first — atomics regenerate from the registry on load).
crude_json::value symbolToJsonObject(const Symbol& s, const SymbolLibrary& lib);

}  // namespace sw
