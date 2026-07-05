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

void emitMidiShort(int status, int data1, int data2, int bytes) {
  ioDeviceBus().midiOut.push_back({status, data1, data2, bytes});
}

void emitMidiSysex(const std::vector<uint8_t>& buffer) {
  ioDeviceBus().sysexOut.push_back(buffer);
}

void emitOscMessage(const std::string& address, const std::vector<float>& args) {
  ioDeviceBus().oscOut.push_back({address, args});
}

void endIoDeviceFrame() {
  IoDeviceBus& bus = ioDeviceBus();
  bus.midi.clear();
  bus.osc.clear();
  bus.midiOut.clear();
  bus.sysexOut.clear();
  bus.oscOut.clear();
}

}  // namespace sw
