// ui/gradient_widget — inspector Gradient color-band widget.
//
// Port of TiXL GradientEditor.cs (Editor/Gui/UiHelpers/GradientEditor.cs:407-508 DrawGradient,
// 240-343 DrawHandle, 94-119 insert logic, 44-86 trash zone, 504-507 constants).
//
// Zone: ui. Depends on runtime/sw_gradient.h only. No Metal, no app layer.
//
// FORK (named vs TiXL):
//   • id-by-index: TiXL identifies stops by Guid (GradientEditor.cs:244 step.Id.GetHashCode() PushID).
//     sw uses loop index as PushID (sw_gradient.h drop-Guid fork). Sort on drag-release may remap the
//     handle index by one slot; acceptable latency (one frame, single stop reorder only).
//   • no presets: TiXL GradientPresets save/load sub-menu (GradientEditor.cs:173-213) deferred (TODO).
//   • no cloneIfModified: TiXL clones the gradient on first hover to keep the definition clean.
//     sw edits in-place; caller owns the undo snapshot (return value true = modified).
//   • no CurveOverlays: TiXL DrawCurveLines (cs:352-391) overlays RGBA channel curves on hover.
//     Deferred (TODO) — adds significant per-frame sampling and is a visual-only affordance.
//   • no UiColors/CustomComponents: TiXL uses theme colors (UiColors.BackgroundFull, ForegroundFull).
//     sw uses hard-coded ImGui equivalents (IM_COL32) since we have no theme system.
//   • stripe background: TiXL calls CustomComponents.FillWithStripes. sw draws a simple grey
//     checkerboard strip (10px tiles) to indicate transparency. Same intent, simpler impl.
#include "ui/gradient_widget.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "imgui.h"

#include "runtime/sw_gradient.h"  // SwGradient, SwGradientStep, kGradient*

namespace sw::ui {
namespace {

// ---- TiXL GradientEditor constants (GradientEditor.cs:504-507) ----
constexpr float kBarHeight      = 24.0f;  // cs:kBarHeight equivalent (no named const — inferred from drawing)
constexpr float kHandleW        =  9.0f;  // cs:StepHandleSize.X (= 9 * UiScaleFactor; sw = 1.0x)
constexpr float kHandleH        = 15.0f;  // cs:StepHandleSize.Y (= 15 * UiScaleFactor)
constexpr float kRemoveThreshold = 15.0f; // cs:RemoveThreshold (cs:504)
// cs:MinInsertHeight=15, RequiredHeightForHandles=20 — we always draw handles (height is fixed).

// Clamp a float to [lo, hi].
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Convert simd::float4 (RGBA linear) to ImU32 (RGBA packed, suitable for ImGui draw calls).
// = ImGui::ColorConvertFloat4ToU32 (cs:452 ImGui.ColorConvertFloat4ToU32(step.Color)).
// ImGui's convention: R in low byte, A in high byte: IM_COL32(R,G,B,A) = 0xAABBGGRR.
inline ImU32 colorToU32(const simd::float4& c) {
  auto to8 = [](float v) -> int {
    int i = (int)(v * 255.0f + 0.5f);
    return i < 0 ? 0 : (i > 255 ? 255 : i);
  };
  return IM_COL32(to8(c.x), to8(c.y), to8(c.z), to8(c.w));
}

// Sample the gradient at t → ImU32 (for the complex-interpolation stepwise scan).
inline ImU32 sampleU32(const SwGradient& g, float t) { return colorToU32(g.sample(t)); }

// Draw a simple checkerboard stripe behind the color bar to indicate alpha transparency.
// Faithful-intent port of TiXL's CustomComponents.FillWithStripes (no source, visual parity).
void drawAlphaBackground(ImDrawList* dl, ImVec2 minP, ImVec2 maxP) {
  constexpr float tile = 8.0f;
  dl->AddRectFilled(minP, maxP, IM_COL32(100, 100, 100, 255));
  for (float x = minP.x; x < maxP.x; x += tile * 2) {
    for (float y = minP.y; y < maxP.y; y += tile) {
      float ox = (((int)((y - minP.y) / tile)) % 2 == 0) ? tile : 0.0f;
      float x0 = x + ox, x1 = x0 + tile;
      if (x0 >= maxP.x) break;
      if (x1 > maxP.x) x1 = maxP.x;
      float y1 = y + tile > maxP.y ? maxP.y : y + tile;
      dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y1), IM_COL32(150, 150, 150, 255));
    }
  }
}

// Draw the gradient bar itself (read-only display of the color ramp).
// Port of TiXL GradientEditor.DrawGradient (cs:407-502).
void drawGradientBar(const SwGradient& g, ImDrawList* dl, ImVec2 barMin, ImVec2 barMax) {
  // Dark background + border (cs:409-410).
  dl->AddRectFilled(barMin, barMax, IM_COL32(38, 38, 38, 255));  // cs: 0.15f grey
  // Stripe background under transparent stops (cs:419-422 CustomComponents.FillWithStripes).
  drawAlphaBackground(dl, barMin, barMax);

  if (g.steps.empty()) {
    dl->AddText(ImVec2(barMin.x + (barMax.x - barMin.x) * 0.5f - 4.0f, barMin.y + 4.0f),
                IM_COL32(200, 200, 200, 100), "?");
    dl->AddRect(barMin, barMax, IM_COL32(0, 0, 0, 255));
    return;
  }

  ImVec2 minPos = barMin;
  ImVec2 maxPos = barMax;
  float barW = barMax.x - barMin.x;

  ImU32 leftColor = colorToU32(g.steps[0].color);  // cs:428

  // Complex path (Smooth / OkLab / Spline): stepwise with 5 sub-rects per segment (cs:431-465).
  if (g.interpolation == kGradientSmooth || g.interpolation == kGradientOkLab ||
      g.interpolation == kGradientSpline) {
    float f = 0.0f;
    for (size_t si = 0; si < g.steps.size(); ++si) {
      float rightF = g.steps[si].pos;   // cs:441
      float rangeF = rightF - f;        // cs:442
      constexpr int kSubSteps = 5;      // cs:439
      float stepSizeF   = rangeF / kSubSteps;            // cs:443
      float pixelStep   = barW * rangeF / kSubSteps;     // cs:445
      maxPos.x = minPos.x + pixelStep;                   // cs:446

      for (int i = 0; i < kSubSteps; ++i) {
        float nextF = f + stepSizeF;                      // cs:450
        ImU32 nextColor = sampleU32(g, nextF);            // cs:451
        dl->AddRectFilledMultiColor(minPos, maxPos,
                                    leftColor, nextColor,  // cs:453-458 TL,TR,BR,BL
                                    nextColor, leftColor);
        maxPos.x += pixelStep;                            // cs:459
        minPos.x += pixelStep;                            // cs:460
        f = nextF;                                        // cs:462
        leftColor = nextColor;                            // cs:463
      }
    }
  } else {
    // Linear / Hold: one rect per segment between consecutive stops (cs:468-495).
    for (const SwGradientStep& step : g.steps) {
      ImU32 rightColor = colorToU32(step.color);          // cs:472
      maxPos.x = barMin.x + barW * step.pos;             // cs:473
      if (g.interpolation == kGradientHold) {
        // Hold: fill with leftColor all the way (cs:474-481).
        dl->AddRectFilledMultiColor(minPos, maxPos,
                                    leftColor, leftColor,
                                    leftColor, leftColor);
      } else {
        // Linear (default): interpolate left→right (cs:483-490).
        dl->AddRectFilledMultiColor(minPos, maxPos,
                                    leftColor, rightColor,
                                    rightColor, leftColor);
      }
      minPos.x = maxPos.x;                               // cs:493
      leftColor = rightColor;                             // cs:494
    }
  }

  // Fill any remaining tail with leftColor (cs:498-500).
  if (minPos.x < barMax.x) {
    dl->AddRectFilled(minPos, barMax, leftColor);
  }

  // Border over everything (cs:409 AddRect Black).
  dl->AddRect(barMin, barMax, IM_COL32(0, 0, 0, 255));
}

}  // namespace

// ---- Public entry point ----
// drawGradientWidget — port of TiXL GradientEditor.Draw() (cs:18-238).
// Draws:
//   1. The gradient color bar (kBarHeight = 24px) via drawGradientBar.
//   2. Stop handles (kHandleW×kHandleH = 9×15 px squares) below the bar,
//      colored with the stop's color + white border.
//   3. Invisible drag buttons over each handle (left-drag moves the stop; release below
//      kRemoveThreshold removes it; click opens a ColorEdit4 popup).
//   4. Invisible insert button over the bar area (click inserts a new stop at the cursor position).
//   5. Context menu: Interpolation sub-menu (change the gradient's interpolation mode).
//
// Returns true if the gradient was modified.
bool drawGradientWidget(SwGradient& gradient, float width) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  bool modified = false;

  // --- Bar geometry ---
  const ImVec2 barMin = cursor;
  const ImVec2 barMax = ImVec2(cursor.x + width, cursor.y + kBarHeight);

  // --- Sort handles (defensive; producer should keep sorted) ---
  gradient.sortHandles();

  // --- Draw the color ramp ---
  drawGradientBar(gradient, dl, barMin, barMax);

  // Reserve vertical space for the bar.
  ImGui::Dummy(ImVec2(width, kBarHeight));

  // --- Stop handles & interaction ---
  // Handle area: immediately below the bar. We draw handles "hanging" below barMax.y.
  const float handleTop  = barMax.y;                         // cs:247 handleArea.Min.Y
  const float handleBot  = handleTop + kHandleH;             // cs:348 handleArea.Max.Y+2 ≈

  int removeIdx = -1;  // index of stop to remove (set on drag-release below trash zone)

  // Keep track of whether any stop is being dragged (for the trash zone display).
  static bool s_anyDragging = false;
  bool anyDragging = false;

  for (int i = 0; i < (int)gradient.steps.size(); ++i) {
    SwGradientStep& step = gradient.steps[i];

    // Handle X center = lerp(barMin.x, barMax.x, step.pos), offset by half handle width.
    float cx = barMin.x + width * step.pos;  // cs:246 / cs:347
    ImVec2 hMin = ImVec2(cx - kHandleW * 0.5f, handleTop);  // cs:246-247
    ImVec2 hMax = ImVec2(cx + kHandleW * 0.5f, handleTop + kHandleH);

    // Invisible button over the handle area (full height from bar top so it captures clicks
    // landing slightly above the handle square — cs:250 InvisibleButton height = full area).
    ImGui::PushID(i);
    ImGui::SetCursorScreenPos(hMin);
    ImGui::InvisibleButton("gradStop", ImVec2(kHandleW, kHandleH));

    // Drag: move the stop horizontally (cs:267-288).
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      anyDragging = true;
      float newPos = clamp01((ImGui::GetMousePos().x - barMin.x) / width);  // cs:282
      step.pos = newPos;
      modified = true;
    }

    // Drag-release: check trash zone (cs:297-303).
    if (ImGui::IsItemDeactivated()) {
      float mouseY = ImGui::GetMousePos().y;
      bool outsideThreshold = mouseY > handleBot + kRemoveThreshold;  // cs:285/299
      if (outsideThreshold && (int)gradient.steps.size() > 1) {
        removeIdx = i;  // defer deletion to after loop (invalidates iterators)
        modified = true;
      } else {
        // sort on release (cs:33 SortHandles — TiXL sorts before every draw; we sort on release
        // to preserve handle identity during drag).
        gradient.sortHandles();
        if (modified) {}  // already set above
      }
    }

    // Click (no drag) → open color-picker popup (cs:326-334).
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 4.0f /* < 2px */ &&
        !ImGui::IsPopupOpen("##gradColorEdit")) {
      ImGui::OpenPopup("##gradColorEdit");
    }

    // ColorEdit popup (cs:336-340 ColorEditPopup.DrawPopup).
    if (ImGui::BeginPopup("##gradColorEdit")) {
      float col[4] = {step.color.x, step.color.y, step.color.z, step.color.w};
      if (ImGui::ColorPicker4("##colorpick", col,
                              ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_AlphaBar)) {
        step.color = simd::make_float4(col[0], col[1], col[2], col[3]);
        modified = true;
      }
      ImGui::EndPopup();
    }

    // Draw the stop handle square (cs:314: AddRectFilled + AddRect).
    ImU32 fillCol  = colorToU32(step.color);
    ImU32 bordCol  = IM_COL32(255, 255, 255, 200);  // cs:324 UiColors.ForegroundFull
    ImU32 shadCol  = IM_COL32(0, 0, 0, 120);        // cs:323 UiColors.BackgroundFull.Fade(0.7f)
    dl->AddRectFilled(hMin, hMax, fillCol);          // cs:314
    dl->AddRect(ImVec2(hMin.x - 1, hMin.y - 1), ImVec2(hMax.x + 1, hMax.y + 1), shadCol);  // cs:323 offset shadow
    dl->AddRect(hMin, hMax, bordCol);                // cs:324 foreground border

    ImGui::PopID();
  }

  // --- Remove deferred stop ---
  if (removeIdx >= 0 && removeIdx < (int)gradient.steps.size()) {
    gradient.steps.erase(gradient.steps.begin() + removeIdx);
    gradient.sortHandles();
  }

  // --- Trash zone visual (cs:54-80) ---
  s_anyDragging = anyDragging;
  if (s_anyDragging) {
    ImDrawList* fgDl = ImGui::GetForegroundDrawList();
    float mouseY = ImGui::GetMousePos().y;
    bool inTrash  = mouseY > handleBot + kRemoveThreshold;
    ImU32 trashCol = inTrash ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 200, 60, 200);
    ImVec2 trMin = ImVec2(barMin.x, handleBot + 4);
    ImVec2 trMax = ImVec2(barMax.x, handleBot + 26);
    fgDl->AddRectFilled(trMin, trMax, IM_COL32(20, 20, 20, 160));
    fgDl->AddRect(trMin, trMax, trashCol);
    // "x" indicator (no Icon system in sw; use text glyph).
    float cx = (trMin.x + trMax.x) * 0.5f - 4.0f;
    fgDl->AddText(ImVec2(cx, trMin.y + 2), trashCol, "x");
  }

  // Reserve space for the handle row.
  ImGui::SetCursorScreenPos(ImVec2(cursor.x, handleBot));
  ImGui::Dummy(ImVec2(width, 2.0f));  // 2px gap

  // --- Insert new stop (cs:94-119: InvisibleButton over the lower half of the bar) ---
  ImGui::SetCursorScreenPos(ImVec2(barMin.x, barMin.y + kBarHeight * 0.5f));
  ImGui::InvisibleButton("gradInsert", ImVec2(width, kBarHeight * 0.5f));
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    float t = clamp01((ImGui::GetMousePos().x - barMin.x) / width);  // cs:100
    SwGradientStep ns;
    ns.pos   = t;
    ns.color = gradient.sample(t);  // cs:108: sample the current gradient at t
    gradient.steps.push_back(ns);   // cs:104-110
    gradient.sortHandles();
    modified = true;
  }
  // Hover ghost line (cs:117: draw a vertical line guide).
  if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
    float mx = ImGui::GetMousePos().x;
    if (mx >= barMin.x && mx <= barMax.x) {
      dl->AddLine(ImVec2(mx, barMin.y), ImVec2(mx, barMax.y),
                  IM_COL32(200, 200, 200, 120), 1.0f);
    }
  }

  // --- Interpolation context menu (cs:215-228) ---
  // Right-click on the bar → change interpolation mode. TiXL: ContextMenuForItem sub-menu.
  ImGui::SetCursorScreenPos(barMin);
  ImGui::InvisibleButton("gradBar", ImVec2(width, kBarHeight));
  if (ImGui::BeginPopupContextItem("##gradCtx")) {
    ImGui::TextDisabled("Interpolation");
    ImGui::Separator();
    const char* modes[] = {"Linear", "Hold", "Smooth", "OkLab", "Spline"};
    for (int m = 0; m < 5; ++m) {
      bool sel = (gradient.interpolation == m);
      if (ImGui::MenuItem(modes[m], nullptr, sel)) {
        gradient.interpolation = m;
        modified = true;
      }
    }
    // TODO: presets sub-menu (TiXL GradientEditor.cs:173-213 — GradientPresets.Save/Load).
    ImGui::EndPopup();
  }

  return modified;
}

}  // namespace sw::ui
