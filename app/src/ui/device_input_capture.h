// ui/device_input_capture — the SHELL-side writer for the DeviceInputProvider (runtime). Reads the live
// ImGui io (io.MousePos / io.MouseDown / the ImGuiKey down-states) once per frame and translates it into
// the provider's VK-code-indexed key array + normalized mouse position. This is the ONE place imgui io
// crosses into the device-input rail; the runtime provider + the app cook never touch imgui.
//
// Zone: ui — it reads imgui (a ui concern) and writes the runtime provider (ui→…→runtime, legal). Mirror
// of keymap_binding.cpp (the other ui leaf that reads imgui io). One call per frame from main.cpp's shell,
// BEFORE framecook::run() (so the cook that frame sees this frame's input).
#pragma once

#include <cstdint>

namespace sw::ui {

// Capture this frame's imgui io into the DeviceInputProvider. `surfaceW`/`surfaceH` = the render surface
// size in pixels, used to normalize io.MousePos into the provider's [0,1] window space (= TiXL
// MouseInput.LastPosition). Call once per frame before the cook.
void captureDeviceInput(float surfaceW, float surfaceH);

}  // namespace sw::ui
