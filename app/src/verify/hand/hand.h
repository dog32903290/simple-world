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
//   rclick <x> <y>            right click
//   double <x> <y>            double click
//   drag   <x0> <y0> <x1> <y1>  press at start, interpolate, release at end
//   scroll <x> <y> <dx> <dy>  wheel at position
//   key    <name>             press+release a key (e.g. backspace, delete, z, enter)
//   keychord <mods> <name>    hold mods, press+release key, release mods.
//                             mods joined by '+': cmd/super, ctrl, shift, alt.
//                             e.g. "keychord cmd z" (undo), "keychord cmd+shift z" (redo)
//   text   <utf8...>          type the REST of the line into the focused InputText
//                             via io.AddInputCharactersUTF8 — keeps spaces, accepts
//                             multibyte UTF-8 (CJK: e.g. "text 心跳偵測" for a node
//                             rename). Inject after the field has keyboard focus.
// A click/drag spans multiple frames (ImGui needs down and up on separate
// frames), so commands are expanded into per-frame steps and consumed one per
// frame. After issuing a command, give the app a few frames before reading back.
#pragma once

namespace sw::hand {

// Read+consume the SW_EYE_DIR/hand command file (if any), expand commands into
// per-frame input steps. Cheap; safe to call every frame.
void poll();

// Apply ONE queued input step to ImGui's IO. MUST be called right before
// ImGui::NewFrame() (IO events are consumed by NewFrame). No-op if queue empty.
void applyPendingStep();

// True while queued input steps await frames. The verify keep-alive (main.cpp)
// reads this to pump frames at gesture speed when the display link stalls:
// steps drain one per frame, and ImGui's double-click window (~0.30s, real-time
// DeltaTime via ImGui_ImplOSX_NewFrame) closes between idle keep-alive frames
// (~4fps) — a queued `double` could never register.
bool hasPending();

// Headless self-test (ARCHITECTURE.md rule 5). Drives a minimal ImGui context
// through every hand capability and asserts the effect:
//   move/down/up — click expands to move->press->release; cursor parks, button
//                  toggles, releases.
//   chord        — Cmd+Z lands the modifier on the Z-press frame (Mac Cmd->Ctrl).
//   text         — `text 測試hi` reaches a focused InputText buffer (CJK via UTF-8).
//   select       — a `click` on a real ax::NodeEditor node body selects it
//                  (GetSelectedNodes reports the node) — the gap-2 regression guard.
// injectBug enqueues NOTHING so the hand is shown to do nothing (every leg RED)
// before trusting a PASS.
int runSelfTest(bool injectBug);

}  // namespace sw::hand
