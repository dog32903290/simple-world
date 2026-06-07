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
//   click  <x> <y>            left click (down then up)
//   rclick <x> <y>            right click
//   double <x> <y>            double click
//   drag   <x0> <y0> <x1> <y1>  press at start, interpolate, release at end
//   scroll <x> <y> <dx> <dy>  wheel at position
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

// Headless self-test (ARCHITECTURE.md rule 5): enqueue a click, pump a minimal
// ImGui context, assert the IO reflects press-then-release. injectBug skips the
// command so the hand is shown to do NOTHING (RED) before trusting a PASS.
int runSelfTest(bool injectBug);

}  // namespace sw::hand
