// platform/live_binding.cpp — binding table: decoded OSC/MIDI event → matched binding → remap → target.
//
// Ground truth (external/tixl, read-only):
//   Core/Utils/MathUtils.cs:368-373       Remap(v,inMin,inMax,outMin,outMax).
//   Operators/Lib/io/midi/MidiInput.cs:208-210  remap controllerValue over 0..127 into OutputRange.
//   Operators/Lib/io/midi/MidiInput.cs:401-414  match: channel(<0=any) & controller(<0=any) & eventType.
//   Operators/Lib/io/osc/OscInput.cs:241-242     OSC address match = msg.Address.StartsWith(address).
//
// platform leaf: pure C++, no runtime / app includes.
#include "platform/live_binding.h"

namespace sw {

float remapLinear(float value, float inMin, float inMax, float outMin, float outMax) {
  // Byte-faithful to MathUtils.Remap (no clamp): factor = (v-inMin)/(inMax-inMin); v = factor*(outMax-outMin)+outMin.
  float factor = (value - inMin) / (inMax - inMin);
  return factor * (outMax - outMin) + outMin;
}

int LiveBindingTable::addBinding(const LiveBinding& b) {
  bindings_.push_back(b);
  return int(bindings_.size()) - 1;
}

void LiveBindingTable::clear() {
  bindings_.clear();
  targetValues_.clear();
}

bool LiveBindingTable::oscMatches(const LiveBinding& b, const std::string& address) {
  if (b.source != LiveSourceKind::Osc) return false;
  // TiXL OscInput.cs:241 — StartsWith(_address). An empty filter matches everything (string::compare
  // of a 0-length prefix is always a match), mirroring TiXL's `!IsNullOrEmpty(_address) && !StartsWith`
  // (i.e. an empty configured address falls through to "accept").
  if (b.oscAddress.empty()) return true;
  return address.size() >= b.oscAddress.size() &&
         address.compare(0, b.oscAddress.size(), b.oscAddress) == 0;
}

bool LiveBindingTable::midiMatches(const LiveBinding& b, MidiMatchKind kind, int channel,
                                   int controllerId) {
  if (b.source != LiveSourceKind::Midi) return false;
  // TiXL MidiInput.cs:401-414: each filter <0 (or "Any") matches anything; otherwise must equal.
  bool matchesKind    = (b.midiKind == MidiMatchKind::Any) || (b.midiKind == kind);
  bool matchesChannel = (b.midiChannel < 0) || (channel == b.midiChannel);
  bool matchesControl = (b.midiControl < 0) || (controllerId == b.midiControl);
  return matchesKind && matchesChannel && matchesControl;
}

int LiveBindingTable::ingestOsc(const std::string& address, float value) {
  int updated = 0;
  for (const auto& b : bindings_) {
    if (!oscMatches(b, address)) continue;
    targetValues_[b.target] = remapLinear(value, b.inMin, b.inMax, b.outMin, b.outMax);
    ++updated;
  }
  return updated;
}

int LiveBindingTable::ingestMidi(MidiMatchKind kind, int channel, int controllerId,
                                 int controllerValue) {
  int updated = 0;
  for (const auto& b : bindings_) {
    if (!midiMatches(b, kind, channel, controllerId)) continue;
    // TiXL remaps the controllerValue (0..127). The raw int is widened to float before Remap.
    targetValues_[b.target] =
        remapLinear(float(controllerValue), b.inMin, b.inMax, b.outMin, b.outMax);
    ++updated;
  }
  return updated;
}

bool LiveBindingTable::valueForTarget(const std::string& target, float& out) const {
  auto it = targetValues_.find(target);
  if (it == targetValues_.end()) return false;
  out = it->second;
  return true;
}

}  // namespace sw
