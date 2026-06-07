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
void beginWidgetFrame();
void recordItem(const char* label);  // grabs ImGui::GetItemRectMin/Max for `label`
// `mtkView` is the metal-cpp MTK::View* (== ObjC MTKView*); used for window/
// screen geometry + backing scale. No-op if there is no pending map request.
void writeWidgetMap(void* mtkView, const char* outName);

// --- headless self-test (RED->GREEN proof the PNG pipeline can see) ----------
// Build a known RGBA buffer, write+reload a PNG, assert the center pixel. With
// injectBug the buffer is written wrong so the eye is shown to FAIL first.
int runSelfTest(bool injectBug);

}  // namespace sw::eye
