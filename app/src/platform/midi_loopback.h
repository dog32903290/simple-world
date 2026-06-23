// platform/midi_loopback — CoreMIDI virtual-port loopback + raw MIDI → value decode.
//
// L5 (IO/硬體 lane) machine-verifiable half: the MIDI receive+decode path proven by an in-process
// CoreMIDI loopback (virtual source → our input port → read callback). No physical controller, no
// TCC prompt — a virtual source is created inside the app's own MIDIClient. The REAL controller
// (柏為 plugs in a knob box and performs) is device residue: it reuses this exact decode path; only
// the event origin changes from our virtual source to a hardware source.
//
// platform leaf: owns the CoreMIDI client/source/input-port behind a C++ pimpl, so callers include
// no CoreMIDI headers and no runtime headers. Each decoded event is handed to a registered callback
// with the value already extracted the way TiXL's MidiInput.ProcessParsedMidiEvent does it (NoteOn→
// velocity, NoteOff→0, CC→data2, PitchWheel→14-bit). The value-→-graph-parameter mapping lives ABOVE
// platform (app hook, future L6) — this stays a pure platform leaf with ZERO runtime dependency.
//
// TiXL uses NAudio (Windows) for the device layer + ProcessParsedMidiEvent for the decode. NAudio is
// not portable; we swap in CoreMIDI for the transport and re-implement the decode 1:1. Ground-truth
// file:line in midi_loopback.mm.
#pragma once

namespace sw {

// Decoded MIDI event type — mirrors TiXL MidiEventTypes used in MidiInput.ProcessParsedMidiEvent.
enum class MidiEventKind {
  Note,           // NoteOn / NoteOff (NoteOff forced to value 0, per TiXL)
  ControllerChange,
  Other,          // PitchWheel / AfterTouch / PatchChange (ControllerId = 10000 + cmd)
};

class MidiLoopbackDevice {
 public:
  MidiLoopbackDevice();
  ~MidiLoopbackDevice();
  MidiLoopbackDevice(const MidiLoopbackDevice&) = delete;
  MidiLoopbackDevice& operator=(const MidiLoopbackDevice&) = delete;

  // Per-event callback, invoked on the CoreMIDI READ THREAD for each decoded MIDI event. Fields
  // mirror TiXL MidiSignal: `kind`/`channel`/`controllerId`/`controllerValue`. channel is 1-based
  // (1–16), matching NAudio MidiEvent.Channel. For NoteOn, controllerId=note number and
  // controllerValue=velocity (0 when velocity is 0 — stays NoteOn, not remapped to NoteOff);
  // NoteOff forces controllerValue=0; CC sets controllerId=controller and controllerValue=data2;
  // PitchWheel sets controllerValue=14-bit bend.
  // Set BEFORE startListening(): the read thread reads it once on start.
  using EventCallback = void (*)(void* user, MidiEventKind kind, int channel,
                                 int controllerId, int controllerValue);

  // Create the CoreMIDI client + a virtual source named "SW Loopback" + an input port connected to
  // that source, and start listening. Returns false on immediate failure (CoreMIDI unavailable) —
  // non-fatal, the caller may skip the test.
  bool startListening(EventCallback cb, void* user);
  void stopListening();

  // For loopback testing: inject a raw 3-byte MIDI message (status, data1, data2) into our virtual
  // source. status high nibble = command (0x90 NoteOn, 0x80 NoteOff, 0xB0 CC, 0xE0 PitchWheel),
  // low nibble = channel. It round-trips: virtual source → input port → read callback → decode → cb.
  bool sendRawMidi(int status, int data1, int data2);

  bool isListening() const;

 private:
  struct Impl;
  Impl* impl_;
};

// Mirror of TiXL MidiInput.ProcessParsedMidiEvent (Operators/Lib/io/midi/MidiInput.cs:296-380).
// Decodes a raw 3-byte MIDI message into (kind, channel, controllerId, controllerValue). Returns
// false for status bytes TiXL maps to no signal (e.g. a CC inside an unused range is still decoded
// here; this only rejects non-channel/system bytes < 0x80). Exposed for the selftest to assert the
// decode table directly without going through the socket/CoreMIDI transport.
bool midiDecodeRaw(int status, int data1, int data2, MidiEventKind& kind, int& channel,
                   int& controllerId, int& controllerValue);

}  // namespace sw
