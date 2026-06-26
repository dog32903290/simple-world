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
extern const ResPreset kResPresets[];
extern const int kResPresetCount;

// Session-only selection: index into kResPresets. 0 = Fill = DefaultResolution = follow window
// (the cook default). Never serialized — matches the Pin (a view setting, not graph state).
extern int g_selectedResIndex;

// Drive the cook-core override to match g_selectedResIndex. Fill (index 0) clears; any other
// preset sets the computed size. `winW/winH` = the Fill-baseline window size. Called ON CHANGE.
void applyResolutionSelection(int winW, int winH);

// Find a preset's table index by its title (TiXL ResolutionHandling.FindByTitle, used by
// OutputWindow.LoadStateFrom). Returns 0 (Fill) when the title is empty or no longer in the table —
// the safe restore fallback (out-window-persistence). Used by the persistence restore path only.
int resolutionIndexForTitle(const char* title);

}  // namespace sw::ui
