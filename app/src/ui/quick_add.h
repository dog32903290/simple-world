// ui/quick_add — Cmd+F quick-add node palette (SearchGraph, TiXL FactoryKeyMap.cs:56).
//
// Zone: ui. Depends on app(document/graph_commands) + runtime + verify(thin hook).
// Never depends upward (runtime/platform/verify cannot include this).
//
// Behaviour contract (mirrors TiXL SymbolBrowser behaviour skeleton):
//   open  : openQuickAdd(canvasX, canvasY) — anchors spawn point on canvas coords
//   filter: real-time scatter / subsequence match (= TiXL SymbolBrowser regex `c.*c.*c`,
//           SymbolFilter.cs:90) over the row's display name OR its category, then ranked
//           by relevancy (exact > prefix > contains > scatter; PascalCase-initials bump;
//           _/OBSOLETE demotion — the portable subset of SymbolFilter.ComputeRelevancy).
//           DEFERRED: namespace TREE grouping + usage/package boosts (categories not yet
//           populated repo-wide; no usage-analysis model). Fork "QuickAddRank_StableTies":
//           equal-score rows hold registry order (stable sort) vs TiXL's Reverse().
//   nav   : CursorDown/Up advance selection
//   commit: Enter or mouse-click spawns the selected type at the anchor
//   cancel: Esc or click-outside closes without action
//
// eye hook: each rendered row emits qa:<type> via eye::recordItem (one line per row;
//           implementation is the recordItem call itself — no logic in verify/).
#pragma once
#include <string>

namespace sw::ui {

// Open the palette anchored at canvas coordinates (cx, cy).
// No-op if already open (idempotent — Cmd+F pressed while open does NOT close it;
// that matches TiXL which just re-focuses the input box).
void openQuickAdd(float cx, float cy);

// Draw the palette this frame. Call once per frame from drawNodeCanvas() while the
// node-editor is current (needed for ed::ScreenToCanvas in the spawn path).
void drawQuickAdd();

// Pure search primitives (exposed for the isolated self-test; production uses them internally).
// scatterMatch: query chars appear in order in `hay` (gaps allowed; empty query = match all).
// computeRelevancy: higher = more relevant (exact > prefix > contains > scatter; see .cpp).
bool   scatterMatch(const std::string& hay, const std::string& q);
double computeRelevancy(const std::string& name, const std::string& query);

// Self-test: 0=PASS, nonzero=FAIL; injectBug=true forces a red-path.
// Tests scatter-match, relevancy ranking, list building, and eye-hook naming.
int runQuickAddSelfTest(bool injectBug);

}  // namespace sw::ui
