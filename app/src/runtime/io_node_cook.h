// runtime/io_node_cook — the per-frame stateful cook for the MIDI/OSC operator NODES (MidiInput /
// OscInput / the MIDI/OSC *Output senders). The io-family sibling of audio_reaction.cpp /
// detect_bpm.cpp: these nodes have NO pure evaluate() (their value comes from the device bus + they
// keep per-instance memory across frames — the damped value, the last controller value), so
// frame_cook cooks them once per frame from the IoDeviceBus and writes the outputs onto
// ResidentNode::extOut; evalResidentFloat returns extOut[outputPortIndex] via the generic no-evaluate
// path (resident_eval_graph.cpp), exactly like AudioReaction.
//
// TiXL authority (per-node file:line cited at each cook fn below):
//   MidiInput.cs   — Result = Remap(controllerValue,0,127,OutputRange) then Lerp-damped; WasHit.
//   OscInput.cs    — WasTrigger (the non-dict output; Contents/Values Dict/List deferred to
//                    seam/dict-currency).
//   Midi*Output.cs / OscOutput.cs — edge-triggered SEND side-effects (echo output for the golden).
//
// runtime leaf: pure computation, reads the runtime IoDeviceBus, no hardware / no UI / no upward dep.
#pragma once
#include <map>
#include <string>

namespace sw {

struct ResidentEvalGraph;   // resident_eval_graph.h
struct Transport;           // transport.h

// Per-instance memory across frames for a MidiInput node (TiXL private fields, MidiInput.cs:459-475).
struct MidiInputState {
  float dampedOutputValue = 0.0f;   // TiXL _dampedOutputValue (starts at 0, NOT seeded to first event)
  float currentControllerValue = 0.0f;  // TiXL _currentControllerValue
  int   currentControllerId = 0;    // TiXL _currentControllerId
  bool  isDefaultValue = true;      // TiXL _isDefaultValue (true until first matching event)
};

// Cook every MidiInput node once this frame: apply each node's channel/control/eventType filter over
// the frame's IoDeviceBus MIDI signals (TiXL MidiInput.cs:401-414 match predicate), remap the matched
// controller value (cs:208 Remap 0..127 → OutputRange), damp it (cs:218 Lerp), and write Result →
// extOut[0], WasHit → extOut[1]. Per-node state keyed by resident path (like AudioReactionState).
void cookMidiInputNodes(ResidentEvalGraph& g, std::map<std::string, MidiInputState>& state);

// Per-instance memory for an OscInput node — only the WasTrigger edge (the non-dict output).
struct OscInputState {
  bool wasTrigger = false;   // TiXL _wasTrigger (true the frame a matching message arrived)
};

// Cook every OscInput node once this frame: for each frame OSC arg whose address StartsWith the node's
// Address filter (TiXL OscInput.cs:241), set WasTrigger → extOut[<WasTrigger idx>]. Contents/Values
// (Dict<float>/List<float>) are DEFERRED to seam/dict-currency (see io_node_cook.cpp TODO).
void cookOscInputNodes(ResidentEvalGraph& g, std::map<std::string, OscInputState>& state);

// Per-instance memory for a MIDI/OSC OUTPUT node — the trigger-edge latch (TiXL private `_triggered`,
// e.g. MidiControlOutput.cs:129). SendWhenTriggered fires only on the rising edge; SendContinuously
// every frame the trigger is held.
struct MidiOutputState {
  bool triggered = false;   // TiXL _triggered (the previous frame's TriggerSend value)
};

// Cook every MidiControlOutput node once this frame: on the send condition (rising edge for
// SendWhenTriggered, every frame for SendContinuously), build the exact CC / ChannelPressure short
// message TiXL builds (MidiControlOutput.cs:84/88/100/104) and emit it to the device out bus. State
// keyed by resident path. (The frozen output-node pattern the *Output siblings follow.)
void cookMidiControlOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state);

// Wall-clock the duration/tempo output modes need (TiXL Playback.RunTimeInSecs / Playback.Bpm). Most
// output cooks ignore it (edge-triggered); only Note_FixedDuration + TempoEvent read it. frame_cook
// fills it from the transport; the golden passes an explicit clock to drive the duration modes.
struct IoOutClock {
  double runTimeSecs = 0.0;   // TiXL Playback.RunTimeInSecs (process-lifetime wall accumulator)
  double bpm = 120.0;         // TiXL Playback.Current.Bpm (TempoEvent)
};

// The 5 remaining MIDI/OSC output cooks (io_output_cook.cpp), same shape as cookMidiControlOutput:
// resolve inputs, edge/continuous send condition, build the TiXL short message, emit to the out bus,
// echo a golden probe onto extOut[0]. Per-node state keyed by resident path.
struct MidiNoteOutputState { bool triggered = false; double lastNoteOnMs = -1.0; int offStatus = 0, offNote = 0; bool haveOff = false; };
struct MidiTriggerOutputState { bool pc = false, start = false, stop = false, cont = false, tempo = false; };
void cookMidiOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiNoteOutputState>& state, const IoOutClock& clk);
void cookMidiNoteOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiNoteOutputState>& state, const IoOutClock& clk);
void cookMidiPitchbendOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state);
void cookMidiTriggerOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiTriggerOutputState>& state, const IoOutClock& clk);
void cookMidiSysexOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state);
void cookOscOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state);

// The single frame_cook entry point: cook every io device node (MidiInput + OscInput) and CLEAR the
// device bus afterward (so next frame starts empty). Owns the per-instance state maps (function-local
// statics keyed by resident path) so frame_cook stays a one-liner. The golden drives the per-family
// cookMidiInputNodes/cookOscInputNodes directly (its own state maps), not this wrapper.
void cookIoDeviceNodes(ResidentEvalGraph& g);

// ── Golden TEETH hook (--selftest-io-nodes). 0 = production; 1 = DROP the filter (accept ANY signal →
// the wrong-channel-rejected assertion goes RED); 2 = DROP the remap (Result = raw controllerValue →
// the closed-form Remap assertion goes RED). Sticky module switch; the golden restores 0. Mirrors
// setAnimValueBug's convention.
void setIoNodeBug(int mode);
int  ioNodeBug();

}  // namespace sw
