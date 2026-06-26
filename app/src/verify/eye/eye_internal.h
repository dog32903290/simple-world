// eye_internal.h — verify-internal seam shared between eye.mm (the live sink) and
// eye_selftest.mm (the headless RED->GREEN teeth). NOT a public API: only the two eye
// TUs include it. Split out so eye.mm stays ≤400 lines (ARCHITECTURE rule 4) while the
// self-tests still drive the SAME internal item buffer / popup walk the live map uses —
// testing the real code path, not a copy. Everything lives in sw::eye::detail.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sw::eye::detail {

// A collected widget rect (ImGui screen coords) for the pending map request.
struct Item {
  std::string label;
  float x0, y0, x1, y1;
  // Part C (occlusion): the ID of the ImGui window that OWNED this widget at record time
  // (ImGui::GetCurrentWindow()->ID). 0 = recorded outside any window scope (e.g. recordRect
  // for canvas overlays that have no owning imgui window). writeWidgetMap compares this against
  // the topmost input-accepting window over the item's center to decide `occluded`. LAST member
  // + default 0 so the existing positional aggregate inits ({label,x0,y0,x1,y1}) stay valid.
  unsigned int ownerWindow = 0;
};
// A native NSMenu row (metadata only — no rect; driven via key equivalent).
struct NativeMenuItem {
  std::string label;     // "nsmenu:<menu>:<title>"
  std::string shortcut;  // "cmd+s" / "cmd+shift+s"
};

// The live per-frame buffers (defined in eye.mm). The self-test reads/clears them to
// assert the exact rows the live map would serialize.
std::vector<Item>& items();
std::vector<NativeMenuItem>& nativeItems();

// True if the recorded set currently holds a row whose label starts with `prefix`.
bool itemsHavePrefix(const char* prefix);

// Gap 1: fold every open combo/popup window's rows into items() as popup_item:<win>:<row>
// (see eye.mm). The self-test calls this directly to prove the OpenPopupStack walk fires.
void recordOpenPopupItems();

// Part C (occlusion): is (px,py) covered by an input-accepting window other than ownerWindow?
// (see eye.mm — the same predicate writeWidgetMap stamps into map.json's "occluded".) The
// self-test drives it directly with two overlapping windows to prove RED->GREEN.
bool pointOccluded(float px, float py, unsigned int ownerWindow);

// PNG round-trip primitives (defined in eye.mm) the pixel self-test drives.
bool writePNG(const std::string& path, const uint8_t* rgba, int w, int h);
std::string outPath(const char* name);
void ensureDir();

}  // namespace sw::eye::detail
