// runtime/dataset — the SwDataSet HOST VALUE CURRENCY (the dataset-currency seam). The C++ twin of TiXL's
// T3.Core.DataTypes.DataSet.DataSet (external/tixl/Core/DataTypes/DataSet/DataSet.cs): a recorded session of
// time-stamped event channels (MIDI / OSC capture, `.data` clips), the value type that rides on the wires
// between the io/data ops:
//   • LoadDataClip  — parses a `.data` JSON file → a SwDataSet, wrapped in a SwDataClip (+ timeline mapping).
//   • MidiRecording — ingests decoded MIDI events → APPENDS channels/events into a SwDataSet.
//   • SimulateIoData— consumes SwDataClip(s) and replays each channel's events through the io bus as the
//                     playhead sweeps (the currency-validation consumer).
//
// TiXL MODEL (DataSet.cs:20-330):
//   DataSet   = List<DataChannel> (+ Guid/Version/Metadata — provenance, NOT needed by the replay math).
//   DataChannel = { Path: List<string>, DurationType: "Tick"|"Interval", Events: List<DataEvent?> }.
//   DataEvent = { double Time, float Value }.  DataIntervalEvent : DataEvent adds { double EndTime }.
//     Tick events occupy a MOMENT (MIDI CC / OSC / telemetry); Interval events SPAN a duration (MIDI notes:
//     NoteOn at Time, NoteOff at EndTime; EndTime = +inf while a note is held / unfinished).
//
// THE CURRENCY (this file): a value-typed struct mirror. We fold DataEvent + DataIntervalEvent into ONE
// `DataEvent` with an `isInterval` flag + `endTime` (kInfinity when a Tick event or an unfinished note) —
// C++ has no cheap sealed-subclass RTTI dispatch and the consumers already branch on the channel's
// durationType, so a flat struct is faithful AND simpler (rule 7 "先求簡單"). `path` is a vector<string> of
// segments (NO '/' join — TiXL serialises Path as a JSON array, DataSet.cs:200-206, so device names with a
// '/' round-trip). Guid/Version/Metadata are DELIBERATELY OMITTED: the replay/analysis math never reads
// them (SimulateIoData.cs / MidiClip.cs only walk Channels→Events), and omitting them keeps the currency a
// tight value type. A future editor/provenance seam can add them without touching the replay path.
//
// runtime leaf: pure computation (std::vector / std::string / double), no GPU, no UI, no upward dep. The
// producers (LoadDataClip parse, MidiRecording ingest) and the consumer (SimulateIoData window) are all
// free functions over this struct, exactly like SwFloatDict's producer/consumer split.
#pragma once

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sw {

// TiXL DataChannel.DurationType discriminators (ChannelDurationTypes, DataSet.cs:300-307). String on the
// wire so future shapes add without subclass churn; an enum here because sw fixes the two live values.
enum class ChannelDurationType {
  Tick,      // moment-in-time events { Time, Value } — MIDI CC / OSC / telemetry (DataSet.cs:303)
  Interval,  // duration events { Time, EndTime, Value } — MIDI notes / regions (DataSet.cs:306)
};

// One time-stamped event on a channel. Folds TiXL DataEvent + DataIntervalEvent (DataSet.cs:309-378):
//   Tick event      → isInterval=false, endTime ignored.
//   Interval event  → isInterval=true;  endTime = the span end (kInfinity while unfinished, DataSet.cs:361).
struct DataEvent {
  double time = 0.0;                                        // TiXL DataEvent.Time (seconds from record-start)
  double endTime = std::numeric_limits<double>::infinity(); // TiXL DataIntervalEvent.EndTime (+inf = unfinished)
  float value = 0.0f;                                       // TiXL DataEvent.Value (float in v1)
  bool isInterval = false;                                  // false = DataEvent (Tick); true = DataIntervalEvent

  bool isUnfinished() const { return std::isinf(endTime); }  // TiXL DataIntervalEvent.IsUnfinished (:355)
};

// One event channel = an ordered path + a duration type + its events (TiXL DataChannel, DataSet.cs:158-289).
// Path segments mirror what IoDataSetRecorder writes:
//   ["Midi", deviceName, "Ch<n>", "<type><param>"]  — MIDI capture (MidiDataRecording.cs FindOrCreate*)
//   ["OSC:<port>", ...address segments]              — OSC capture
struct DataChannel {
  std::vector<std::string> path;                       // TiXL DataChannel.Path (JSON array of segments)
  ChannelDurationType durationType = ChannelDurationType::Tick;
  std::vector<DataEvent> events;                       // TiXL DataChannel.Events (Time-ascending as recorded)

  // TiXL DataChannel.GetLastEvent (DataSet.cs:222-227): the newest event, or nullptr when empty. Used by
  // the recorder's note-pairing (finish the last unfinished NoteOn on the matching NoteOff).
  DataEvent* getLastEvent() { return events.empty() ? nullptr : &events.back(); }
};

// A recorded session = an ordered list of channels (TiXL DataSet, DataSet.cs:20). The value that rides the
// wire. Ordered (vector, not map) because the replay walks channels/events in recorded order and duplicate
// paths are legal in TiXL (List<DataChannel>, no uniqueness constraint).
struct SwDataSet {
  std::vector<DataChannel> channels;

  void clear() { channels.clear(); }   // TiXL DataSet.Clear (DataSet.cs:87-91) — Metadata omitted (see header)
};

}  // namespace sw
