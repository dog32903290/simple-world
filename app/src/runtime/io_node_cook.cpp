// runtime/io_node_cook — per-frame cook for the MIDI/OSC operator nodes. See io_node_cook.h.
//
// TiXL authority: Operators/Lib/io/midi/MidiInput.cs, Operators/Lib/io/osc/OscInput.cs (read-only).
#include "runtime/io_node_cook.h"

#include <cmath>
#include <string>

#include <map>

#include "runtime/io_device_bus.h"        // IoDeviceBus / MidiBusSignal / OscBusArg (the device seam)
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / ResidentNode / resolveResidentFloatInputs

namespace sw {

namespace {
int g_ioNodeBug = 0;

// TiXL MathUtils.Remap (Core/Utils/MathUtils.cs:368-373) — the un-clamped remap MidiInput.cs:208 uses.
// Inlined here (not the platform/live_binding twin) so this stays a pure runtime leaf. Degenerate
// inMin==inMax yields the C# ±inf/NaN; callers pass 0..127 so it never degenerates in practice.
float remapMidi(float value, float inMin, float inMax, float outMin, float outMax) {
  return (value - inMin) / (inMax - inMin) * (outMax - outMin) + outMin;
}
// TiXL MathUtils.Lerp (Core/Utils/MathUtils.cs:305-308): Lerp(a,b,t) = a + (b-a)*t. MidiInput.cs:218
// calls Lerp(currentValue, _dampedOutputValue, damping) → damping=0 returns the raw new value.
float lerpMidi(float a, float b, float t) { return a + (b - a) * t; }

// TiXL MidiInput match predicate (MidiInput.cs:401-414). _trainedChannel<0 / _trainedControllerId<0 =
// "match any". EventType All(0) matches every kind; otherwise the signal's kind must equal it. The
// signal kind numbering matches TiXL MidiEventTypes projected onto the bus: bus kind 0=Note,
// 1=ControllerChange, 2=Other; the node's EventType enum is [All, Notes, ControllerChanges, MidiTime,
// MidiEvent] → map EventType to the bus kind it selects (Notes→0, ControllerChanges→1, MidiEvent→2;
// MidiTime is the timing-clock path, not carried on this value bus → never matches a note/cc/other).
bool midiSignalMatches(const MidiBusSignal& s, int trainedChannel, int trainedControl, int eventType) {
  if (g_ioNodeBug == 1) return true;  // TEETH mode 1: drop the filter (accept anything)
  const bool matchesChannel = trainedChannel < 0 || s.channel == trainedChannel;
  const bool matchesControl = trainedControl < 0 || s.controllerId == trainedControl;
  bool matchesEventType;
  switch (eventType) {
    case 0:  matchesEventType = true; break;              // All
    case 1:  matchesEventType = (s.kind == 0); break;     // Notes
    case 2:  matchesEventType = (s.kind == 1); break;     // ControllerChanges
    case 3:  matchesEventType = false; break;             // MidiTime (not on this value bus)
    case 4:  matchesEventType = (s.kind == 2); break;     // MidiEvent (Other: pitchwheel/aftertouch)
    default: matchesEventType = true; break;
  }
  return matchesChannel && matchesControl && matchesEventType;
}
}  // namespace

void setIoNodeBug(int mode) { g_ioNodeBug = mode; }
int  ioNodeBug() { return g_ioNodeBug; }

// ── MidiInput ────────────────────────────────────────────────────────────────────────────────────
// Faithful port of MidiInput.cs Update (the device-driven value path; the teach/learn flow lives in
// the app binding pipe, app/midi_bind.cpp — a node reads its CONFIGURED filter, not a learned one).
//   match:  MidiInput.cs:401-414 (channel/control/eventType, <0 = any).
//   value:  MidiInput.cs:208  currentValue = Remap(_currentControllerValue, 0,127, OutRange.X, OutRange.Y).
//   damp:   MidiInput.cs:218  _dampedOutputValue = Lerp(currentValue, _dampedOutputValue, damping).
//   wasHit: MidiInput.cs:153-168  hasValueChanged && ControllerValue>0.
//   default:MidiInput.cs:199-206  while _isDefaultValue: Result = DefaultOutputValue (until first match).
// Port order (see node_registry_math_io.cpp): outputs FIRST → extOut idx: [0]=Result, [1]=WasHit.
// Float inputs (resolved via P): OutputRange.x/.y, DefaultOutputValue, Damping, Channel, Control,
// EventType, ControlRange.x/.y. NAMED FORK fork-midiinput-controlrange-range-output-deferred: the
// Range output (List<float> _valuesForControlRange) is a LIST → deferred to seam/dict-currency.
void cookMidiInputNodes(ResidentEvalGraph& g, std::map<std::string, MidiInputState>& state) {
  ResidentEvalCtx rctx;  // MidiInput reads no clock/automation curves — a bare ctx resolves its consts.
  const IoDeviceBus& bus = ioDeviceBus();
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "MidiInput") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const float outMin  = P["OutputRange.x"];
    const float outMax  = P["OutputRange.y"];
    const float defOut  = P["DefaultOutputValue"];
    const float damping = P["Damping"];
    const int   channel = (int)std::lround(P["Channel"]);
    const int   control = (int)std::lround(P["Control"]);
    const int   evType  = (int)std::lround(P["EventType"]);

    MidiInputState& st = state[rn.path];
    bool wasHit = false;

    // Drain this frame's matching signals (TiXL _lastMatchingSignals loop, MidiInput.cs:124-172).
    for (const MidiBusSignal& s : bus.midi) {
      if (!midiSignalMatches(s, channel, control, evType)) continue;
      const bool hasValueChanged =
          std::fabs(st.currentControllerValue - (float)s.controllerValue) > 0.001f;  // cs:153
      st.currentControllerValue = (float)s.controllerValue;                          // cs:154
      st.currentControllerId = s.controllerId;                                       // cs:155
      if (hasValueChanged && s.controllerValue > 0) wasHit = true;                   // cs:167-168
      st.isDefaultValue = false;                                                     // cs:171
    }

    // While no matching event has ever arrived, emit DefaultOutputValue (cs:199-206).
    if (st.isDefaultValue) {
      rn.extOut[0] = defOut;
      rn.extOut[1] = 0.0f;
      continue;
    }

    // currentValue = Remap(controllerValue, 0..127, outRange) (cs:208). TEETH mode 2 drops the remap.
    const float currentValue =
        (g_ioNodeBug == 2) ? st.currentControllerValue
                           : remapMidi(st.currentControllerValue, 0.0f, 127.0f, outMin, outMax);
    // damped = Lerp(currentValue, dampedPrev, damping) (cs:218). damping=0 → raw currentValue.
    st.dampedOutputValue = lerpMidi(currentValue, st.dampedOutputValue, damping);
    if (!std::isnormal(st.dampedOutputValue) && st.dampedOutputValue != 0.0f)  // cs:231-232
      st.dampedOutputValue = 0.0f;

    rn.extOut[0] = st.dampedOutputValue;    // Result
    rn.extOut[1] = wasHit ? 1.0f : 0.0f;    // WasHit
  }
}

// ── OscInput (PARTIAL — WasTrigger only) ───────────────────────────────────────────────────────────
// Faithful port of OscInput.cs's WasTrigger path (the non-dict output). A matching message this frame
// (address StartsWith the Address filter, OscInput.cs:241) fires WasTrigger true for one frame
// (cs:190-191 AnimatedUpdate: WasTrigger.Value = _wasTrigger; _wasTrigger = false). The primary
// Contents (Dict<float>) + Values (List<float>) outputs are DEFERRED — dict/list currency is a
// separate seam (seam/dict-currency); this node ships WasTrigger + a golden proving the trigger edge,
// and leaves the dict/list ports as a documented TODO (do NOT fake a scalar for them).
// Port order (node_registry_math_io.cpp): [0]=WasTrigger (the only value-rail output shipped).
void cookOscInputNodes(ResidentEvalGraph& g, std::map<std::string, OscInputState>& state) {
  ResidentEvalCtx rctx;
  const IoDeviceBus& bus = ioDeviceBus();
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "OscInput") continue;
    // Address is a String input (StartsWith prefix filter). Empty address = match every message
    // (OscInput.cs:241: string.IsNullOrEmpty(_address) skips the prefix guard).
    std::string addr;
    auto ait = rn.strInputs.find("Address");
    if (ait != rn.strInputs.end()) addr = ait->second;

    bool matched = false;
    for (const OscBusArg& a : bus.osc) {
      const bool addrMatches =
          addr.empty() || (a.address.size() >= addr.size() && a.address.compare(0, addr.size(), addr) == 0);
      if (g_ioNodeBug == 1) matched = true;      // TEETH mode 1: drop the address filter
      else if (addrMatches) matched = true;
    }

    OscInputState& st = state[rn.path];
    st.wasTrigger = matched;
    rn.extOut[0] = matched ? 1.0f : 0.0f;   // WasTrigger
    // TODO(seam/dict-currency): Contents (Dict<float>) + Values (List<float>) outputs — wire the
    // per-address float map + the ordered value list once the dict/list output currency lands.
  }
}

// ── MidiControlOutput ────────────────────────────────────────────────────────────────────────────
// Faithful port of MidiControlOutput.cs Update: on the send condition build a CC (0xB0) or Channel-
// Pressure (0xD0) short message and emit it. TiXL clamps controller 0..127 (cs:35), value 0..127 or
// (float*127) 0..127 (cs:42/46), channel 1..16 (cs:72). SendModes: 0=SendContinuously (every frame),
// 1=SendWhenTriggered (rising edge only, cs:82). DataTypes: 0=CC, 1=ChannelPressure. NAMED FORK
// fork-midioutput-shared-transport: TiXL loops MidiOutsWithDevices matching Device name; sw emits to
// the shared out bus (the app forwards to the real CoreMIDI destination — deferred-hw-verify), so the
// Device-name select drops here (one destination, not per-name). Output Result is a Command in TiXL
// (no value); sw echoes the emitted data2 (value) onto extOut[0] as the golden probe (0 if nothing
// sent this frame). TEETH: mode 1 forces the send condition true (SendWhenTriggered fires without the
// rising edge → the edge-gated golden goes RED); mode 2 drops the value clamp/scale (raw float → RED).
static void cookMidiControlOutput(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "MidiControlOutput") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const int  sendMode  = (int)std::lround(P["SendMode"]);       // 0 continuous, 1 triggered
    const bool trigActive = P["TriggerSend"] > 0.5f;
    const int  ccOrPress = (int)std::lround(P["CCorPressure"]);   // 0 CC, 1 ChannelPressure
    const bool useFloat  = P["UseValueFloat"] > 0.5f;
    int channel    = (int)std::lround(P["ChannelNumber"]); channel = channel < 1 ? 1 : channel > 16 ? 16 : channel;
    int controller = (int)std::lround(P["ControllerNumber"]); controller = controller < 0 ? 0 : controller > 127 ? 127 : controller;
    int intValue;
    if (!useFloat) { intValue = (int)std::lround(P["Value"]); }
    else {
      float vf = P["ValueFloat"]; vf = vf < 0.0f ? 0.0f : vf > 1.0f ? 1.0f : vf;
      intValue = (g_ioNodeBug == 2) ? (int)P["ValueFloat"] : (int)(vf * 127.0f);  // bug: drop clamp+scale
    }
    if (g_ioNodeBug != 2) { intValue = intValue < 0 ? 0 : intValue > 127 ? 127 : intValue; }

    MidiOutputState& st = state[rn.path];
    const bool justActivated = trigActive && !st.triggered;   // rising edge (cs:58-64)
    st.triggered = trigActive;

    // Send condition (cs:79-106): continuous every frame, triggered only on the rising edge. TEETH
    // mode 1: force the send condition (ignore the edge) → the SendWhenTriggered golden bites.
    bool doSend = (sendMode == 0) || (sendMode == 1 && justActivated);
    if (g_ioNodeBug == 1) doSend = (sendMode == 0) || (sendMode == 1 && trigActive);  // edge dropped

    float echo = 0.0f;
    if (doSend) {
      if (ccOrPress == 0) {  // CC: 0xB0 | (channel-1), controller, value
        emitMidiShort(0xB0 | (channel - 1), controller, intValue, 3);
      } else {               // ChannelPressure: 0xD0 | (channel-1), value (2-byte)
        emitMidiShort(0xD0 | (channel - 1), intValue, 0, 2);
      }
      echo = (float)intValue;
    }
    rn.extOut[0] = echo;  // Result echo (golden probe)
  }
}

void cookMidiControlOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state) {
  cookMidiControlOutput(g, state);
}

// frame_cook entry point: cook both io input families + the output families, then clear the bus. State
// maps are function-local statics keyed by resident path (survives projection rebuilds, per-instance),
// so frame_cook adds one call not a state block. DEFERRED-HW-VERIFY: once the real CoreMIDI/UDP
// forwarder is wired app-side, it must drain bus.midiOut/oscOut/sysexOut BEFORE this endIoDeviceFrame
// clear (the send side-effect leg — the physical device). Today nothing forwards them (no device), so
// the clear here is harmless; the golden drives the cooks directly and reads midiOut before its own clear.
void cookIoDeviceNodes(ResidentEvalGraph& g) {
  static std::map<std::string, MidiInputState>  s_midiInState;
  static std::map<std::string, OscInputState>   s_oscInState;
  static std::map<std::string, MidiOutputState> s_ctrlOutState;
  cookMidiInputNodes(g, s_midiInState);
  cookOscInputNodes(g, s_oscInState);
  cookMidiControlOutputNodes(g, s_ctrlOutState);
  endIoDeviceFrame();
}

}  // namespace sw
