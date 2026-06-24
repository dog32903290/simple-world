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

// PNG round-trip primitives (defined in eye.mm) the pixel self-test drives.
bool writePNG(const std::string& path, const uint8_t* rgba, int w, int h);
std::string outPath(const char* name);
void ensureDir();

}  // namespace sw::eye::detail
