// app/document — the open project's core state: the default-lib accessor (g_lib), the session
// view-globals, and the lib revision (the resident-projection trigger). The thin coordinator.
// Navigation (composition path / combine / resident paths) lives in document_navigation.cpp;
// the new/open/save lifecycle + window title + dirty tracking + selftest live in document_io.cpp
// (rule-4 splits). Zone: app (product behaviour). Depends on runtime + platform only (never ui).
#include "app/document.h"

#include <string>

#include "runtime/graph.h"          // defaultParticleGraph (seed only)
#include "runtime/graph_bridge.h"   // libFromGraph (default-lib seed)

namespace sw::doc {

// Default doc. CONSTRUCT-ON-FIRST-USE (NOT pre-main static) so findSpec sees the live sink registrars; rationale in document.h.
SymbolLibrary& g_lib() {
  static SymbolLibrary inst = sw::libFromGraph(sw::defaultParticleGraph());
  return inst;
}
NS::Window* g_window = nullptr;
bool g_relayout = true;
std::string g_status = "ready";

namespace {
uint64_t g_libRevision = 1;    // bumped on every g_lib() mutation (projection contract, document.h)
}  // namespace

uint64_t libRevision() { return g_libRevision; }
void bumpLibRevision() { ++g_libRevision; }

}  // namespace sw::doc
