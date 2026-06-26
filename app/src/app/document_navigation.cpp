// app/document_navigation — composition-path navigation (split from document.cpp, rule 4).
// Zone: app (product behaviour). Depends on runtime + platform only (never ui).
#include "app/document.h"
#include "app/document_navigation.h"

#include <string>
#include <vector>

#include "app/command.h"
#include "runtime/combine.h"  // combineChildren (批次 4)

namespace sw::doc {

// Where the canvas is looking (document.h contract). Defined here — the navigation TU owns it.
std::vector<int> g_compositionPath;

// The number of leading path entries that still resolve (each element a child id in the
// symbol the previous prefix reaches). Shared by the pure getter (walks the valid prefix)
// and the per-frame validator (the only place that truncates).
size_t validPathPrefix() {
  std::string cur = g_lib().rootId;
  for (size_t i = 0; i < g_compositionPath.size(); ++i) {
    const Symbol* s = g_lib().find(cur);
    const SymbolChild* c = s ? childById(*s, g_compositionPath[i]) : nullptr;
    if (!c) return i;
    cur = c->symbolId;
  }
  return g_compositionPath.size();
}

const std::string& currentSymbolId() {
  // PURE: walks the valid prefix only, never mutates the path (document.h contract).
  static std::string s_cur;
  s_cur = g_lib().rootId;
  const size_t n = validPathPrefix();
  for (size_t i = 0; i < n; ++i)
    s_cur = childById(*g_lib().find(s_cur), g_compositionPath[i])->symbolId;
  return s_cur;
}

Symbol* currentSymbol() { return g_lib().find(currentSymbolId()); }
const Symbol* currentSymbolConst() { return g_lib().find(currentSymbolId()); }

void validateCompositionPath() {
  const size_t n = validPathPrefix();
  if (n < g_compositionPath.size()) {
    g_compositionPath.resize(n);  // a path child was deleted/retyped: fall back honestly
    g_relayout = true;            // the canvas is suddenly showing a different symbol
  }
}

bool pushComposition(int childId) {
  validateCompositionPath();  // never push onto a dangling tail (refuter N3 B3)
  const Symbol* cur = currentSymbolConst();
  const SymbolChild* c = cur ? childById(*cur, childId) : nullptr;
  const Symbol* target = c ? g_lib().find(c->symbolId) : nullptr;
  if (!target || target->atomic) {
    // TiXL: only items with children open. SAY so — a silent refusal reads as "double-click
    // is broken" (柏為 2026-06-11). UI string stays ASCII (imgui default font has no CJK).
    if (c) g_status = c->symbolId + " is a native operator - no subgraph inside";
    return false;
  }
  // Refuse entering a SELF-NESTED instance (target symbol already on the chain): the
  // resident build skips such children (S14 guard), so inside it the "current terminal"
  // resolves to paths that don't exist -> permanent black (refuter N3 S2). Mirror the guard.
  {
    std::string walk = g_lib().rootId;
    for (size_t i = 0;; ++i) {
      if (walk == target->id) {
        g_status = "recursive composition — not entered";
        return false;
      }
      if (i >= g_compositionPath.size()) break;
      walk = childById(*g_lib().find(walk), g_compositionPath[i])->symbolId;
    }
  }
  g_compositionPath.push_back(childId);
  g_relayout = true;
  g_status = "entered " + (target->name.empty() ? target->id : target->name);
  return true;
}

bool popComposition() {
  validateCompositionPath();
  if (g_compositionPath.empty()) return false;
  g_compositionPath.pop_back();
  g_relayout = true;
  g_status = "up";
  return true;
}

void truncateComposition(size_t depth) {
  validateCompositionPath();
  if (depth >= g_compositionPath.size()) return;
  g_compositionPath.resize(depth);
  g_relayout = true;
  g_status = "up";
}

bool doCombine(const std::vector<int>& childIds, const std::string& name) {
  Symbol* cur = currentSymbol();
  if (!cur) return false;
  sw::CombineResult r = sw::combineChildren(g_lib(), cur->id, childIds, name);
  if (!r.ok) {
    g_status = "combine failed: " + r.error;
    return false;
  }
  // 照 TiXL: not undoable. The spec table refreshes at the next frame boundary
  // (frame_cook, keyed off the revision) — never mid-frame (N2 UAF lesson).
  sw::g_commands.clear();
  bumpLibRevision();
  g_relayout = true;
  g_status = "combined " + std::to_string(childIds.size()) + " into " +
             (name.empty() ? r.newSymbolId : name) + " (not undoable)";
  return true;
}

std::string residentPathPrefix() {
  // Walk the VALID prefix only — main resolves the cook target before this frame's
  // validateCompositionPath, so a dangling tail must not leak into the path string
  // (one black frame + the "navigation never blanks" invariant broken — refuter N3 B3).
  std::string p;
  const size_t n = validPathPrefix();
  for (size_t i = 0; i < n; ++i) p += std::to_string(g_compositionPath[i]) + "/";
  return p;
}

std::string residentPathFor(int childId) {
  return residentPathPrefix() + std::to_string(childId);
}

}  // namespace sw::doc
