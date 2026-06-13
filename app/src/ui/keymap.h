// ui/keymap — data-driven keyboard shortcut table + per-frame dispatcher.
// Zone: ui. Depends on app(frame_cook/graph_commands/copy_paste) + runtime. Never the reverse.
//
// = TiXL Editor/Gui/Interaction/Keyboard/FactoryKeyMap.cs + KeyActionHandling.cs (locked SHA).
// TiXL uses a UserActions enum + KeyMap list + context flags; we mirror that architecture with a
// small C table (鐵律7: new shortcuts = one row in the table, NOT scattered io.KeyCtrl checks).
//
// Scope / fork decisions:
//   • Existing Cmd+Z/C/V/Delete remain scattered in editor_ui.cpp (risk asymmetry: moving them
//     would touch a long stable code path; named fork "散打保留" until the table covers > ~10 keys).
//   • Playback keys (Space/L/J/K/Shift+←→): global-context, guard = !io.WantTextInput.
//   • Cmd+D (duplicate): canvas-focus context.
//   • F (focus selection): canvas-hover context.
//   • P (pin selected output): canvas-focus context. Sets g_pinnedNode = g_selectedNode
//     (the Output window's display source + the shell's cook target). Named forks vs TiXL
//     in keymap.cpp handlePinToOutput (no-FocusMode / no-Cmd-P-background / single-select).
//   • Mac Cmd↔Ctrl: ConfigMacOSXBehaviors swaps them; io.KeyCtrl detects physical Cmd (see memory).
//     The playback keys here are all bare unmodified keys — WantTextInput is the only guard needed.
//
// Frame step (Shift+←/→ = TiXL PlaybackPreviousFrame/PlaybackNextFrame, FactoryKeyMap.cs:29-30):
// TiXL's step quantum is configurable (FrameStepAmount, TimeControls.cs:51-60); we hardwire 1/30s
// (FrameAt30Fps) as a named fork — no UserSettings system yet. Step = scrub(pos ± 1/30s-in-bars).
//
// PlaybackBackwards (J, FactoryKeyMap.cs:26): TiXL doubles the negative speed on repeated presses
// (TimeControls.cs:85-96, up to -16). We call transport.playBackwards() — one-press toggle
// (stopped/forward → rate=-1,playing; playing-backwards → stop). The ×2 speed ladder belongs to
// the Speed knob (toolbar.cpp named fork C3). Named fork "J=toggle not ladder".
#pragma once

namespace sw::ui::km {

// Called once per frame from the canvas host window (editor_ui / drawNodeCanvas) with the
// node-editor CURRENT. Processes the global keymap table and dispatches actions. All playback
// actions use the !io.WantTextInput guard (= TiXL KeyPressOnly + text-input inhibit, KeyActionHandling.cs).
// Canvas-focus actions (Cmd+D, F) also require IsWindowFocused / IsWindowHovered as appropriate.
void processFrame();

// Selftest: table completeness (every row has a non-null handler, no duplicate bare key binding)
// + injectBug red-proof. --selftest-keymap / --selftest-keymap-bug.
int runKeymapSelfTest(bool injectBug);

}  // namespace sw::ui::km
