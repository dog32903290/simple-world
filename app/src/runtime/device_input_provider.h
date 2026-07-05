// runtime/device_input_provider — the process-global device-input hand-off singleton.
//
// = TiXL T3.SystemUi.KeyHandler (the static PressedKeys[] array) + T3.SystemUi.MouseInput
// (LastPosition / IsLeftButtonDown) + the XInput gamepad state the io/input operators read. In TiXL
// those are PROCESS-GLOBAL statics the EDITOR's input loop writes (from the render-window's captured
// key/mouse events) and the io/input operators READ during their per-frame Update(). This singleton
// is the faithful sw analog: the ONE channel between the app shell (the WRITER — populates it from the
// live ImGui io each frame) and the per-frame device-input cook (the READER — cookDeviceInputNodes).
//
// WHY imgui io, not an NSEvent global monitor (selection, per lane brief):
//   TiXL's KeyHandler.PressedKeys is the editor's OWN captured key array (app-focus scoped, filled from
//   the render window's key events — NOT an OS-global hook) and MouseInput.LastPosition is the render-
//   window-relative normalized cursor. ImGui's io (io.KeysDown / io.MousePos, already flowing through
//   this app's event pump — app_delegate.cpp / main.cpp) is EXACTLY that same app-focus-scoped input
//   stream. So reading imgui io reproduces TiXL's semantics with ZERO new platform capture code and NO
//   accessibility permission (an NSEvent CGEventTap global monitor would need one, and would also change
//   the semantics to OS-global — a fork away from TiXL). The shell writes this singleton from imgui io;
//   runtime/ stays a pure leaf (no imgui, no AppKit include here).
//
// KEY INDEXING — Windows virtual-key codes (the TiXL contract):
//   TiXL KeyboardInput.Key is a Windows VK code (KeyboardInput.t3 default 13 = VK_RETURN) and
//   KeyHandler.PressedKeys is indexed by VK code. KeyboardInputAsInt reads PressedKeys[48..57] (the
//   ASCII '0'..'9' digit-row VK codes, which equal their ASCII values) and PressedKeys[96..105] (the
//   VK_NUMPAD0..9 codes). So this singleton's key array is indexed by VK code — the shell translates
//   its platform key events into VK codes when it writes (keeping the operator semantics byte-identical
//   to TiXL regardless of the host key API). The array is 256 wide (VK codes are a single byte).
//
// Zone: runtime leaf — pure host state, no hardware / no UI (mirror of BpmProvider / PlaybackProvider).
// The app shell (ui/main) writes it; frame_cook (app) reads it. app→runtime and ui→…→runtime are legal.
// Not thread-safe (TiXL's statics aren't either — a single editor thread writes & reads).
#pragma once

#include <array>
#include <cstdint>

namespace sw {

// Mirror of the TiXL SystemUi input statics (KeyHandler.PressedKeys + MouseInput.LastPosition/
// IsLeftButtonDown), collapsed into ONE per-frame snapshot the shell overwrites and the cook reads.
// Zero-init = no key down, cursor at origin, no button — the TiXL cold-start state.
struct DeviceInputState {
  // PressedKeys, indexed by Windows VK code (see header). true = that key is currently held THIS frame.
  // KeyboardInput reads keys[VkCode]; KeyboardInputAsInt scans keys[48..57] / keys[96..105].
  std::array<bool, 256> keys{};

  // MouseInput.LastPosition — the render-window-relative cursor, NORMALIZED to [0,1] (x right, y down),
  // exactly as TiXL's MouseInput.LastPosition (a Vector2 in 0..1 window space). The shell divides the
  // pixel cursor by the render surface size before writing. (0,0)=top-left, (1,1)=bottom-right.
  float mouseX = 0.0f;
  float mouseY = 0.0f;

  // MouseInput.IsLeftButtonDown — left button held THIS frame.
  bool leftButtonDown = false;

  // Gamepad (XInput) — sw has NO XInput on macOS; the shell MAY populate this from the GameController
  // framework later (a platform-region .mm, per lane brief), but with no wired hardware capture it stays
  // zero-init (isConnected=false) → Gamepad.IsConnected=false, all axes/buttons 0. The golden injects a
  // synthetic state here (deferred-hw-verify). Axes are XInput-normalized: thumbs [-1,1], triggers [0,1],
  // buttons/dpad 0|1. Indexed [0..3] = the four controller slots (Gamepad.Index).
  struct Pad {
    bool isConnected = false;
    float leftThumbX = 0.0f, leftThumbY = 0.0f;
    float rightThumbX = 0.0f, rightThumbY = 0.0f;
    float leftTrigger = 0.0f, rightTrigger = 0.0f;
    float dpadLeft = 0.0f, dpadRight = 0.0f, dpadUp = 0.0f, dpadDown = 0.0f;
    float buttonA = 0.0f, buttonB = 0.0f, buttonX = 0.0f, buttonY = 0.0f;
    float leftShoulder = 0.0f, rightShoulder = 0.0f;
    float leftStickButton = 0.0f, rightStickButton = 0.0f;
    float start = 0.0f, back = 0.0f;
  };
  std::array<Pad, 4> pads{};
};

// The one process-global (= KeyHandler + MouseInput's static state). The shell overwrites `mutableState()`
// each frame from imgui io; the cook reads `snapshot()`. Both return the SAME object (single-threaded,
// no double-buffer — TiXL's statics aren't double-buffered either).
class DeviceInputProvider {
 public:
  static DeviceInputProvider& instance();

  // WRITER handle (shell): overwrite the fields from the live imgui io this frame.
  DeviceInputState& mutableState();

  // READER handle (cook): the current device state (const view).
  const DeviceInputState& snapshot() const;

  // Test seam only (goldens): reset to the zero-init cold-start state between cases (TiXL has no such
  // reset — its statics persist for the app's life; a golden needs a clean slate per case).
  void reset();

 private:
  DeviceInputState state_;
};

}  // namespace sw
