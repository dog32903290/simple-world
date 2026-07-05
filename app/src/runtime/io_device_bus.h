// runtime/io_device_bus — the runtime-side per-frame device-value seam for the MIDI/OSC operator
// nodes (MidiInput / OscInput / the *Output senders). The mirror of the audio seam inversion
// (ARCHITECTURE "leaf seam"): the PLATFORM leaves (midi_loopback / osc_loopback) own the CoreMIDI
// client + UDP socket and DECODE each event; the APP owns those transports and PUSHES every decoded
// event into THIS runtime sink (ingestMidiSignal / ingestOscArg); the RUNTIME node cook (frame_cook's
// cookMidiInputNodes / cookOscInputNodes) READS the sink to produce node outputs. runtime NEVER
// includes a platform header — the app is the only writer, exactly like audio_monitor feeds the
// spectrum. This keeps the MIDI/OSC nodes pure-runtime (no hardware include) and machine-verifiable:
// a golden feeds ingestMidiSignal directly (no transport) and asserts the node's cooked output.
//
// TiXL parity note: TiXL's MidiInput/OscInput each own their OWN device registration (MidiConnection-
// Manager.RegisterConsumer / OscConnectionManager.RegisterConsumer) and a lock-guarded queue of the
// signals that matched THIS instance's filter, drained on Update. sw hoists the single shared transport
// up to the app (one virtual-CoreMIDI source + one UDP socket for the whole process — the loopback
// leaves) and lets each node's cook apply ITS OWN filter over the shared last-frame signal list here.
// The per-instance filter+queue-drain semantics are byte-identical; only the transport is shared
// (NAMED FORK fork-io-shared-transport-per-node-filter — faithful in value, one socket not N).
//
// runtime leaf: pure computation (std::vector / std::string), no hardware, no UI, no upward dep.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sw {

// A decoded MIDI event (the exact fields midi_loopback's callback produces / TiXL MidiSignal carries).
// kind mirrors platform MidiEventKind AND TiXL MidiEventTypes ordering used by the match predicate:
//   0 = Note (NoteOn/NoteOff; NoteOff forced value 0), 1 = ControllerChange, 2 = Other (PitchWheel/
//   AfterTouch/PatchChange, controllerId = 10000 + cmd). channel is 1-based (1..16, TiXL convention).
struct MidiBusSignal {
  int kind = 0;
  int channel = 0;        // 1-based
  int controllerId = 0;   // note number / CC id / (10000+cmd) for Other
  int controllerValue = 0;
};

// A decoded OSC argument (address + value already float-coerced by oscCoerceToFloat). One entry PER arg
// (a multi-arg message pushes several with the same address, ascending argIndex) — matches the
// osc_loopback callback contract.
struct OscBusArg {
  std::string address;
  float value = 0.0f;
  int argIndex = 0;
};

// A MIDI short message an output node EMITTED this frame (the exact 3 bytes TiXL builds via
// MidiEvent.GetAsShortMessage(): status = command|channel-1, data1, data2). The output cooks append
// these to the out sink; the golden asserts the bytes; the app forwards them to the real CoreMIDI
// destination (deferred-hw-verify). SysEx (variable-length) rides the `sysex` vector instead.
struct MidiOutMessage {
  int status = 0;   // command nibble | (channel-1): 0x90 NoteOn, 0x80 NoteOff, 0xB0 CC, 0xE0 PitchWheel,
                    // 0xD0 ChannelPressure, 0xC0 ProgramChange, 0xFA Start, 0xFC Stop, 0xFB Continue.
  int data1 = 0;    // note / controller / pitch-lsb / program (0 for system realtime).
  int data2 = 0;    // velocity / cc-value / pitch-msb (0 for 2-byte messages).
  int bytes = 3;    // wire length: 3 (note/cc/pitchwheel), 2 (program/pressure), 1 (system realtime).
};

// An OSC message an output node EMITTED this frame (address + the ordered float args). The app
// forwards it to the real UDP sender (deferred-hw-verify); the golden asserts address + args.
struct OscOutMessage {
  std::string address;
  std::vector<float> args;
};

// The single per-frame device sink (Meyers singleton, one per process). The app's transport callbacks
// APPEND this frame's decoded events as they arrive (async, on the receive thread); the node cooks
// READ the accumulated list once per frame; the app CLEARS it after the cook (endIoDeviceFrame), so
// next frame starts empty. Not thread-safe by itself: the app owns a lock around the transport
// callback + the read/clear (mirrors LiveBindingTable's thread note); the golden drives it single-
// threaded (append → cook → assert), so no clear is needed inside a golden.
struct IoDeviceBus {
  std::vector<MidiBusSignal> midi;  // INPUT: this frame's decoded MIDI events (all filters, un-matched)
  std::vector<OscBusArg>     osc;   // INPUT: this frame's decoded OSC args (all addresses, un-matched)
  std::vector<MidiOutMessage> midiOut;  // OUTPUT: MIDI short messages the output cooks emitted this frame
  std::vector<std::vector<uint8_t>> sysexOut;  // OUTPUT: variable-length SysEx buffers emitted this frame
  std::vector<OscOutMessage> oscOut;    // OUTPUT: OSC messages the output cooks emitted this frame
};

// The process-wide bus. The node cooks read it; the app's ingest hooks append; the app clears it.
IoDeviceBus& ioDeviceBus();

// Output-side emit helpers (called by the output cooks in io_node_cook.cpp; the app drains midiOut/
// oscOut/sysexOut each frame to the real CoreMIDI/UDP senders, then the frame clear wipes them).
void emitMidiShort(int status, int data1, int data2, int bytes);
void emitMidiSysex(const std::vector<uint8_t>& buffer);
void emitOscMessage(const std::string& address, const std::vector<float>& args);

// App-side ingest hooks (called from the loopback callbacks the app installs). Thin appends onto the
// bus — the runtime never calls these, the app does (leaf-seam: app is the only writer).
void ingestMidiSignal(int kind, int channel, int controllerId, int controllerValue);
void ingestOscArg(const std::string& address, float value, int argIndex);

// Clear the accumulated events — BOTH the input events (midi/osc, read by the input cooks) AND the
// output messages (midiOut/oscOut/sysexOut, drained by the app to the real senders). Called once per
// frame AFTER the input cooks read + the app drained the output messages.
void endIoDeviceFrame();

}  // namespace sw
