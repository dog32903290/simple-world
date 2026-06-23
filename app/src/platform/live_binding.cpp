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

float lerpLinear(float a, float b, float t) {
  // Byte-faithful to MathUtils.Lerp (Core/Utils/MathUtils.cs:305-308): a + (b - a) * t.
  return a + (b - a) * t;
}

int LiveBindingTable::addBinding(const LiveBinding& b) {
  bindings_.push_back(b);
  states_.push_back(BindingState{});
  // Seed the target with this binding's DefaultOutputValue so valueForTarget returns it BEFORE any
  // event arrives (TiXL MidiInput.cs:201 _isDefaultValue path). The first matching event overwrites it.
  // If another binding already targets this name, the later addBinding's default wins as the seed; once
  // events fire, whichever binding last matched owns the value — the faithful per-op model.
  targetValues_[b.target] = b.defaultOutputValue;
  return int(bindings_.size()) - 1;
}

void LiveBindingTable::clear() {
  bindings_.clear();
  states_.clear();
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

void LiveBindingTable::applyToBinding(int i, float rawValue) {
  const LiveBinding& b = bindings_[i];
  BindingState& st = states_[i];
  // 1) Remap the raw value (MidiInput.cs:208-210 / OscInput raw-through).
  float remapped = remapLinear(rawValue, b.inMin, b.inMax, b.outMin, b.outMax);
  // 2) Damping (MidiInput.cs:218): damped = Lerp(new, dampedPrev, damping). dampedPrev starts at 0 and
  //    is NOT seeded to the first event — so the first event yields remapped*(1-damping). damping=0
  //    short-circuits to the raw remap.
  st.dampedValue = lerpLinear(remapped, st.dampedValue, b.damping);
  st.hasReceived = true;  // TiXL _isDefaultValue = false (MidiInput.cs:171)
  targetValues_[b.target] = st.dampedValue;
}

int LiveBindingTable::ingestOsc(const std::string& address, float value) {
  int updated = 0;
  for (int i = 0; i < int(bindings_.size()); ++i) {
    if (!oscMatches(bindings_[i], address)) continue;
    applyToBinding(i, value);
    ++updated;
  }
  return updated;
}

int LiveBindingTable::ingestMidi(MidiMatchKind kind, int channel, int controllerId,
                                 int controllerValue) {
  int updated = 0;
  for (int i = 0; i < int(bindings_.size()); ++i) {
    if (!midiMatches(bindings_[i], kind, channel, controllerId)) continue;
    // TiXL remaps the controllerValue (0..127). The raw int is widened to float before Remap.
    applyToBinding(i, float(controllerValue));
    ++updated;
  }
  return updated;
}

bool LiveBindingTable::valueForTarget(const std::string& target, float& out) const {
  // targetValues_ holds the binding's DefaultOutputValue from addBinding (TiXL _isDefaultValue path,
  // MidiInput.cs:201) until the first matching event overwrites it with the damped value. A name with
  // no binding at all is absent → false.
  auto it = targetValues_.find(target);
  if (it == targetValues_.end()) return false;
  out = it->second;
  return true;
}

}  // namespace sw
