// runtime/device_input_provider — see device_input_provider.h. The sw analog of the TiXL SystemUi
// input statics (KeyHandler.PressedKeys + MouseInput.LastPosition/IsLeftButtonDown), a single
// process-global the shell overwrites from imgui io each frame and the device-input cook reads.
#include "runtime/device_input_provider.h"

namespace sw {

DeviceInputProvider& DeviceInputProvider::instance() {
  // Meyers singleton: one process-global, first-use construction, init-order safe (= the static
  // KeyHandler / MouseInput state in TiXL SystemUi).
  static DeviceInputProvider s_instance;
  return s_instance;
}

DeviceInputState& DeviceInputProvider::mutableState() { return state_; }

const DeviceInputState& DeviceInputProvider::snapshot() const { return state_; }

void DeviceInputProvider::reset() { state_ = DeviceInputState{}; }

}  // namespace sw
