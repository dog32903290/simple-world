// ui/variation_panel — the Variation window imgui surface (P2). Faithful to TiXL VariationCanvas:
// a 3-column grid of snapshot cells (title bottom-left, index bottom-right, active-dot + selection
// border), with Grab / Activate / Delete per cell; below it the N-way weighted Mix (per-snapshot
// weight sliders + Apply) and the full 2-way crossfader (left/right pickers + 0..127 fader).
// Zone: ui. All state + wiring lives in app/variation_panel; this draws + forwards gestures.
#include "ui/variation_panel.h"

#include <string>
#include <vector>

#include "imgui.h"

#include "app/document.h"
#include "app/variation_panel.h"  // varpanel:: pool / mix / crossfader wiring
#include "verify/eye/eye.h"       // one-line hooks: slot/button rects for the hand

namespace sw::ui {
namespace {

// TiXL VariationThumbnail: 3 columns, 160×90 cells, 3px padding (VariationBaseCanvas.cs:691 columns=3,
// VariationThumbnail.cs:330-332). We use the same column count + aspect; the cell is sized to fit the
// window width so the grid reads like the TiXL canvas without a separate scroll-canvas.
constexpr float kCellAspect = 90.0f / 160.0f;  // TiXL 16:9 thumbnail
constexpr float kCellPad = 3.0f;               // TiXL SnapPadding

// One cell of the snapshot grid. Returns a gesture for the panel to act on (the click selects the cell
// as a crossfade/activate target; the buttons below the grid act on the selected slot).
enum class CellGesture { None, Select, Activate };

CellGesture drawSlotCell(const sw::varpanel::SlotInfo& si, float cellW, float cellH, bool selected) {
  ImGui::PushID(si.index);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 p1 = ImVec2(p0.x + cellW, p0.y + cellH);
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // Background (TiXL: faded gray fill). Filled slots a touch brighter than empty ones.
  dl->AddRectFilled(p0, p1, ImGui::GetColorU32(si.filled ? ImVec4(0.18f, 0.18f, 0.20f, 1.0f)
                                                         : ImVec4(0.10f, 0.10f, 0.11f, 1.0f)),
                    3.0f);

  // The whole cell is a click target (TiXL bare click = select/activate). Double-click = activate.
  ImGui::InvisibleButton("cell", ImVec2(cellW, cellH));
  const bool hovered = ImGui::IsItemHovered();
  CellGesture g = CellGesture::None;
  if (ImGui::IsItemClicked()) g = CellGesture::Select;
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && si.filled)
    g = CellGesture::Activate;

  // Border: selection (bright) / hover / normal (TiXL VariationThumbnail.cs:72-78, 111-122).
  ImU32 border = ImGui::GetColorU32(ImVec4(0.35f, 0.35f, 0.38f, 0.8f));  // normal
  float thickness = 1.0f;
  if (selected) { border = ImGui::GetColorU32(ImVec4(1.0f, 0.7f, 0.2f, 1.0f)); thickness = 2.0f; }
  else if (hovered) { border = ImGui::GetColorU32(ImVec4(0.9f, 0.9f, 0.9f, 0.4f)); thickness = 2.0f; }
  dl->AddRect(p0, p1, border, 3.0f, 0, thickness);

  // Title bottom-left (TiXL: "Untitled" when empty); index bottom-right "00" (ActivationIndex).
  const ImU32 textCol = ImGui::GetColorU32(ImVec4(1, 1, 1, si.filled ? 0.9f : 0.35f));
  const float fontH = ImGui::GetTextLineHeight();
  const std::string title = si.filled ? si.title : "(empty)";
  dl->AddText(ImVec2(p0.x + 4, p1.y - fontH - 4), textCol, title.c_str());
  char idx[8];
  std::snprintf(idx, sizeof(idx), "%02d", si.index);
  const ImVec2 idxSize = ImGui::CalcTextSize(idx);
  dl->AddText(ImVec2(p1.x - idxSize.x - 4, p1.y - fontH - 4),
              ImGui::GetColorU32(ImVec4(1, 1, 1, 0.3f)), idx);
  // Param-count readout (top-left), so a grabbed snapshot shows it captured something.
  if (si.filled) {
    char pc[16];
    std::snprintf(pc, sizeof(pc), "%d p", si.paramCount);
    dl->AddText(ImVec2(p0.x + 4, p0.y + 3), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.4f)), pc);
  }
  // Active-dot (TiXL: small filled circle bottom-right when State==Active).
  if (si.active)
    dl->AddCircleFilled(ImVec2(p1.x - 6, p0.y + 6), 3.0f,
                        ImGui::GetColorU32(ImVec4(0.3f, 1.0f, 0.4f, 1.0f)));

  // Eye hook: a stable per-slot key so the hand can target a cell by index.
  char key[24];
  std::snprintf(key, sizeof(key), "var_slot_%d", si.index);
  sw::eye::recordRect(key, p0.x, p0.y, p1.x, p1.y);

  ImGui::PopID();
  return g;
}

}  // namespace

bool& variationPanelVisible() {
  static bool g_visible = false;  // default OFF: opened on demand from the toolbar, never over canvas
  return g_visible;
}

void drawVariationPanel() {
  if (!variationPanelVisible()) return;  // closed: the canvas underneath keeps every click
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  // RIGHT-edge column (bottom), NOT over the canvas. The old bottom-LEFT default (+16, bottom-376)
  // overlapped the graph's left/lower nodes (compound_smoke node:5 at canvas 400,480; node:1/2's
  // column) — and an always-open panel there ate canvas clicks/right-clicks on those nodes, killing
  // coordinate hit-test. TiXL docks tool windows at the screen edge clear of the graph; we stack on
  // the right under the Asset Library (which sits y+216, h≈280). 柏為 drags it wherever he wants.
  ImGui::SetNextWindowSize(ImVec2(360.0f, 360.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + vp->WorkSize.x - 376.0f, vp->WorkPos.y + 508.0f),
      ImGuiCond_FirstUseEver);
  ImGui::Begin("Variation");

  // The selected slot persists across frames (the Grab/Activate/Delete + crossfade pickers act on it).
  static int s_selected = 0;  // 0 = none; else 1..kSlotCount

  const std::vector<sw::varpanel::SlotInfo> slots = sw::varpanel::slots();

  // ── Snapshot pool grid (3 columns, TiXL VariationCanvas) ───────────────────────────────────────
  ImGui::TextDisabled("Snapshots");
  const float availW = ImGui::GetContentRegionAvail().x;
  const float cellW = (availW - kCellPad * (sw::varpanel::kSlotColumns - 1)) / sw::varpanel::kSlotColumns;
  const float cellH = cellW * kCellAspect;
  for (int i = 0; i < sw::varpanel::kSlotCount; ++i) {
    if (i % sw::varpanel::kSlotColumns != 0) ImGui::SameLine(0.0f, kCellPad);
    const bool selected = slots[i].index == s_selected;
    const CellGesture g = drawSlotCell(slots[i], cellW, cellH, selected);
    if (g == CellGesture::Select) s_selected = slots[i].index;
    else if (g == CellGesture::Activate) {
      s_selected = slots[i].index;
      sw::varpanel::activateSnapshot(sw::doc::g_lib(), slots[i].index);
      sw::doc::g_status = "activated snapshot " + std::to_string(slots[i].index);
    }
  }

  // ── Grab / Activate / Delete on the selected slot ──────────────────────────────────────────────
  ImGui::Spacing();
  const bool haveSel = s_selected >= 1 && s_selected <= sw::varpanel::kSlotCount;
  const bool selFilled = haveSel && slots[s_selected - 1].filled;
  if (ImGui::Button("Grab")) {
    const int target = haveSel ? s_selected : 1;  // default to slot 1 if nothing picked
    if (sw::varpanel::grabSnapshot(sw::doc::g_lib(), target)) {
      s_selected = target;
      sw::doc::g_status = "grabbed snapshot into slot " + std::to_string(target);
    }
  }
  sw::eye::recordItem("var_grab");
  ImGui::SameLine();
  if (!selFilled) ImGui::BeginDisabled();
  if (ImGui::Button("Activate") && selFilled) {
    sw::varpanel::activateSnapshot(sw::doc::g_lib(), s_selected);
    sw::doc::g_status = "activated snapshot " + std::to_string(s_selected);
  }
  sw::eye::recordItem("var_activate");
  ImGui::SameLine();
  if (ImGui::Button("Delete") && selFilled) {
    sw::varpanel::deleteSnapshot(s_selected);
    sw::doc::g_status = "deleted snapshot " + std::to_string(s_selected);
  }
  sw::eye::recordItem("var_delete");
  if (!selFilled) ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled(haveSel ? "slot %d" : "(pick a slot)", s_selected);

  // ── N-way weighted mix (per-snapshot weight sliders + Apply) ────────────────────────────────────
  ImGui::Separator();
  ImGui::TextDisabled("N-way Mix (weighted blend)");
  for (int i = 0; i < sw::varpanel::kSlotCount; ++i) {
    if (!slots[i].filled) continue;
    ImGui::PushID(1000 + slots[i].index);
    float w = sw::varpanel::mixWeight(slots[i].index);
    ImGui::SetNextItemWidth(160.0f);
    char label[32];
    std::snprintf(label, sizeof(label), "slot %d", slots[i].index);
    if (ImGui::SliderFloat(label, &w, 0.0f, 4.0f, "%.2f"))
      sw::varpanel::setMixWeight(slots[i].index, w);
    char key[24];
    std::snprintf(key, sizeof(key), "var_mixw_%d", slots[i].index);
    sw::eye::recordItem(key);
    ImGui::PopID();
  }
  if (ImGui::Button("Apply Mix")) {
    if (sw::varpanel::applyMix(sw::doc::g_lib()))
      sw::doc::g_status = "applied N-way mix";
  }
  sw::eye::recordItem("var_apply_mix");

  // ── Full 2-way crossfader (left/right pickers + 0..127 fader) ───────────────────────────────────
  ImGui::Separator();
  ImGui::TextDisabled("Crossfader (2-way)");
  static int s_left = 0, s_right = 0;
  static float s_fader = 0.0f;
  // Build the picker option list = filled slots.
  std::string opts;          // combo items separated by '\0' (ImGui combo format)
  std::vector<int> optSlot;  // parallel: option index -> slotIndex
  for (int i = 0; i < sw::varpanel::kSlotCount; ++i)
    if (slots[i].filled) { opts += "Slot " + std::to_string(slots[i].index); opts += '\0'; optSlot.push_back(slots[i].index); }
  auto comboFor = [&](const char* lbl, int& slotVar) {
    int cur = -1;
    for (size_t k = 0; k < optSlot.size(); ++k) if (optSlot[k] == slotVar) cur = (int)k;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo(lbl, &cur, opts.empty() ? "(none)\0" : opts.c_str()))
      if (cur >= 0 && cur < (int)optSlot.size()) slotVar = optSlot[cur];
    sw::eye::recordItem(lbl);
  };
  comboFor("Left", s_left);
  ImGui::SameLine();
  comboFor("Right", s_right);
  ImGui::SameLine();
  if (ImGui::Button("Arm")) {
    if (sw::varpanel::armCrossfade(sw::doc::g_lib(), s_left, s_right)) {
      s_fader = 0.0f;
      sw::doc::g_status = "armed crossfade " + std::to_string(s_left) + " -> " + std::to_string(s_right);
    }
  }
  sw::eye::recordItem("var_arm_crossfade");
  if (sw::varpanel::crossfadeArmed()) {
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::SliderFloat("Crossfade", &s_fader, 0.0f, 127.0f, "%.0f"))
      sw::varpanel::updateCrossfade(s_fader);  // 0..127 fader -> spring target (TiXL BlendActions)
    sw::eye::recordItem("var_crossfade");
    ImGui::TextDisabled("L=%d R=%d  w=%.2f", sw::varpanel::crossfadeLeft(),
                        sw::varpanel::crossfadeRight(), sw::varpanel::crossfadeWeight());
  } else {
    ImGui::TextDisabled("arm two filled slots to crossfade");
  }

  ImGui::End();
}

}  // namespace sw::ui
