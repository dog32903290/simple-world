// eye — the agent's structural eye over the live app (codex-eyes method).
//
// Three capabilities, one sentinel mechanism. An agent has no eyes; the honest
// fix is to make THIS process hand over its own pixels / widget coordinates, so
// provenance is structural — it can never be the wrong window, by construction.
//
//   clean : the pure render layer (g_particles->target()) -> clean.png
//   full  : the whole presented window (UI + render)       -> full.png
//   map   : every clickable widget's SCREEN rect           -> map.json
//
// Trigger is a sentinel file under SW_EYE_DIR: the agent `touch`es req_clean /
// req_full / req_map, the live app consumes it next frame and writes the output
// beside it. No window-finding, no screen scraping.
//
// Implemented in eye.mm (ObjC++) because PNG encode + AppKit window geometry are
// one-liners there and the coordinate transform is done by AppKit itself (the
// part most likely to drift if hand-rolled). MRC, like the imgui backends.
#pragma once

#include <cstdint>

namespace MTL { class Texture; }

namespace sw::eye {

// What the agent asked for this frame (each flag consumed independently).
struct Request {
  bool clean = false;  // dump the pure render layer
  bool full = false;   // dump the whole presented window
  bool map = false;    // dump the widget coordinate table
  bool state = false;  // dump graph state (caller composes json) -> state.json
};

// Check SW_EYE_DIR for req_* sentinels; delete the ones found; report which.
// Cheap (stat of 3 paths) — safe to call every frame.
Request poll();

// --- pixel outputs -----------------------------------------------------------
// RGBA8Unorm linear texture (e.g. the particle target) -> PNG, colors verbatim.
void dumpTextureRGBA(MTL::Texture* tex, const char* outName);
// BGRA8Unorm_sRGB drawable texture -> PNG (swizzles BGRA->RGBA; sRGB bytes are
// already gamma-encoded, written as-is so the PNG looks like the screen).
void dumpDrawableBGRA(MTL::Texture* tex, const char* outName);

// --- widget map (capability 3) ----------------------------------------------
// Call once at the top of the imgui frame, then recordItem() after each widget
// whose screen position the hand may need. writeWidgetMap() turns the collected
// ImGui rects into top-left global screen points using the live window geometry.
//
// Label naming convention (one prefix per popup/widget scope, so the driver can
// query map.json instead of screenshot-hunting coordinates):
//   "New", "Add Node", ...   toolbar / dialog widgets (plain label)
//   menu:<id>                Add-Node popup rows (toolbar.cpp; id = ASCII symbol id)
//   ctx:<action>             canvas right-click menus (combine_dialog.cpp node_ctx/bg_ctx)
//   tlctx:<label>            timeline key right-click interpolation menu (timeline_window.cpp)
//   insp:<action>            Inspector parameter right-click (inspector.cpp Animate/Remove)
//   param:<id> / node:<id> / pin:<id> / tl_* / tlc_*   rows recorded via recordRect
//   nsmenu:<menu>:<title>    native NSMenu rows (menu.cpp) — metadata only (shortcut,
//                            no rect): NSMenu items have no imgui rect and the in-process
//                            hand cannot reach them; drive them via their key equivalent
//                            at the OS level. Emitted as "native_menu_items" in map.json.
// Popup rows exist in the map ONLY on frames the popup is open (clear+refill per frame),
// so the driver flow is: rclick/click to open -> req_map -> read rects -> click the row.
void beginWidgetFrame();
void recordItem(const char* label);  // grabs ImGui::GetItemRectMin/Max for `label`
// Like recordItem but with explicit ImGui-screen coords — for node-editor canvas
// items whose GetItemRect is canvas-local (caller applies ed::CanvasToScreen).
void recordRect(const char* label, float x0, float y0, float x1, float y1);
// Register one NATIVE menu-bar item (NSMenu — outside imgui). Persistent rows
// (registered once at startup by the menu builder, NOT cleared per frame); they
// surface in map.json as "native_menu_items" with the shortcut, no rect. `shift`
// = the key equivalent carries Shift on top of Cmd (all rows are Cmd-based).
void recordNativeMenuItem(const char* menu, const char* title, const char* key, bool shift);
// `mtkView` is the metal-cpp MTK::View* (== ObjC MTKView*); used for window/
// screen geometry + backing scale. No-op if there is no pending map request.
// The json also carries "hand_pending" (queued hand input steps not yet applied)
// so a driver can wait for a multi-frame gesture to finish via map round-trips.
void writeWidgetMap(void* mtkView, const char* outName);

// --- generic state dump (capability 4) --------------------------------------
// Write `content` verbatim to SW_EYE_DIR/outName. eye stays a leaf I/O sink; the
// caller (which depends on runtime/ui) composes the json (e.g. graph + selection)
// so the agent can machine-check mutations without OCR'ing a screenshot.
void writeText(const char* outName, const char* content);

// --- headless self-test (RED->GREEN proof the PNG pipeline can see) ----------
// Build a known RGBA buffer, write+reload a PNG, assert the center pixel. With
// injectBug the buffer is written wrong so the eye is shown to FAIL first.
int runSelfTest(bool injectBug);

// --- headless self-test (RED->GREEN proof the widget MAP repopulates) --------
// Drives the real beginWidgetFrame()/recordItem() path through a headless ImGui
// frame sequence that mirrors the reported failure: toolbar drawn, Add-Node popup
// opened, then dismissed. Asserts the map is non-empty on EVERY frame the editor
// is drawn — including the frame after the popup closed — and that the per-frame
// buffer carries no stale rows from the popup-open frame. injectBug models the
// regression (post-popup record pass suppressed) so the map empties and we see RED.
int runMapSelfTest(bool injectBug);

}  // namespace sw::eye
