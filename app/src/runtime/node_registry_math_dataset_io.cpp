// runtime/node_registry_math_dataset_io — self-registering NodeSpec leaf for the io/data + io/midi DATASET
// family: LoadDataClip / SimulateIoData / MidiRecording. These three carry the SwDataSet-currency cores
// that already live (golden) in dataset_ops.cpp (parseDataFile / simulateClipWindow / recordMidiEvent);
// this leaf gives each a GRAPH NODE (Add-menu + findSpec spec) so the op exists on the canvas. Split like
// every node_registry_* leaf (ARCHITECTURE rule 7); each op's TiXL authority is cited inline for the
// census 4th source (`// TiXL authority: <Op>.cs`).
//
// SCOPE — spec-only nodes (evaluate==nullptr), the node-COOK is DEFERRED, and this is a real currency
// wall, not laziness:
//   • The SwDataSet cores are proven closed-form by --selftest-dataset (parse/record/simulate); the node
//     WIRING they still need is a downstream WIRE CURRENCY that does not exist on main yet —
//       - LoadDataClip.Clip and MidiRecording.DataSet output "DataClip"/"DataSet", and NO consumer op
//         reads a DataClip/DataSet wire yet (grep of the spec ports: only Dict/Command/Points/Field/…).
//         Registering the producer node without a consumer is faithful (TiXL has the same producer); the
//         cook that fills the wire waits on the SwDataClip/SwDataSet wire currency seam.
//       - SimulateIoData.Clips INPUT is a DataClip MultiInput (same missing producer wire) and its Execute
//         OUTPUT is a Command (a real currency) whose effect is a SIDE-EFFECT dispatch onto io_device_bus
//         (SimFiredEvent → ingestMidiSignal). With no DataClip input wire to consume, the dispatch cook has
//         nothing to fire; simulateClipWindow (the golden core) is the math it WILL call once the input
//         wire lands.
//   The honest split (memory sw-stateful-node-parity-gap): the CORES are done+verified; these NODES are
//   registered (menu-visible, census done) with the cook DEFERRED to the dataclip/dataset wire-currency
//   seam. FORK fork-dataset-nodes-cook-deferred-no-wire-currency.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

// TiXL LoadDataClip (Operators/Lib/io/data/LoadDataClip.cs) — parse a `.data` JSON file → a DataClip
// (TimeClipSlot<DataClip?>, LoadDataClip.cs:25-26). Input: FilePath (string, the .data path,
// LoadDataClip.cs:100-101). The parse core = parseDataFile (dataset_ops.h:51, golden). Output "Clip" is a
// DataClip wire (no consumer currency yet → cook deferred). FORK fork-loaddataclip-cook-deferred.
// TiXL authority: LoadDataClip.cs
static const MathOp _reg_LoadDataClip{
    {"LoadDataClip", "LoadDataClip",
     {{"FilePath", "FilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      {"Clip", "Clip", "DataClip", false}},
     /*evaluate=*/nullptr,
     "io.data"}
};

// TiXL SimulateIoData (Operators/Lib/io/data/SimulateIoData.cs) — replay one-or-more DataClips through the
// io bus as the playhead sweeps: Execute (Slot<Command>, SimulateIoData.cs:49-50). Inputs: Clips
// (MultiInputSlot<DataClip?>, :279-280), Enabled (bool, :282-283). The window core = simulateClipWindow
// (dataset_ops.h:124, golden). The Command output rides the real command rail, but the real dispatch
// (SimFiredEvent → ingestMidiSignal) needs a DataClip input WIRE to consume — deferred until the dataclip
// wire currency lands. FORK fork-simulateiodata-dispatch-deferred.
// TiXL authority: SimulateIoData.cs
static const MathOp _reg_SimulateIoData{
    {"SimulateIoData", "SimulateIoData",
     {{"Execute", "Execute", "Command", false},
      {"Clips", "Clips", "DataClip", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, /*multiInput=*/true},
      {"Enabled", "Enabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr,
     "io.data"}
};

// TiXL MidiRecording (Operators/Lib/io/midi/MidiRecording.cs) — expose the ACTIVE recording SwDataSet as
// an output (Slot<DataSet>, MidiRecording.cs:9-10); ResetTrigger (bool, :31-32) clears it. The ingest core
// = recordMidiEvent (dataset_ops.h:87, golden). The DataSet output has no consumer wire currency yet, and
// the real ingest reads the app io_device_bus MIDI stream (a cook + state + app wiring) → deferred.
// FORK fork-midirecording-ingest-deferred.
// TiXL authority: MidiRecording.cs
static const MathOp _reg_MidiRecording{
    {"MidiRecording", "MidiRecording",
     {{"DataSet", "DataSet", "DataSet", false},
      {"ResetTrigger", "ResetTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr,
     "io.midi"}
};

}  // namespace
}  // namespace sw
