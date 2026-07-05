// ui/device_input_capture — see device_input_capture.h. Reads imgui io each frame and fills the runtime
// DeviceInputProvider's VK-code key array + normalized mouse. Data-driven imgui→VK map (rule 7).
#include "ui/device_input_capture.h"

#include "imgui.h"
#include "runtime/device_input_provider.h"

namespace sw::ui {
namespace {

// imgui ImGuiKey → Windows VK code. Only the keys the io/input operators can address are mapped: the
// digit row (VK 48..57) + numpad (VK 96..105) KeyboardInputAsInt scans, Enter (the KeyboardInput.t3
// default 13), and the common named/letter keys a KeyboardInput.Key slider could select. VK letters =
// ASCII 'A'..'Z' (65..90); VK digits = ASCII '0'..'9' (48..57); these EQUAL the values TiXL's Key enum
// carries, so the operator semantics stay byte-identical. A key not in this table is simply never set
// (its VK slot stays false) — a bounded, honest gap, not a silent wrong mapping.
struct KeyMap { ImGuiKey imgui; int vk; };
const KeyMap kKeyMap[] = {
    // digit row (VK 48..57 == ASCII '0'..'9')
    {ImGuiKey_0, 48}, {ImGuiKey_1, 49}, {ImGuiKey_2, 50}, {ImGuiKey_3, 51}, {ImGuiKey_4, 52},
    {ImGuiKey_5, 53}, {ImGuiKey_6, 54}, {ImGuiKey_7, 55}, {ImGuiKey_8, 56}, {ImGuiKey_9, 57},
    // numpad (VK_NUMPAD0..9 == 96..105)
    {ImGuiKey_Keypad0, 96}, {ImGuiKey_Keypad1, 97}, {ImGuiKey_Keypad2, 98}, {ImGuiKey_Keypad3, 99},
    {ImGuiKey_Keypad4, 100}, {ImGuiKey_Keypad5, 101}, {ImGuiKey_Keypad6, 102}, {ImGuiKey_Keypad7, 103},
    {ImGuiKey_Keypad8, 104}, {ImGuiKey_Keypad9, 105},
    // letters (VK == ASCII 'A'..'Z' == 65..90)
    {ImGuiKey_A, 65}, {ImGuiKey_B, 66}, {ImGuiKey_C, 67}, {ImGuiKey_D, 68}, {ImGuiKey_E, 69},
    {ImGuiKey_F, 70}, {ImGuiKey_G, 71}, {ImGuiKey_H, 72}, {ImGuiKey_I, 73}, {ImGuiKey_J, 74},
    {ImGuiKey_K, 75}, {ImGuiKey_L, 76}, {ImGuiKey_M, 77}, {ImGuiKey_N, 78}, {ImGuiKey_O, 79},
    {ImGuiKey_P, 80}, {ImGuiKey_Q, 81}, {ImGuiKey_R, 82}, {ImGuiKey_S, 83}, {ImGuiKey_T, 84},
    {ImGuiKey_U, 85}, {ImGuiKey_V, 86}, {ImGuiKey_W, 87}, {ImGuiKey_X, 88}, {ImGuiKey_Y, 89},
    {ImGuiKey_Z, 90},
    // common named keys (Windows VK codes)
    {ImGuiKey_Enter, 13}, {ImGuiKey_Space, 32}, {ImGuiKey_Escape, 27}, {ImGuiKey_Backspace, 8},
    {ImGuiKey_Tab, 9}, {ImGuiKey_LeftArrow, 37}, {ImGuiKey_UpArrow, 38}, {ImGuiKey_RightArrow, 39},
    {ImGuiKey_DownArrow, 40}, {ImGuiKey_LeftShift, 16}, {ImGuiKey_LeftCtrl, 17}, {ImGuiKey_LeftAlt, 18},
};

}  // namespace

void captureDeviceInput(float surfaceW, float surfaceH) {
  DeviceInputState& s = DeviceInputProvider::instance().mutableState();
  const ImGuiIO& io = ImGui::GetIO();

  // Keys: clear then set from the map (a released key must fall back to false — the edge modes depend on it).
  s.keys.fill(false);
  for (const KeyMap& k : kKeyMap) {
    if (k.vk >= 0 && k.vk < 256 && ImGui::IsKeyDown(k.imgui)) s.keys[(size_t)k.vk] = true;
  }

  // Mouse: normalize the pixel cursor into [0,1] window space (= TiXL MouseInput.LastPosition). Guard a
  // zero/uninitialised surface (io.MousePos can be (-FLT_MAX) before the first move — clamp to [0,1]).
  if (surfaceW > 0.0f && surfaceH > 0.0f && io.MousePos.x > -1e6f) {
    float nx = io.MousePos.x / surfaceW;
    float ny = io.MousePos.y / surfaceH;
    s.mouseX = nx < 0.0f ? 0.0f : (nx > 1.0f ? 1.0f : nx);
    s.mouseY = ny < 0.0f ? 0.0f : (ny > 1.0f ? 1.0f : ny);
  }
  s.leftButtonDown = io.MouseDown[0];

  // Gamepad: no macOS capture wired yet (GameController framework is the future platform-region .mm per
  // the lane brief) — the pad slots stay zero-init (isConnected=false). Nothing to write here.
}

}  // namespace sw::ui
