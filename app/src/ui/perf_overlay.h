// ui/perf_overlay — FrameTime P99 grader + mini rolling graph overlay.
// Zone: ui. Pure imgui, zero Metal/cook/runtime/platform deps.
//
// TiXL parity: FrameTimeGrader.cs (Core/Stats/FrameTimeGrader.cs, 122 lines).
// Fork (named): TiXL RollingMetric uses a histogram with bucket counts; sw uses a
//   flat ring buffer of 128 raw floats. Percentile precision is sample-exact rather
//   than bucket-bound. Grading maths are identical (tail/drop formula, score→letter).
// Fork (named): TiXL has no mini-graph widget; sw uses ImGui::PlotLines overlay.
// Fork (named): A+ threshold is 0.95 (task spec), TiXL is 0.97 (named "A+Threshold_0.95_fork").
#pragma once

namespace sw::ui {

  // Call each frame (before draw) with the raw delta-time in seconds.
  void updatePerfOverlay(float dtSeconds);

  // Draw the floating corner mini-graph window. No-op when g_showPerfOverlay is false.
  void drawPerfMiniGraph();

  // Toggle g_showPerfOverlay (bound to F10 via keymap).
  void togglePerfOverlay();

  // Keymap table handler — bare F10 → togglePerfOverlay(). Returns true if the key fired.
  // Called by keymap::kKeyTable, body in perf_overlay.cpp (fork "PerfOverlayF10_fork").
  bool handlePerfOverlayKey();

  // Public flag — readable by menu / other ui code.
  extern bool g_showPerfOverlay;

}  // namespace sw::ui
