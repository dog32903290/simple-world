// ui/slider_ladder — see slider_ladder.h. 1:1 port of TiXL SliderLadder.Draw().
#include "ui/slider_ladder.h"

#include <cmath>

#include "imgui.h"

#include "verify/eye/eye.h"  // one-line hook: overlay active-row rect for the hand/eye

namespace sw::ui {
namespace {

// TiXL Ranges[] (SliderLadder.cs:27-36): scale factor + display label, coarsest first.
struct RangeDef {
  double scaleFactor;
  const char* label;
};
const RangeDef kRanges[] = {
    {1000.0, "1000"}, {100.0, "100"}, {10.0, "10"}, {1.0, "1"},
    {0.1, "0.1"},     {0.01, "0.01"}, {0.001, "0.001"},
};
constexpr int kNumRanges = (int)(sizeof(kRanges) / sizeof(kRanges[0]));

// TiXL constants (SliderLadder.cs:40,163-168).
constexpr float kPixelsPerStep = 10.0f;
constexpr float kRangeWidth = 40.0f;
constexpr float kOuterRangeHeight = 50.0f;
constexpr float kLockDistance = 100.0f;

// Colors. FORK: TiXL uses a `Color` struct (UiColors.cs); here uint32_t via IM_COL32.
// rangeFill (0.3,0.3,0.3), activeFill (0.8,0.8,0.8), lockedFill (1.0,0.6,0.6),
// BackgroundFull = black, ForegroundFull = white, Gray = (0.6,0.6,0.6) (UiColors.cs:15,17,51).
const ImU32 kRangeFillColor = IM_COL32(77, 77, 77, 255);     // 0.3*255 ≈ 77
const ImU32 kActiveRangeFillColor = IM_COL32(204, 204, 204, 255);  // 0.8*255 ≈ 204
const ImU32 kLockedRangeFillColor = IM_COL32(255, 153, 153, 255);  // 1.0/0.6/0.6
const ImU32 kBackgroundFull = IM_COL32(0, 0, 0, 255);
const ImU32 kForegroundFull = IM_COL32(255, 255, 255, 255);
const ImU32 kGray = IM_COL32(153, 153, 153, 255);  // 0.6*255 ≈ 153

// Persistent latch state (TiXL static fields: _lockedRange / _hasExceededDragThreshold / _lastX).
// Single overlay at a time (one slider drags at a time), so module-level mirrors the C# statics.
// FORK: C# nullable RangeDef reference -> here an index (-1 == null / no lock).
int g_lockedRangeIndex = -1;
bool g_hasExceededDragThreshold = false;
float g_lastX = 0.0f;

double optionalClamp(double v, double min, bool clampMin, double max, bool clampMax) {
  // TiXL MathUtils.OptionalClamp.
  if (clampMin && v < min) v = min;
  if (clampMax && v > max) v = max;
  return v;
}

}  // namespace

void drawLadderIfActive(double minV, double maxV, double timeStart) {
  if (!ImGui::IsItemActive()) return;
  const ImVec2 center = ImGui::GetIO().MouseClickedPos[0];
  double ignored = 0.0;  // applyDelta=false → editValue untouched; host drag owns the value
  drawSliderLadder(ignored, minV, maxV, /*scale=*/0.01f,
                   (float)(ImGui::GetTime() - timeStart),
                   /*clampMin=*/true, /*clampMax=*/true, center, /*applyDelta=*/false);
}

void resetSliderLadder() {
  g_lockedRangeIndex = -1;
  g_hasExceededDragThreshold = false;
  g_lastX = 0.0f;
}

void drawSliderLadder(double& editValue, double min, double max,
                      float scale, float timeSinceVisible,
                      bool clampMin, bool clampMax,
                      ImVec2 center, bool applyDelta) {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* foreground = ImGui::GetForegroundDrawList();
  const double initialDelay = 0.2;

  // pNow = mouse position relative to the overlay origin (TiXL: io.MousePos - center).
  const ImVec2 pNow(io.MousePos.x - center.x, io.MousePos.y - center.y);

  if (timeSinceVisible < initialDelay) {
    g_lockedRangeIndex = -1;
    g_hasExceededDragThreshold = false;
    g_lastX = 0.0f;
  }

  const float x = std::floor(pNow.x / kPixelsPerStep);

  if (std::fabs(pNow.x) > kPixelsPerStep * 4.0f) g_hasExceededDragThreshold = true;

  const float dx = x - g_lastX;
  double activeScaleFactor = 0.0;
  bool usingCenterRange = false;

  // Find center range index (SliderLadder.cs:65-71): first range whose factor < scale*1.5.
  int centerRangeIndex;
  for (centerRangeIndex = 0; centerRangeIndex < kNumRanges; centerRangeIndex++) {
    if (kRanges[centerRangeIndex].scaleFactor < scale * 1.5f) break;
  }

  for (int rangeIndex = 0; rangeIndex < kNumRanges; rangeIndex++) {
    const float yMin = kOuterRangeHeight * (rangeIndex - centerRangeIndex - 1 + 0.5f);
    const float yMax = kOuterRangeHeight * (rangeIndex - centerRangeIndex + 0.5f);

    const RangeDef& range = kRanges[rangeIndex];
    const bool isVerticalMatch = pNow.y > yMin && pNow.y < yMax;
    const bool isWithinLockRange = std::fabs(pNow.x) < kLockDistance;

    const bool isActiveRange = (isVerticalMatch && isWithinLockRange) ||
                               (g_lockedRangeIndex == -1 && isVerticalMatch) ||
                               (g_lockedRangeIndex == rangeIndex);

    if (isActiveRange) {
      activeScaleFactor = range.scaleFactor;
      g_lockedRangeIndex = isWithinLockRange ? -1 : rangeIndex;
    }

    const bool isCenterRange = rangeIndex == centerRangeIndex;
    if (isCenterRange) {
      usingCenterRange = isActiveRange;
      continue;  // center range row is not drawn (TiXL: continue)
    }

    // Draw the row rect (TiXL:101-119).
    const bool isLockedRange = g_lockedRangeIndex == rangeIndex;
    const ImU32 centerColor = !isActiveRange      ? kRangeFillColor
                              : isLockedRange      ? kLockedRangeFillColor
                                                   : kActiveRangeFillColor;

    const ImVec2 bMin(-kRangeWidth + center.x, yMin + center.y);
    const ImVec2 bMax(kRangeWidth + center.x, yMax + center.y);
    foreground->AddRectFilled(bMin, bMax, centerColor);
    foreground->AddRect(bMin, bMax, kBackgroundFull);

    const ImVec2 labelSize = ImGui::CalcTextSize(range.label);
    const ImVec2 pText((bMin.x + bMax.x) * 0.5f - labelSize.x * 0.5f,
                       (bMin.y + bMax.y) * 0.5f - labelSize.y * 0.5f);
    // active row: label in background (black) over the bright fill; inactive: white over dark.
    foreground->AddText(pText, isActiveRange ? kBackgroundFull : kForegroundFull, range.label);

    if (isActiveRange) {
      // one-line eye hook: surface the active overlay row so the hand/eye can see the ladder.
      sw::eye::recordRect("slider_ladder:active", bMin.x, bMin.y, bMax.x, bMax.y);
    }
  }

  if (std::fabs(dx) < 0.0001f) return;

  if (g_hasExceededDragThreshold) {
    const double delta = dx;

    // FORK: TiXL pushes Fonts.FontSmall for the modifier hint; simple_world has no FontSmall
    // registry here, so the ×0.01 / ×10 hint draws in the default font (text + behavior parity).
    if (io.KeyAlt) {
      foreground->AddText(ImVec2(io.MousePos.x + 10.0f, io.MousePos.y + 10.0f), kGray, "x0.01");
      scale *= 0.01f;
    } else if (io.KeyShift) {
      foreground->AddText(ImVec2(io.MousePos.x + 10.0f, io.MousePos.y + 10.0f), kGray, "x10");
      scale *= 10.0f;
    }

    if (applyDelta) {
      const double scaling = usingCenterRange ? (double)scale : activeScaleFactor;
      editValue += delta * scaling;

      if (activeScaleFactor != 0.0 && (!usingCenterRange && io.KeyCtrl)) {
        editValue = std::round(editValue / activeScaleFactor) * activeScaleFactor;
      }
      editValue = optionalClamp(editValue, min, clampMin, max, clampMax);
    }
  }

  g_lastX = x;
}

}  // namespace sw::ui
