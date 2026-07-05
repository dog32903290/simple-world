// runtime/node_registry_math_io — self-registering NodeSpec leaf for the io/midi + io/osc operator
// family (device-input + device-output nodes). Split like every other node_registry_math_* leaf
// (ARCHITECTURE rule 7). These nodes are DEVICE-driven: they have no pure evaluate() (their value
// comes from the runtime IoDeviceBus / their effect is a send side-effect + per-instance memory) →
// evaluate==nullptr, cooked once per frame by frame_cook's io seam (cookMidiInputNodes /
// cookOscInputNodes / the *Output cooks), exactly like AudioReaction/DetectBpm write extOut.
//
// TiXL authority per spec cited inline (Operators/Lib/io/midi/*.cs, Operators/Lib/io/osc/*.cs).
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

// TiXL MidiInput (Lib/io/midi/MidiInput.cs) — CC/note → damped float. STATEFUL (device bus + per-node
// memory), evaluate==nullptr; cooked by cookMidiInputNodes (io_node_cook.cpp) which writes Result →
// extOut[0], WasHit → extOut[1]. OUTPUTS FIRST → output-port index == extOut index (the readback
// contract). Float inputs in TiXL [Input] decl order MINUS the device/teach/persist plumbing the node
// value-path does not use: Device(string device-name — the shared-transport fork drops per-device
// selection), TeachTrigger (learn lives in app/midi_bind, not the node), LastKnownControllerValue
// (editor persistence), PrintLogMessages (telemetry). ControlRange kept (drives the deferred Range
// output's window; carried but the Range LIST output is deferred — fork below). .t3 DEFAULTS
// (MidiInput.t3): OutputRange=(0,1), DefaultOutputValue=0, Damping=0.94, Channel=1, Control=48,
// EventType=0(All), ControlRange=(0,0).
// FORK fork-midiinput-range-output-deferred: Range (List<float> _valuesForControlRange) is a LIST
//   output → deferred to seam/dict-currency; Result + WasHit ship now (named, not invented).
// FORK fork-midiinput-vec2-as-2-floats: OutputRange/ControlRange (Vector2/Int2) → 2 Float ports each.
static const MathOp _reg_MidiInput{
    {"MidiInput", "MidiInput",
     {{"Result", "Result", "Float", false},
      {"WasHit", "WasHit", "Float", false},
      {"OutputRange.x", "OutputRange",   "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Vec, {}, false, 2},
      {"OutputRange.y", "OutputRange.y", "Float", true, 1.0f, -10000.0f, 10000.0f, Widget::Vec, {}, false, 1},
      {"DefaultOutputValue", "DefaultOutputValue", "Float", true, 0.0f, -10000.0f, 10000.0f, Widget::Slider},
      {"Damping", "Damping", "Float", true, 0.94f, 0.0f, 1.0f, Widget::Slider},
      {"Channel", "Channel", "Float", true, 1.0f, -1.0f, 16.0f, Widget::Slider},
      {"Control", "Control", "Float", true, 48.0f, -1.0f, 127.0f, Widget::Slider},
      {"EventType", "EventType", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"All", "Notes", "ControllerChanges", "MidiTime", "MidiEvent"}},
      {"ControlRange.x", "ControlRange",   "Float", true, 0.0f, 0.0f, 128.0f, Widget::Vec, {}, false, 2},
      {"ControlRange.y", "ControlRange.y", "Float", true, 0.0f, 0.0f, 128.0f, Widget::Vec, {}, false, 1}},
     nullptr,
     "io.midi"}
};

// TiXL OscInput (Lib/io/osc/OscInput.cs) — OSC address → values. STATEFUL (device bus), evaluate==
// nullptr; cooked by cookOscInputNodes (io_node_cook.cpp). PARTIAL: only WasTrigger (the bool edge)
// ships on the value rail; Contents(Dict<float>) + Values(List<float>) outputs are DEFERRED to
// seam/dict-currency (fork below). OUTPUT FIRST → extOut[0]=WasTrigger. Address is a String input
// (StartsWith prefix filter). Float/bool inputs carried in TiXL decl order MINUS the editor-only /
// dict-shaping plumbing (Port — the shared UDP socket is app-owned, not per-node; UseKeyValuePairs /
// GroupKeysAsPaths / FilterKeys / SearchFilterKey / SearchPattern — these shape the DEFERRED dict
// output only; IsListening / PrintLogMessages — transport/telemetry). .t3 DEFAULT: Address="".
// FORK fork-oscinput-dict-list-output-deferred: Contents/Values (Dict/List) deferred; WasTrigger ships.
static const MathOp _reg_OscInput{
    {"OscInput", "OscInput",
     {{"WasTrigger", "WasTrigger", "Float", false},
      {"Address", "Address", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
     nullptr,
     "io.osc"}
};

// TiXL MidiControlOutput (Lib/io/midi/MidiControlOutput.cs) — CC / ChannelPressure SENDER. STATEFUL
// (per-node trigger-edge latch), evaluate==nullptr; cooked by cookMidiControlOutputNodes (io_node_cook.cpp)
// which emits the short message to the device out bus + echoes the sent value onto extOut[0]. Result is
// a Command in TiXL (no value) → the echo is the golden probe (fork-midioutput-echo-output). Inputs in
// TiXL [Input] decl order MINUS Device (shared-transport fork — the app owns one destination, not a
// per-name select). .t3 DEFAULTS: SendMode=0(SendContinuously), TriggerSend=false, CCorPressure=0(CC),
// ChannelNumber=1, ControllerNumber=1, Value=0, UseValueFloat=false, ValueFloat=0.
static const MathOp _reg_MidiControlOutput{
    {"MidiControlOutput", "MidiControlOutput",
     {{"Result", "Result", "Float", false},   // echo of the sent value (extOut[0], golden probe)
      {"SendMode", "SendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"SendContinuously", "SendWhenTriggered"}},
      {"TriggerSend", "TriggerSend", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"CCorPressure", "CCorPressure", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"ContinuosController_CC", "ChannelPressure"}},
      {"ChannelNumber", "ChannelNumber", "Float", true, 1.0f, 1.0f, 16.0f, Widget::Slider},
      {"ControllerNumber", "ControllerNumber", "Float", true, 1.0f, 0.0f, 127.0f, Widget::Slider},
      {"Value", "Value", "Float", true, 0.0f, 0.0f, 127.0f, Widget::Slider},
      {"UseValueFloat", "UseValueFloat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"ValueFloat", "ValueFloat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider}},
     nullptr,
     "io.midi"}
};

// TiXL MidiOutput (Lib/io/midi/MidiOutput.cs) — note/CC/sequence SENDER. cook: cookMidiOutputNodes.
// SendModes: 0 Notes_FixedDuration,1 Note_WhileTriggered,2 ControllerChange,3 StartSequence,4 Stop,
// 5 Continue,6 TempoEvent. .t3: SendMode=0,TriggerSend=true,ChannelNumber=1,NoteOrController=0,
// Velocity=-1,Velocity127=0,DurationInSecs=0.1. FORK fork-midioutput-tempo-meta-dropped (see cook).
static const MathOp _reg_MidiOutput{
    {"MidiOutput", "MidiOutput",
     {{"Result", "Result", "Float", false},
      {"TriggerSend", "TriggerSend", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
      {"SendMode", "SendMode", "Float", true, 0.0f, 0.0f, 6.0f, Widget::Enum,
       {"Notes_FixedDuration", "Note_WhileTriggered", "ControllerChange", "StartSequence", "StopSequence", "ContinueSequence", "TempoEvent"}},
      {"ChannelNumber", "ChannelNumber", "Float", true, 1.0f, 1.0f, 16.0f, Widget::Slider},
      {"NoteOrController", "NoteOrController", "Float", true, 0.0f, 0.0f, 127.0f, Widget::Slider},
      {"Velocity", "Velocity", "Float", true, -1.0f, -1.0f, 1.0f, Widget::Slider},
      {"Velocity127", "Velocity127", "Float", true, 0.0f, -1.0f, 127.0f, Widget::Slider},
      {"DurationInSecs", "DurationInSecs", "Float", true, 0.1f, 0.0f, 100.0f, Widget::Slider}},
     nullptr, "io.midi"}
};

// TiXL MidiNoteOutput (Lib/io/midi/MidiNoteOutput.cs) — note SENDER. cook: cookMidiNoteOutputNodes.
// SendModes: 0 Note_FixedDuration,1 Note_WhileTriggered. .t3: SendMode=0,ChannelNumber=1,NoteNumber=64,
// Velocity=100,UseVelocityFloat=false,VelocityFloat=0.75,DurationInSecs=0.5,TriggerSend=false.
static const MathOp _reg_MidiNoteOutput{
    {"MidiNoteOutput", "MidiNoteOutput",
     {{"Result", "Result", "Float", false},
      {"TriggerSend", "TriggerSend", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"SendMode", "SendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Note_FixedDuration", "Note_WhileTriggered"}},
      {"ChannelNumber", "ChannelNumber", "Float", true, 1.0f, 1.0f, 16.0f, Widget::Slider},
      {"NoteNumber", "NoteNumber", "Float", true, 64.0f, 0.0f, 127.0f, Widget::Slider},
      {"Velocity", "Velocity", "Float", true, 100.0f, 0.0f, 127.0f, Widget::Slider},
      {"UseVelocityFloat", "UseVelocityFloat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"VelocityFloat", "VelocityFloat", "Float", true, 0.75f, 0.0f, 1.0f, Widget::Slider},
      {"DurationInSecs", "DurationInSecs", "Float", true, 0.5f, 0.0f, 100.0f, Widget::Slider}},
     nullptr, "io.midi"}
};

// TiXL MidiPitchbendOutput (Lib/io/midi/MidiPitchbendOutput.cs). SendModes: 0 SendContinuously,
// 1 SendWhenTriggered. .t3: SendMode=0,TriggerSend=false,ChannelNumber=1,Pitch=0,UsePitchFloat=false,
// PitchFloat=0.
static const MathOp _reg_MidiPitchbendOutput{
    {"MidiPitchbendOutput", "MidiPitchbendOutput",
     {{"Result", "Result", "Float", false},
      {"SendMode", "SendMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"SendContinuously", "SendWhenTriggered"}},
      {"TriggerSend", "TriggerSend", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"ChannelNumber", "ChannelNumber", "Float", true, 1.0f, 1.0f, 16.0f, Widget::Slider},
      {"Pitch", "Pitch", "Float", true, 0.0f, -8192.0f, 8191.0f, Widget::Slider},
      {"UsePitchFloat", "UsePitchFloat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"PitchFloat", "PitchFloat", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider}},
     nullptr, "io.midi"}
};

// TiXL MidiTriggerOutput (Lib/io/midi/MidiTriggerOutput.cs) — 5 rising-edge triggers → PC/Start/Stop/
// Continue/Tempo. .t3: all triggers false, ChannelNumber=1, ProgramChangeNumber=0.
static const MathOp _reg_MidiTriggerOutput{
    {"MidiTriggerOutput", "MidiTriggerOutput",
     {{"Result", "Result", "Float", false},
      {"ChannelNumber", "ChannelNumber", "Float", true, 1.0f, 1.0f, 16.0f, Widget::Slider},
      {"TriggerProgramChange", "TriggerProgramChange", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"ProgramChangeNumber", "ProgramChangeNumber", "Float", true, 0.0f, 0.0f, 127.0f, Widget::Slider},
      {"TriggerStart", "TriggerStart", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"TriggerStop", "TriggerStop", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"TriggerContinue", "TriggerContinue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"TriggerTempoEvent", "TriggerTempoEvent", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     nullptr, "io.midi"}
};

// TiXL MidiSysexOutput (Lib/io/midi/MidiSysexOutput.cs) — SysEx SENDER on a rising TriggerSend edge.
// SysexString = space-separated hex bytes. .t3: TriggerSend=false, SysexString="".
static const MathOp _reg_MidiSysexOutput{
    {"MidiSysexOutput", "MidiSysexOutput",
     {{"Result", "Result", "Float", false},
      {"TriggerSend", "TriggerSend", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"SysexString", "SysexString", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
     nullptr, "io.midi"}
};

// TiXL OscOutput (Lib/io/osc/OscOutput.cs) — OSC SENDER. PARTIAL: single-value path only (Values/
// IntValues/Strings MultiInput LISTS deferred to seam/dict-currency, see cookOscOutputNodes). shouldSend
// = SendTrigger && (changed || !OnlySendChanges) (cs:273). .t3: Address="/tooll3", Port=9000,
// IpAddress="127.0.0.1", SendTrigger=true, OnlySendChanges=true, Values=0. FORK fork-oscoutput-single-
// value: one scalar Value replaces the MultiInput Values list until the list currency lands.
static const MathOp _reg_OscOutput{
    {"OscOutput", "OscOutput",
     {{"Result", "Result", "Float", false},
      {"Address", "Address", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "/tooll3"},
      {"Value", "Value", "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"SendTrigger", "SendTrigger", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
      {"OnlySendChanges", "OnlySendChanges", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},
     nullptr, "io.osc"}
};

}  // namespace
}  // namespace sw
