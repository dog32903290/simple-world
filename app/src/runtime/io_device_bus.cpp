// runtime/io_device_bus — the per-frame device-value sink implementation (Meyers singleton).
// See io_device_bus.h for the leaf-seam rationale (app writes, runtime reads).
#include "runtime/io_device_bus.h"

namespace sw {

IoDeviceBus& ioDeviceBus() {
  static IoDeviceBus bus;
  return bus;
}

void ingestMidiSignal(int kind, int channel, int controllerId, int controllerValue) {
  ioDeviceBus().midi.push_back({kind, channel, controllerId, controllerValue});
}

void ingestOscArg(const std::string& address, float value, int argIndex) {
  ioDeviceBus().osc.push_back({address, value, argIndex});
}

void endIoDeviceFrame() {
  IoDeviceBus& bus = ioDeviceBus();
  bus.midi.clear();
  bus.osc.clear();
}

}  // namespace sw
