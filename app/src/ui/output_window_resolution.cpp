// ui/output_window_resolution — Output resolution selector (port of TiXL ResolutionHandling).
// Extracted verbatim from output_window.cpp; the preset table + the ComputeResolution math +
// the on-change apply that drives the cook-core override seam. No behavior change. Zone: ui.
#include "ui/output_window_resolution.h"

#include <cstring>  // std::strcmp (title lookup for the persistence restore path)

// The shell owns the frame render-size override (cook seeds it into RequestedResolution). A preset
// → set; Fill → clear (back to window size, byte-identical to today). Defined in main.cpp.
namespace sw {
void setOutputResolutionOverride(int w, int h);
void clearOutputResolutionOverride();
}  // namespace sw

namespace sw::ui {

// The preset TABLE is the canonical default set (ResolutionHandling.cs:69-78) — exact labels +
// dims, in order, 隨 TiXL 不自編. `useAsAspectRatio` entries resolve through computeResolution
// (window-aspect fit); fixed-pixel entries return their {w,h} verbatim. The Fill sentinel is
// index 0 (DefaultResolution): selecting it CLEARS the cook override (== window size == today).
// Data-driven (鐵律 7): one row per preset, the combo + apply both iterate the table.
const ResPreset kResPresets[] = {
    {"Fill", 0, 0, true},   {"1:1", 1, 1, true},   {"16:9", 16, 9, true},
    {"4:3", 4, 3, true},    {"480p", 850, 480, false},  {"720p", 1280, 720, false},
    {"1080p", 1920, 1080, false}, {"4k", 1920 * 2, 1080 * 2, false},
    {"8k", 1920 * 4, 1080 * 4, false}, {"4k Portrait", 1080 * 2, 1920 * 2, false},
};
const int kResPresetCount = static_cast<int>(sizeof(kResPresets) / sizeof(kResPresets[0]));

// Session-only selection: index into kResPresets. 0 = Fill = DefaultResolution = follow window
// (the cook default). Never serialized — matches the Pin (a view setting, not graph state) and
// TiXL, which keeps _selectedResolution in the OutputWindow instance, not the .t3.
int g_selectedResIndex = 0;

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

// Drive the cook-core override to match g_selectedResIndex. Fill (index 0) clears; any other
// preset sets the computed size. `winW/winH` = the Fill-baseline window size (the size the cook
// shows when no override is engaged). Called ON CHANGE (not every frame) so there is no cook
// churn — the setters are idempotent, but we still gate on a change to keep the contract crisp.
void applyResolutionSelection(int winW, int winH) {
  const ResPreset& p = kResPresets[g_selectedResIndex];
  if (g_selectedResIndex == 0) {  // Fill / DefaultResolution -> back to window size.
    sw::clearOutputResolutionOverride();
    return;
  }
  const Int2 r = computeResolution(p, winW, winH);
  if (r.w > 0 && r.h > 0) sw::setOutputResolutionOverride(r.w, r.h);
  else sw::clearOutputResolutionOverride();  // degenerate window -> behave as Fill
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
