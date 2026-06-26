// ui/perf_overlay — FrameTime P99 grader + mini rolling graph overlay.
// Zone: ui. Deps: imgui.h + perf_overlay.h only. Zero app/runtime/platform includes.
//
// TiXL parity: Core/Stats/FrameTimeGrader.cs (122 lines).
// FrameTimeGrader algorithm ported verbatim; see fork notes at bottom for deviations.

#include "ui/perf_overlay.h"

#include <algorithm>  // std::sort
#include <cstddef>    // size_t

#include "imgui.h"

namespace sw::ui {

bool g_showPerfOverlay = false;

// ---------------------------------------------------------------------------
// RollingMetric — 128-entry ring buffer of frame times (ms)
// ---------------------------------------------------------------------------
// Fork (named): TiXL RollingMetric is a histogram with bucket counts for fast
// percentile from cumulative sums. sw uses a flat ring of raw floats — simpler
// to implement correctly with zero external deps, percentile precision is
// sample-exact (not bucket-bound), and 128 samples is cheap to sort per-frame.
// ---------------------------------------------------------------------------
static constexpr int RING_SIZE = 128;

static float  g_ring[RING_SIZE] = {};
static int    g_ringHead = 0;   // next write position (oldest after ring fills)
static int    g_ringCount = 0;  // samples stored (capped at RING_SIZE)

static void ringPush(float ms) {
  g_ring[g_ringHead] = ms;
  g_ringHead = (g_ringHead + 1) % RING_SIZE;
  if (g_ringCount < RING_SIZE) ++g_ringCount;
}

// Returns the p-th percentile (0..1) of the current ring contents, in ms.
// Uses a temporary sorted copy — O(N log N) with N=128, negligible per-frame.
static float ringPercentile(float p) {
  if (g_ringCount == 0) return 0.f;
  float sorted[RING_SIZE];
  for (int i = 0; i < g_ringCount; ++i) sorted[i] = g_ring[i];
  std::sort(sorted, sorted + g_ringCount);
  // TiXL uses ceil(p * Count) rank (1-based), same here.
  int rank = static_cast<int>(p * static_cast<float>(g_ringCount) + 0.9999f);
  if (rank < 1)            rank = 1;
  if (rank > g_ringCount)  rank = g_ringCount;
  return sorted[rank - 1];
}

// Fraction of samples strictly above thresholdMs.
static float ringFractionOver(float thresholdMs) {
  if (g_ringCount == 0) return 0.f;
  int over = 0;
  for (int i = 0; i < g_ringCount; ++i)
    if (g_ring[i] > thresholdMs) ++over;
  return static_cast<float>(over) / static_cast<float>(g_ringCount);
}

// ---------------------------------------------------------------------------
// FrameTimeGrader — port of TiXL FrameTimeGrader.cs
// ---------------------------------------------------------------------------
struct FrameGrade {
  float       p50Ms;
  float       p99Ms;
  float       targetMs;
  float       score;
  const char* letter;
};

// TiXL FrameTimeGrader.cs:_commonTargetsMs
static constexpr float kCommonTargetsMs[] = {
    1000.f / 144.f,
    1000.f / 120.f,
    1000.f /  75.f,
    1000.f /  60.f,
    1000.f /  30.f,
};
static constexpr int kNumTargets = (int)(sizeof(kCommonTargetsMs) / sizeof(kCommonTargetsMs[0]));

// TiXL FrameTimeGrader.cs:SnapToCommonTarget — nearest by absolute distance.
static float snapToCommonTarget(float medianMs) {
  float best     = kCommonTargetsMs[kNumTargets - 1];
  float bestDist = 1e9f;
  for (int i = 0; i < kNumTargets; ++i) {
    float d = medianMs - kCommonTargetsMs[i];
    if (d < 0.f) d = -d;
    if (d < bestDist) { bestDist = d; best = kCommonTargetsMs[i]; }
  }
  return best;
}

// TiXL FrameTimeGrader.cs:ScoreToLetter.
// Fork (named "A+Threshold_0.95_fork"): task spec requires >=0.95 for A+;
// TiXL uses score>=0.97 && targetMs<=1000/60+0.5 && p50<=targetMs+0.5.
// FORK sw_grade_thresholds: TiXL FrameTimeGrader.cs uses C>=0.50/D>=0.30; sw uses 0.55/0.40.
// The tighter thresholds mean sw awards C/D less liberally — a cosmetic display-only divergence.
static const char* scoreToLetter(float score) {
  if (score >= 0.95f) return "A+";
  if (score >= 0.85f) return "A";
  if (score >= 0.70f) return "B";
  if (score >= 0.55f) return "C";  // TiXL: 0.50
  if (score >= 0.40f) return "D";  // TiXL: 0.30
  return "F";
}

// TiXL FrameTimeGrader.cs:Grade
static FrameGrade gradeFrameTimes() {
  if (g_ringCount == 0)
    return {0.f, 0.f, 1000.f / 60.f, 0.f, "-"};

  const float p50      = ringPercentile(0.50f);
  const float p99      = ringPercentile(0.99f);
  const float targetMs = snapToCommonTarget(p50);
  const float fracOver = ringFractionOver(2.f * targetMs);

  // TiXL: tail = 1 - (p99 - target) / (target * 3), clamp 0..1
  float tail = 1.f - (p99 - targetMs) / (targetMs * 3.f);
  if (tail < 0.f) tail = 0.f;
  if (tail > 1.f) tail = 1.f;

  // TiXL: score = tail - fracOver*2, clamp 0..1
  float score = tail - fracOver * 2.f;
  if (score < 0.f) score = 0.f;
  if (score > 1.f) score = 1.f;

  return {p50, p99, targetMs, score, scoreToLetter(score)};
}

// ---------------------------------------------------------------------------
// Per-frame cache — computed once in updatePerfOverlay, read in drawPerfMiniGraph
// ---------------------------------------------------------------------------
static FrameGrade g_grade = {0.f, 0.f, 1000.f / 60.f, 0.f, "-"};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool handlePerfOverlayKey() {
  // Bare F10, no modifiers (sw-specific, no TiXL equivalent; named fork "PerfOverlayF10_fork").
  const ImGuiIO& io = ImGui::GetIO();
  if (io.KeyCtrl || io.KeyAlt || io.KeyShift) return false;
  if (!ImGui::IsKeyPressed(ImGuiKey_F10, false)) return false;
  g_showPerfOverlay = !g_showPerfOverlay;
  return true;
}

void updatePerfOverlay(float dtSeconds) {
  const float ms = dtSeconds * 1000.f;
  ringPush(ms);
  g_grade = gradeFrameTimes();
}

void togglePerfOverlay() {
  g_showPerfOverlay = !g_showPerfOverlay;
}

// TiXL has no direct mini-graph widget; sw uses ImGui::PlotLines.
// Fork (named): "PerfMiniGraphWidget_fork" — custom corner overlay, no TiXL equivalent.
void drawPerfMiniGraph() {
  if (!g_showPerfOverlay) return;

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + vp->WorkSize.x - 220.f, vp->WorkPos.y + 28.f),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(210.f, 110.f), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.75f);

  constexpr ImGuiWindowFlags kPerfWinFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoInputs;

  if (ImGui::Begin("##perf", nullptr, kPerfWinFlags)) {
    // Header line: grade letter, detected fps target, P99 tail latency
    const float fps = (g_grade.targetMs > 0.f) ? (1000.f / g_grade.targetMs) : 0.f;
    ImGui::Text("%s  %.0ffps  P99:%.1fms", g_grade.letter, fps, g_grade.p99Ms);

    // PlotLines expects contiguous floats starting at an offset. g_ringHead is the
    // NEXT write position, so [g_ringHead..g_ringHead+RING_SIZE-1] % RING_SIZE gives
    // samples oldest→newest. We copy to a contiguous scratch to avoid wrap arithmetic.
    // (With RING_SIZE=128 this is 512 bytes on the stack — fine.)
    float scratch[RING_SIZE];
    const int n = g_ringCount;
    if (n > 0) {
      // oldest sample index
      const int oldest = (g_ringCount < RING_SIZE) ? 0 : g_ringHead;
      for (int i = 0; i < n; ++i)
        scratch[i] = g_ring[(oldest + i) % RING_SIZE];
    }

    // Scale: 0 .. target*3 (shows everything up to 3x target before clipping).
    const float scaleMax = (g_grade.targetMs > 0.f) ? g_grade.targetMs * 3.f : 50.f;
    ImGui::PlotLines("##ft",
                     scratch,
                     n > 0 ? n : 1,
                     0,
                     nullptr,
                     0.f,
                     scaleMax,
                     ImVec2(196.f, 60.f));
  }
  ImGui::End();
}

}  // namespace sw::ui
