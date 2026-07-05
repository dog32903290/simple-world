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

}  // namespace
}  // namespace sw
