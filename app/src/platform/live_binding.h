// platform/live_binding — live OSC-address / MIDI-CC → named target, with TiXL-faithful range remap.
//
// L5 (IO/硬體 lane) binding layer. The platform OSC/MIDI loopback leaves (osc_loopback / midi_loopback)
// decode a wire/transport event into a value the way TiXL does (OscConnectionManager.TryGetFloat... /
// MidiInput.ProcessParsedMidiEvent). THIS leaf is the next stage: it owns a data-driven binding TABLE
// that, given a decoded event, decides which binding(s) it matches (address / channel+controller +
// event-type filter), applies the TiXL remap (MathUtils.Remap), and stores the result under a named
// target. A graph node input can then READ that target's current value — the value path is fully
// loopback-verifiable WITHOUT touching the cook core or the op registrar (that cook-side wire is a
// future L4/cook step).
//
// platform leaf: pure C++ (std::string / std::vector / std::unordered_map), ZERO runtime / app / ui
// includes — mirrors the inversion osc_loopback / midi_loopback already use. The op-registrar / cook
// wiring that makes a node actually pull from a target lives ABOVE this (future L4); this batch ships
// the binding + its read accessor, machine-verified by the extended L5 loopback golden.
//
// Ground truth (external/tixl, read-only):
//   MidiInput.cs:208-210  remap: MathUtils.Remap(controllerValue, 0, 127, OutputRange.X, OutputRange.Y).
//   MidiInput.cs:401-414  match predicate: channel (<0 = any) AND controller (<0 = any) AND eventType.
//   MathUtils.cs:368-373  Remap(v,inMin,inMax,outMin,outMax) = (v-inMin)/(inMax-inMin)*(outMax-outMin)+outMin.
//   OscInput.cs:241-242   OSC address match = msg.Address.StartsWith(_address); OSC has NO built-in
//                         remap (value flows raw) — so the faithful OSC default is identity in→out.
// Named fork: TiXL keeps MIDI remap and OSC pass-through in two separate operators. We UNIFY them into
// one binding row whose [inMin,inMax]→[outMin,outMax] reproduces both: MIDI uses inRange=[0,127], OSC
// uses inRange=[0,1] (identity when outRange=[0,1]). Same MathUtils.Remap, no behavior change vs TiXL.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace sw {

// TiXL MathUtils.Remap (Core/Utils/MathUtils.cs:368-373). NO clamp (the MIDI op uses the un-clamped
// Remap, not RemapAndClamp). Degenerate inMin==inMax yields the C# result (factor → ±inf/NaN); callers
// that want identity pass inMin=0,inMax=1.
float remapLinear(float value, float inMin, float inMax, float outMin, float outMax);

// TiXL MathUtils.Lerp (Core/Utils/MathUtils.cs:305-308): Lerp(a,b,t) = a + (b - a) * t. Exposed so the
// golden can assert the damping closed-form directly. MidiInput.cs:218 calls Lerp(currentValue,
// _dampedOutputValue, damping) — i.e. a = new value, b = previous damped, t = damping; so damping=0
// returns the raw new value (no smoothing) and damping=1 freezes at the previous damped value.
float lerpLinear(float a, float b, float t);

// Which transport a binding listens to. Mirrors TiXL's split (separate MidiInput / OscInput ops),
// collapsed into one table.
enum class LiveSourceKind { Osc, Midi };

// The MIDI event filter, mirroring MidiInput's trained filter (MidiInput.cs:401-414). A negative value
// means "match any" exactly like TiXL's `_trainedChannel < 0` / `_trainedControllerId < 0`. The kind
// filter mirrors EventType (All / ControllerChange / Note / Other); Any = match all.
enum class MidiMatchKind { Any, ControllerChange, Note, Other };

// One binding row: a live source filter + a remap + a named target. Data-driven — adding a binding is
// adding one of these to the table, not writing code.
struct LiveBinding {
  LiveSourceKind source = LiveSourceKind::Osc;
  std::string    target;        // the named value a graph node input will read

  // --- OSC filter (source == Osc): TiXL OscInput matches msg.Address.StartsWith(address). ---
  std::string    oscAddress;

  // --- MIDI filter (source == Midi): TiXL MidiInput trained channel / controller / event type. ---
  MidiMatchKind  midiKind    = MidiMatchKind::Any;
  int            midiChannel = -1;   // 1-based (1..16); <0 = any  (TiXL _trainedChannel)
  int            midiControl = -1;   // controller id / note number; <0 = any (TiXL _trainedControllerId)

  // --- Remap [inMin,inMax] → [outMin,outMax], TiXL MathUtils.Remap. MIDI default = [0,127]→[0,1]
  //     (the exact MidiInput remap); OSC default = [0,1]→[0,1] (identity, since OscInput has no remap).
  float inMin  = 0.0f;
  float inMax  = 127.0f;
  float outMin = 0.0f;
  float outMax = 1.0f;

  // --- Damping (TiXL MidiInput.cs:218 + Damping input, line 493). Per-update exponential smoothing
  //     toward the new remapped value: damped = Lerp(newRemapped, dampedPrev, damping). damping=0 ⇒
  //     raw remap (no smoothing, the previous behavior). damping in (0,1) lags toward the new value;
  //     damping=1 freezes. TiXL's _dampedOutputValue starts at 0 and is NOT seeded to the first event
  //     (unlike the math Damp op), so the first event with damping d yields newRemapped*(1-d). ---
  float damping = 0.0f;

  // --- DefaultOutputValue (TiXL MidiInput.cs:201 + DefaultOutputValue input, line 490). The value
  //     emitted BEFORE any matching event has arrived (while TiXL's _isDefaultValue is true). Once the
  //     first matching event fires, _isDefaultValue clears (MidiInput.cs:171) and the damped value
  //     takes over. ---
  float defaultOutputValue = 0.0f;
};

// The binding table. Owns the rows + the current remapped value per target. A graph node input reads a
// target via valueForTarget(). Fed by ingestOsc / ingestMidi, which take EXACTLY the values the
// osc_loopback / midi_loopback callbacks already produce (so the real device half reuses this verbatim).
//
// Thread note: the loopback callbacks fire on a receive/read thread. This table is intended to be fed
// from one ingest source at a time in the selftest (single-threaded round-trip). A future app-side
// owner that feeds it from the live receive thread would add its own lock; kept out here to stay a pure
// data leaf (mirrors how audio_capture hands raw blocks up for the app to own).
class LiveBindingTable {
 public:
  // Register a binding. Returns the row index (handy for tests / future UI).
  int addBinding(const LiveBinding& b);
  void clear();
  int  bindingCount() const { return int(bindings_.size()); }

  // Ingest a decoded OSC arg (address + value already coerced to float by oscCoerceToFloat). Applies to
  // every binding whose source==Osc and whose oscAddress is a StartsWith-prefix of `address` (TiXL
  // OscInput.cs:241). Returns the number of bindings updated (0 = no match → no target changes).
  int ingestOsc(const std::string& address, float value);

  // Ingest a decoded MIDI event (the fields midi_loopback's callback produces: kind/channel/
  // controllerId/controllerValue). Applies to every binding whose source==Midi and whose
  // kind/channel/control filter matches (TiXL MidiInput.cs:401-414; <0 = any). The raw value remapped
  // is controllerValue (TiXL remaps _currentControllerValue, 0..127). Returns bindings updated.
  int ingestMidi(MidiMatchKind kind, int channel, int controllerId, int controllerValue);

  // Read accessor — the value a graph node input pulls. Returns the current value if the target is
  // bound: the damped/remapped value once a matching event has fired, OR the binding's
  // DefaultOutputValue before any event (TiXL MidiInput.cs:201, _isDefaultValue path). Returns false
  // (and leaves `out` untouched) only if NO binding targets this name at all.
  bool valueForTarget(const std::string& target, float& out) const;

  // Does this MIDI event match this binding's filter? Exposed for the golden to assert the predicate
  // directly (TiXL MidiInput.cs:401-414), independent of the transport.
  static bool midiMatches(const LiveBinding& b, MidiMatchKind kind, int channel, int controllerId);
  // Does this OSC address match this binding's filter (StartsWith, TiXL OscInput.cs:241)?
  static bool oscMatches(const LiveBinding& b, const std::string& address);

 private:
  // Per-binding runtime state, indexed 1:1 with bindings_. Mirrors MidiInput's instance fields
  // _dampedOutputValue (line 473) and _isDefaultValue (line 459). dampedValue starts at 0 (TiXL does
  // NOT seed it to the first event); hasReceived=false means _isDefaultValue is still true.
  struct BindingState {
    float dampedValue = 0.0f;  // TiXL _dampedOutputValue (starts at 0)
    bool  hasReceived = false; // !_isDefaultValue once a matching event has fired
  };

  // Shared ingest core: apply remap + damping for binding `i` against `rawValue`, write the result into
  // the target. Used by both ingestOsc and ingestMidi so the damping path is identical for both.
  void applyToBinding(int i, float rawValue);

  std::vector<LiveBinding> bindings_;
  std::vector<BindingState> states_;
  std::unordered_map<std::string, float> targetValues_;
};

}  // namespace sw
