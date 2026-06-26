#pragma once
// app/document_navigation — composition-path navigation split out of document.cpp (mechanical,
// rule 4). The canvas-navigation slice of the document: where the canvas is looking
// (g_compositionPath), the pure path getters, the once-per-frame validator, push/pop/truncate,
// Combine, and the resident path helpers. Zone: app. Owns the g_compositionPath definition.
//
// The PUBLIC declarations stay in document.h (callers unchanged); this header only carries the
// one file-private helper shared between this TU and the validator. The navigation functions
// themselves are declared in document.h and defined in document_navigation.cpp.
#include <cstddef>

namespace sw::doc {

// The number of leading path entries that still resolve (each element a child id in the symbol
// the previous prefix reaches). Shared by the pure getters (walk the valid prefix) and the
// per-frame validator (the only place that truncates). Defined in document_navigation.cpp.
size_t validPathPrefix();

}  // namespace sw::doc
