#pragma once
// ui/output_window_resolution — the Output resolution selector (port of TiXL ResolutionHandling),
// split out of output_window.cpp. The preset TABLE is the canonical default set
// (ResolutionHandling.cs:69-78) — exact labels + dims, in order. The combo (in the coordinator)
// drives g_selectedResIndex; applyResolutionSelection pushes the pick to the cook-core override
// seam. Zone: ui (reaches the shell's resolution-override accessors, defined in main.cpp).
namespace sw::ui {

// One row per preset (鐵律 7: data-driven). `useAsAspectRatio` entries resolve through
// computeResolution (window-aspect fit); fixed-pixel entries return their {w,h} verbatim.
struct ResPreset {
  const char* title;
  int w, h;
  bool useAsAspectRatio;
};

// The canonical preset table + its count (defined in output_window_resolution.cpp). The Fill
// sentinel is index 0 (DefaultResolution): selecting it CLEARS the cook override (== window size).
// The LAST row ("Custom", kCustomResIndex) is the editable-W/H sentinel (TiXL "Add" flow analogue).
extern const ResPreset kResPresets[];
extern const int kResPresetCount;
extern const int kCustomResIndex;

// Session-only selection: index into kResPresets. 0 = Fill = DefaultResolution = follow window
// (the cook default). Never serialized — matches the Pin (a view setting, not graph state).
extern int g_selectedResIndex;

// Custom-row session W/H (used only when g_selectedResIndex == kCustomResIndex). Edited by the two
// W/H fields in output_window.cpp; clamped to [kCustomResMin, kCustomResMax] (TiXL Resolution.IsValid).
extern int g_customResW, g_customResH;
constexpr int kCustomResMin = 1, kCustomResMax = 8192;
int clampCustomRes(int v);  // clamp a Custom W/H edit into [kCustomResMin, kCustomResMax]

// Drive the cook-core override to match g_selectedResIndex. Fill (index 0) clears; Custom uses the
// session W/H; any other preset sets the computed size. `winW/winH` = the Fill-baseline window size.
// Called ON CHANGE (and, for Custom, on a W/H field edit).
void applyResolutionSelection(int winW, int winH);

// Draw the Custom-row W/H editors inline after the combo (no-op unless the Custom row is selected).
// Split here (not the coordinator) so the selector's widget code stays with its table + apply (鐵律 4).
void drawCustomResolutionEditor();

// Find a preset's table index by its title (TiXL ResolutionHandling.FindByTitle, used by
// OutputWindow.LoadStateFrom). Returns 0 (Fill) when the title is empty or no longer in the table —
// the safe restore fallback (out-window-persistence). Used by the persistence restore path only.
int resolutionIndexForTitle(const char* title);

// S1-fill window-follow: the coordinator calls this every frame with the viewport content region
// (TiXL re-reads ImGui.GetWindowSize() inside ComputeResolution every frame, ResolutionHandling.cs:120
// + OutputWindow.cs:411-414). Pushes the size to the shell seam; the runtime applies it at cook entry
// and rebuilds the window target ONLY when the size actually changed. Degenerate regions are dropped.
void pushFillWindowSize(float regionW, float regionH);

}  // namespace sw::ui
