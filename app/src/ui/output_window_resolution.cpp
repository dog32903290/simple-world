// ui/output_window_resolution — Output resolution selector (port of TiXL ResolutionHandling).
// Extracted verbatim from output_window.cpp; the preset table + the ComputeResolution math +
// the on-change apply that drives the cook-core override seam. No behavior change. Zone: ui.
#include "ui/output_window_resolution.h"

#include <cstring>  // std::strcmp (title lookup for the persistence restore path)

#include "imgui.h"  // Custom-row W/H editors (drawCustomResolutionEditor)

// The shell owns the frame render-size override (cook seeds it into RequestedResolution). A preset
// → set; Fill → clear (back to the live window size). setOutputWindowContentSize is the S1-fill
// window-follow push (content region → runtime, applied at cook entry). All defined in main.cpp.
namespace sw {
void setOutputResolutionOverride(int w, int h);
void clearOutputResolutionOverride();
void setOutputWindowContentSize(int w, int h);
bool outputWindowResolution(int& w, int& h);  // Fill baseline (window size) — for the Custom re-apply
}  // namespace sw

namespace sw::ui {

// The preset TABLE is the canonical default set (ResolutionHandling.cs:69-78) — exact labels +
// dims, in order, 隨 TiXL 不自編. `useAsAspectRatio` entries resolve through computeResolution
// (window-aspect fit); fixed-pixel entries return their {w,h} verbatim. The Fill sentinel is
// index 0 (DefaultResolution): selecting it CLEARS the cook override (== window size == today).
// Data-driven (鐵律 7): one row per preset, the combo + apply both iterate the table.
//
// "Custom" (LAST row) = the sw analogue of TiXL's "Add" flow (ResolutionHandling.cs:45-50: index 666
// creates a user Resolution("untitled", 256, 256) with an EDITABLE Size, clamped by Resolution.IsValid
// to >0 && <16384, cs:144-145). Rather than a full add-named-resolution dialog we surface ONE Custom
// row whose W/H are edited inline (output_window.cpp W/H fields → g_customResW/H). Its table w/h below
// is a placeholder only: applyResolutionSelection reads the session W/H for this row, not these.
const ResPreset kResPresets[] = {
    {"Fill", 0, 0, true},   {"1:1", 1, 1, true},   {"16:9", 16, 9, true},
    {"4:3", 4, 3, true},    {"480p", 850, 480, false},  {"720p", 1280, 720, false},
    {"1080p", 1920, 1080, false}, {"4k", 1920 * 2, 1080 * 2, false},
    {"8k", 1920 * 4, 1080 * 4, false}, {"4k Portrait", 1080 * 2, 1920 * 2, false},
    {"Custom", 1280, 720, false},  // sentinel: W/H come from g_customResW/H (see kCustomResIndex)
};
const int kResPresetCount = static_cast<int>(sizeof(kResPresets) / sizeof(kResPresets[0]));
const int kCustomResIndex = kResPresetCount - 1;  // the "Custom" row (last): W/H from the session globals

// Session-only selection: index into kResPresets. 0 = Fill = DefaultResolution = follow window
// (the cook default). Never serialized — matches the Pin (a view setting, not graph state) and
// TiXL, which keeps _selectedResolution in the OutputWindow instance, not the .t3.
int g_selectedResIndex = 0;

// Custom-row session W/H (only meaningful when g_selectedResIndex == kCustomResIndex). Clamped to
// [kCustomResMin, kCustomResMax] on edit (TiXL Resolution.IsValid, ResolutionHandling.cs:144-145:
// Size.Width/Height > 0 && < 16384; we cap at 8192 per the工單 clamp range). Session-only like the
// selection index; persistence is a dossier follow-up (out-window-persist stores the title, not W/H).
int g_customResW = 1280, g_customResH = 720;

namespace {
// TiXL Resolution.ComputeResolution (ResolutionHandling.cs:115-135). Fixed-pixel preset → its
// own size. UseAsAspectRatio with 0/0 (Fill) → the window size verbatim. Otherwise fit the
// requested aspect into the window: requested wider than window → fit width (letterbox), else fit
// height (pillarbox). `winW/winH` is the Fill baseline = the cook's window resolution. Returns
// {0,0} only if the window is degenerate (caller treats that as "leave override unset" = Fill).
struct Int2 { int w, h; };
Int2 computeResolution(const ResPreset& p, int winW, int winH) {
  if (!p.useAsAspectRatio) return {p.w, p.h};
  if (winW <= 0 || winH <= 0) return {0, 0};
  if (p.w <= 0 || p.h <= 0) return {winW, winH};  // Fill: window size verbatim
  const float windowAspect = static_cast<float>(winW) / static_cast<float>(winH);
  const float requestedAspect = static_cast<float>(p.w) / static_cast<float>(p.h);
  return (requestedAspect > windowAspect)
             ? Int2{winW, static_cast<int>(winW / requestedAspect)}
             : Int2{static_cast<int>(winH * requestedAspect), winH};
}
}  // namespace

// Clamp a Custom W/H edit to the valid pixel range (TiXL Resolution.IsValid, ResolutionHandling.cs:
// 144-145: >0 && <16384; capped at 8192 per the工單 range). Applied at the edit site AND here so a
// stale session value can never drive a degenerate override.
int clampCustomRes(int v) { return v < kCustomResMin ? kCustomResMin : (v > kCustomResMax ? kCustomResMax : v); }

// Drive the cook-core override to match g_selectedResIndex. Fill (index 0) clears; the Custom row uses
// the session W/H (clamped); any other preset sets the computed size. `winW/winH` = the Fill-baseline
// window size (the size the cook shows when no override is engaged). Called ON CHANGE (and, for Custom,
// on a W/H field edit) so there is no cook churn — the setters are idempotent, gate kept for a crisp contract.
void applyResolutionSelection(int winW, int winH) {
  if (g_selectedResIndex == 0) {  // Fill / DefaultResolution -> back to window size.
    sw::clearOutputResolutionOverride();
    return;
  }
  if (g_selectedResIndex == kCustomResIndex) {  // Custom: session W/H (a fixed pixel size, like TiXL's Size).
    sw::setOutputResolutionOverride(clampCustomRes(g_customResW), clampCustomRes(g_customResH));
    return;
  }
  const ResPreset& p = kResPresets[g_selectedResIndex];
  const Int2 r = computeResolution(p, winW, winH);
  if (r.w > 0 && r.h > 0) sw::setOutputResolutionOverride(r.w, r.h);
  else sw::clearOutputResolutionOverride();  // degenerate window -> behave as Fill
}

// The Custom-row W/H editors (TiXL "Add" flow, ResolutionHandling.cs:45-50 → an editable Resolution
// Size, clamped by Resolution.IsValid cs:144-145). Drawn INLINE after the combo, but ONLY when the
// Custom row is selected: two InputInt fields drive the session W/H (g_customResW/H), re-applied on
// edit so the preview retargets live. Clamped [kCustomResMin, kCustomResMax]. Split into this module
// (not the coordinator) so the resolution selector's widget code stays with its table + apply (鐵律 4).
void drawCustomResolutionEditor() {
  if (g_selectedResIndex != kCustomResIndex) return;
  int w = g_customResW, h = g_customResH;
  bool edited = false;
  ImGui::SetNextItemWidth(64.0f);
  if (ImGui::InputInt("##CustomResW", &w, 0, 0)) { g_customResW = clampCustomRes(w); edited = true; }
  ImGui::SameLine();
  ImGui::TextDisabled("x");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(64.0f);
  if (ImGui::InputInt("##CustomResH", &h, 0, 0)) { g_customResH = clampCustomRes(h); edited = true; }
  if (edited) {
    int winW = 0, winH = 0;
    sw::outputWindowResolution(winW, winH);
    applyResolutionSelection(winW, winH);  // re-push the clamped session W/H → cook override
  }
  ImGui::SameLine();
}

// S1-fill window-follow (TiXL ResolutionHandling.cs:120: ComputeResolution reads the LIVE
// `ImGui.GetWindowSize()` every frame; OutputWindow.cs:411-414 seeds RequestedResolution from it
// every frame). The coordinator forwards the viewport content region here each frame; the shell
// hands it to the runtime, which applies at cook entry and rebuilds ONLY on an actual size change
// — so this per-frame push carries zero realloc churn. Degenerate regions (collapsed window,
// first frame) are dropped: the cook keeps the last known size, like TiXL's un-drawn window.
void pushFillWindowSize(float regionW, float regionH) {
  if (regionW > 1.0f && regionH > 1.0f)
    sw::setOutputWindowContentSize(static_cast<int>(regionW), static_cast<int>(regionH));
}

// TiXL ResolutionHandling.FindByTitle (OutputWindow.LoadStateFrom: match the saved title against the
// preset table first). Returns 0 (Fill) for empty/unknown titles — the safe restore default.
int resolutionIndexForTitle(const char* title) {
  if (!title || !*title) return 0;
  for (int i = 0; i < kResPresetCount; ++i)
    if (std::strcmp(kResPresets[i].title, title) == 0) return i;
  return 0;  // title no longer in the table -> Fill (TiXL falls back to a Custom resolution; we Fill)
}

}  // namespace sw::ui
