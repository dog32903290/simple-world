// runtime/dataset_ops — the three FREE-FUNCTION cores over the SwDataSet currency (the dataset-currency
// seam producers + consumer). Mirrors SwFloatDict's producer/consumer-as-free-functions split (dict_cook.h):
// no woven cook slot — the io/data ops are Command/Clip-rail, so their DATA LOGIC lives here as pure
// functions a golden can drive directly (append→assert / parse→assert / window→assert), and the thin
// wire/bus adapters live in the node cook (deferred where they touch real hardware).
//
//   1. parseDataFile   — LoadDataClip core. `.data` JSON text → SwDataSet. TiXL DataSetCache.ParseDataSet
//                        (DataSetCache.cs:190-340). Uses the shared runtime JSON reader (datapoint_json.h).
//   2. recordMidiEvent — MidiRecording core. one decoded MIDI event → APPEND channel/event into a SwDataSet
//                        (find-or-create channel by path; note-pairing for intervals). TiXL
//                        MidiDataRecording.MessageReceivedHandler (MidiDataRecording.cs:44-135).
//   3. simulateClipWindow — SimulateIoData core. a SwDataClip + a (last, current] playhead window → the list
//                        of events that FIRE this frame, each already resolved to its device/address target.
//                        TiXL SimulateIoData.DispatchOneClip/DispatchChannel (SimulateIoData.cs:101-251). The
//                        real dispatch to the io bus is a thin adapter over this list (deferred-hw seam).
//
// runtime leaf: pure computation, no GPU/UI/hardware/upward dep.
#pragma once

#include <string>
#include <vector>

#include "runtime/dataset.h"       // SwDataSet / DataChannel / DataEvent
#include "runtime/dataset_clip.h"  // SwDataClip / SwTimeRangeMapping

namespace sw {

// ── Channel-path tags (TiXL IoDataSetRecorder.ChannelPaths, IoDataSetRecorder.cs:439-460). The recorder
// writes these and the replay/parse read them; single source of truth so recorder and consumer can't drift.
namespace DataSetPaths {
inline const char* kMidiNamespace = "Midi";  // path[0] for MIDI channels (:442)
inline const char* kOscNamespace = "OSC";     // path[0] prefix "OSC:<port>" for OSC channels (:445)
inline const char* kChannelPrefix = "Ch";     // path[2] "Ch<n>" — 1-based MIDI channel (:448)
inline const char* kNoteTag = "N";            // path[3] "N<note>" — MIDI note (Interval) (:451)
inline const char* kControlChangeTag = "CC";  // path[3] "CC<num>" — MIDI CC (Tick) (:454)
inline const char* kPitchBendTag = "PB";      // path[3] "PB" — MIDI pitch bend (Tick) (:457)
inline const char* kChannelPressureTag = "CP";// path[3] "CP" — MIDI channel pressure (Tick) (:460)
}  // namespace DataSetPaths

// ─────────────────────────────────────────────────────────────────────────────────────────────
// 1. LoadDataClip core — parse `.data` JSON → SwDataSet.
// ─────────────────────────────────────────────────────────────────────────────────────────────
// Parse the v1 `.data` JSON in `jsonText` into `out`. Mirrors DataSetCache.ParseDataSet
// (DataSetCache.cs:190-340): reads Channels[].{Path[], Type, DurationType, Events[].{Time,Value,EndTime?}}.
//   • Only "float" (or absent Type → "float", :195) channels are reconstructed; others skipped (:200-204).
//   • DurationType "Interval" → every event is interval; "Tick" → every event is tick; absent → SNIFF per
//     event (EndTime present → interval, else tick — the legacy path, :299-314).
// Guid/Version/Metadata are parsed-and-ignored (the currency omits them — see dataset.h header). Returns
// false only on MALFORMED json (mirrors the .cs try/catch that aborts on a parse error, :176-186); a
// well-formed file with no Channels array yields an empty SwDataSet (true) — TiXL returns the empty set too.
bool parseDataFile(const std::string& jsonText, SwDataSet& out);

// Test-only injection seam (golden): when true, parseDataFile CORRUPTS the real parse (drops the last event
// of the last channel) so a golden's RED bites the PARSE path, not by flipping the expected value. Off in
// production. Mirrors dictInjectBug's convention (dict_op_registry.h).
bool& datasetParseInjectBug();

// ─────────────────────────────────────────────────────────────────────────────────────────────
// 2. MidiRecording core — one decoded MIDI event → append into a SwDataSet.
// ─────────────────────────────────────────────────────────────────────────────────────────────
// The decoded MIDI event the recorder ingests (the fields MidiDataRecording.MessageReceivedHandler reads).
// kind mirrors the switch on MidiEvent subtype (MidiDataRecording.cs:64):
enum class MidiRecKind {
  NoteOn,           // NoteEvent, CommandCode NoteOn  (:77-93) — velocity>0 opens an interval
  NoteOff,          // NoteEvent, CommandCode NoteOff (:73-75) — finishes the open interval
  ControlChange,    // ControlChangeEvent (:96-104)  — Tick, value = ControllerValue
  PitchWheelChange, // PitchWheelChangeEvent (:107-114) — Tick, value = Pitch
  ChannelPressure,  // ChannelAfterTouchEvent (:117-125) — Tick, value = AfterTouchPressure
};

struct MidiRecEvent {
  MidiRecKind kind = MidiRecKind::ControlChange;
  int channel = 1;        // 1-based MIDI channel (path segment "Ch<channel>")
  int number = 0;         // note number (Note*) / controller id (CC) — ignored for PB/CP
  int value = 0;          // velocity / controller value / pitch / pressure
  double time = 0.0;      // Playback.RunTimeInSecs at ingest (TiXL `someTime`, MidiDataRecording.cs:62)
};

// Record `ev` into `set` at wall time `ev.time`, mirroring MidiDataRecording.MessageReceivedHandler
// (:44-135): find-or-create the channel by its recorder path, then:
//   • ControlChange/PitchWheel/ChannelPressure → append a Tick DataEvent { time, value } (:96-125).
//   • NoteOn  → if the channel's last event is an UNFINISHED interval, finish it first (:82-86); then, when
//               velocity>0, open a NEW interval { time, endTime=+inf, value=velocity } (:88-93). velocity==0
//               NoteOn is a NoteOff-in-disguise: finish only, open nothing (:85).
//   • NoteOff → finish the channel's last unfinished interval at `time` (:73-75); open nothing.
// `deviceName` is the recorder's device segment (path[1]).
void recordMidiEvent(SwDataSet& set, const std::string& deviceName, const MidiRecEvent& ev);

// Test-only injection seam (golden): when true, recordMidiEvent SKIPS finishing an interval on NoteOff (the
// note stays unfinished) so a golden asserting the paired EndTime goes RED on the real record path.
bool& datasetRecordInjectBug();

// ─────────────────────────────────────────────────────────────────────────────────────────────
// 3. SimulateIoData core — clip + (last, current] window → fired events.
// ─────────────────────────────────────────────────────────────────────────────────────────────
// One event the replay FIRES this frame, already resolved to its target (what SimulatedIoBus.DispatchMidi/
// DispatchOsc would carry — SimulateIoData.cs:201-249). A golden asserts this list; the real io-bus adapter
// (deferred) just translates each entry into ingestMidiSignal / ingestOscArg.
enum class SimEventKind { MidiNoteOn, MidiNoteOff, MidiCC, MidiPitchBend, MidiChannelPressure, Osc };

struct SimFiredEvent {
  SimEventKind kind = SimEventKind::MidiCC;
  std::string device;    // MIDI: device product name (path[1]) ; OSC: unused
  int channel = 0;       // MIDI channel (1-based)
  int number = 0;        // note number / CC controller id (MIDI Note/CC)
  int value = 0;         // MIDI: clamped velocity/cc/pitch/pressure (cs:199/216/221/226)
  int oscPort = 0;       // OSC: port parsed from "OSC:<port>" (cs:235)
  std::string oscAddress;// OSC: "/" + join(path[1..]) (cs:240)
  float oscValue = 0.0f; // OSC: the event's float value (cs:249)
};

// Compute the events a single clip fires as the playhead moves from source-second window (last, current].
// Mirrors SimulateIoData.DispatchOneClip + DispatchChannel + DispatchMidiChannel/DispatchOscChannel
// (SimulateIoData.cs:101-251), but returns the fired list instead of dispatching (the golden's oracle; the
// bus adapter is the deferred leg). The window predicate is TiXL's IsInside: t > from && t <= to (cs:260).
//
// `localBars` is the playhead (context.LocalTime). Per the .cs, the op:
//   • skips (fires nothing) when clip has no mapping OR the playhead is outside the timeline window (:105/124);
//   • converts localBars → currentSourceSecs via mapping.localBarsToSourceSecs, clamped to the source slice;
//   • fires every channel event whose Time (note: EndTime for NoteOff) is in (lastSourceSecs, currentSourceSecs].
// `lastSourceSecs` is the per-clip cursor from the previous frame (the caller owns cursor state — a golden
// passes explicit windows). Backward/no-movement (current<=last) fires nothing (:142). Returns the events in
// channel order, then event order (SimulateIoData's foreach order).
std::vector<SimFiredEvent> simulateClipWindow(const SwDataClip& clip, double localBars, double lastSourceSecs);

// Test-only injection seam (golden): when true, simulateClipWindow uses the WRONG window predicate
// (t >= from && t <= to — includes the left edge) so a golden probing an event exactly AT `from` (which must
// NOT fire) goes RED on the real windowing path. Bites the (last,current] boundary.
bool& datasetSimulateInjectBug();

}  // namespace sw
