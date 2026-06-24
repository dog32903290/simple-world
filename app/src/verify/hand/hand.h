// hand — the agent's second hand: inject mouse input straight into ImGui.
//
// Sibling of `eye` (verify/eye): eye lets the app HAND its pixels/coords to the
// agent; hand lets the agent DRIVE the app back. Both are structural — they go
// through the app itself, never through the OS cursor. That's why hand needs no
// foreground focus, doesn't touch the user's physical cursor, and survives the
// user doing other things on another app.
//
// Why not a CGEventPostToPid wheel (Hammerspoon etc.): ImGui reads input via an
// addLocalMonitorForEvents monitor, and AppKit does NOT dispatch mouse events to
// that monitor unless the app is the active/key app — and this bare binary can't
// reliably become active (window server sees 0 windows). So OS-level injection
// can't reach ImGui here. Feeding io.AddMouse*Event directly bypasses all of it.
//
// Coordinates are IMGUI coordinates (inside DisplaySize, top-left, points) — the
// `imgui_rect` field in eye's map.json, NOT the screen_topleft_pt one.
//
// Trigger: agent writes a command file at SW_EYE_DIR/hand, one command per line:
//   move   <x> <y>
//   click  <x> <y>            left click (move->down->up; the move frame settles
//                             hover at the target so node-editor selection registers)
//   rclick <x> <y>            right click (same 3-frame settle pattern as click —
//                             node-editor's context menu latches the hot object at
//                             PRESS, so the press must land on a settled hover)
//   double <x> <y>            double click (two settled clicks; both reuse the
//                             click expansion, so each press hovers first)
//   drag   <x0> <y0> <x1> <y1>  press at start, interpolate, release at end
//   scroll <x> <y> <dx> <dy>  wheel at position
//   key    <name>             press+release a key (e.g. backspace, delete, z, enter)
//   keychord <mods> <name>    hold mods, press+release key, release mods.
//                             mods joined by '+': cmd/super, ctrl, shift, alt.
//                             e.g. "keychord cmd z" (undo), "keychord cmd+shift z" (redo)
//   selectnode <childId>      select the graph node whose SymbolChild id == childId,
//                             DIRECTLY via the node-editor selection API (ed::SelectNode),
//                             bypassing coordinate hit-tests. The operator node's ed node id
//                             IS the childId (ui/node_draw.cpp: ed::BeginNode(child.id)), so
//                             the map is identity. Fixes the flaky "injected click on a
//                             non-terminal node doesn't select" gap. Applied by the canvas via
//                             applyPendingSelectNodes() while the editor context is current.
//   text   <utf8...>          type the REST of the line into the focused InputText
//                             via io.AddInputCharactersUTF8 — keeps spaces, accepts
//                             multibyte UTF-8 (CJK: e.g. "text 心跳偵測" for a node
//                             rename). Inject after the field has keyboard focus.
//   learn  <child> <slot>     arm P3 MIDI-learn for graph param (child, slot) via the app hook
//                             (= clicking the inspector's MIDI button, but selection-independent so
//                             the scenario doesn't fight the node-select harness gap). The NEXT
//                             `midi` line binds that CC to the param.
//   midi   <ch> <ctrl> <val>  inject a decoded MIDI ControllerChange into the app's live
//                             binding table (P3 learn / cook-side wire), via the app-owned
//                             hook below. A no-op if no hook is set (verify stays a leaf).
// A click/drag spans multiple frames (ImGui needs down and up on separate
// frames), so commands are expanded into per-frame steps and consumed one per
// frame. After issuing a command, give the app a few frames before reading back.
#pragma once

namespace sw::hand {

// App-owned MIDI-inject hook (leaf inversion, like the runtime asset-decoder fn-ptr): the `midi`
// directive forwards (channel, controllerId, controllerValue) here. The app sets this to
// midibind::injectMidiForTest so the scenario can drive a CC into the live binding table without the
// verify leaf depending on app. Unset (null) = the `midi` directive is a no-op.
void setMidiInjectHook(void (*hook)(int channel, int controllerId, int controllerValue));

// App-owned MIDI-learn-arm hook: the `learn` directive forwards (childId, slotId) here. The app sets
// it to a wrapper around midibind::beginLearn (it knows the current composition id). Unset = no-op.
void setLearnArmHook(void (*hook)(int childId, const char* slotId));

// Read+consume the SW_EYE_DIR/hand command file (if any), expand commands into
// per-frame input steps. Cheap; safe to call every frame.
void poll();

// Apply ONE queued input step to ImGui's IO. MUST be called right before
// ImGui::NewFrame() (IO events are consumed by NewFrame). No-op if queue empty.
void applyPendingStep();

// Gap 2: drain the queue of `selectnode <childId>` requests into the node editor's
// selection. MUST be called with the editor context CURRENT (inside ed::Begin/ed::End,
// like ui/node_menu_actions selectConnected) — that's the only scope where ed::SelectNode
// has an editor to act on. One-line hook from the canvas draw; the select logic stays here.
// No-op when nothing is queued. Returns how many ids it selected this call (0 = none).
int applyPendingSelectNodes();

// True while queued input steps await frames. The verify keep-alive (main.cpp)
// reads this to pump frames at gesture speed when the display link stalls:
// steps drain one per frame, and ImGui's double-click window (~0.30s, real-time
// DeltaTime via ImGui_ImplOSX_NewFrame) closes between idle keep-alive frames
// (~4fps) — a queued `double` could never register. Also surfaced in map.json as
// "hand_pending" so a driver can wait for a gesture to finish (sw_drive.sh do).
bool hasPending();

// Parse ONE command line (same grammar as the SW_EYE_DIR/hand file) straight into
// the step queue — the in-process seam the self-test drives; the file path is a
// thin wrapper over this. clearPending() drops whatever steps are still queued.
void feedLine(const char* line);
void clearPending();

// Headless self-test (ARCHITECTURE.md rule 5; lives in hand_selftest.cpp).
// Drives a minimal ImGui context through every hand capability and asserts the effect:
//   move/down/up — click expands to move->press->release; cursor parks, button
//                  toggles, releases.
//   chord        — Cmd+Z lands the modifier on the Z-press frame (Mac Cmd->Ctrl).
//   double       — `double` registers an ImGui double-click (two 3-frame clicks
//                  land inside the 0.30s window at one spot).
//   text         — `text 測試hi` reaches a focused InputText buffer (CJK via UTF-8).
//   select       — a `click` on a real ax::NodeEditor node body selects it
//                  (GetSelectedNodes reports the node) — the gap-2 regression guard.
//   rclick ctx   — a cold `rclick` on a node body fires ed::ShowNodeContextMenu
//                  with that node (批次9: the D3 "rclick 注入不穩" probe — proves
//                  the 3-frame settle suffices for the context-menu latch).
// injectBug enqueues NOTHING so the hand is shown to do nothing (every leg RED)
// before trusting a PASS.
int runSelfTest(bool injectBug);

}  // namespace sw::hand
