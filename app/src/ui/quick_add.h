// ui/quick_add — Cmd+F quick-add node palette (SearchGraph, TiXL FactoryKeyMap.cs:56).
//
// Zone: ui. Depends on app(document/graph_commands) + runtime + verify(thin hook).
// Never depends upward (runtime/platform/verify cannot include this).
//
// Behaviour contract (mirrors TiXL SymbolBrowser behaviour skeleton):
//   open  : openQuickAdd(canvasX, canvasY) — anchors spawn point on canvas coords
//   filter: real-time substring match (fork "QuickAddFilter_Substring": TiXL uses
//           regex scatter-match; we use case-insensitive strstr — adequate for the
//           current library size and avoids std::regex link weight in the UI hot path)
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

// Self-test: 0=PASS, nonzero=FAIL; injectBug=true forces a red-path.
// Tests the filter logic (substring match), list building, and eye-hook naming.
int runQuickAddSelfTest(bool injectBug);

}  // namespace sw::ui
