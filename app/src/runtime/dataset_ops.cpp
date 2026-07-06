// runtime/dataset_ops — impl of the three SwDataSet cores (parse / record / simulate). See dataset_ops.h.
// Each function is a faithful transcription of its TiXL .cs (cited inline), pure over the currency struct.
#include "runtime/dataset_ops.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "runtime/datapoint_json.h"  // jsonParse / jsonFind / JsonVal — the shared runtime JSON reader

namespace sw {

// ── Injection seams (module-static; goldens flip, restore) ──
bool& datasetParseInjectBug()     { static bool b = false; return b; }
bool& datasetRecordInjectBug()    { static bool b = false; return b; }
bool& datasetSimulateInjectBug()  { static bool b = false; return b; }

namespace {

// StartsWith(tag) on a std::string (TiXL string.StartsWith).
bool startsWith(const std::string& s, const std::string& pre) {
  return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
}

// Parse the integer suffix after `tag` in `segment` (TiXL int.TryParse(segment.AsSpan(tag.Length))). Returns
// false when the suffix is empty or not an integer. Used to pull "<n>" from "Ch<n>", "N<note>", "CC<num>".
bool parseIntSuffix(const std::string& segment, const std::string& tag, int& out) {
  if (!startsWith(segment, tag)) return false;
  const std::string suffix = segment.substr(tag.size());
  if (suffix.empty()) return false;
  size_t consumed = 0;
  try {
    out = std::stoi(suffix, &consumed);
  } catch (...) {
    return false;
  }
  return consumed == suffix.size();
}

// TiXL SimulateIoData.IsInside (:260): t > from && t <= to. Under the injection bug, use the WRONG left-
// inclusive predicate (t >= from) so an event exactly AT `from` (which must NOT re-fire) wrongly fires.
bool isInside(double t, double from, double to) {
  if (datasetSimulateInjectBug()) return t >= from && t <= to;  // -bug: left edge leaks
  return t > from && t <= to;
}

int clampi(double v, double lo, double hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return (int)v;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────────────────────
// 1. parseDataFile — DataSetCache.ParseDataSet (DataSetCache.cs:190-340).
// ─────────────────────────────────────────────────────────────────────────────────────────────
bool parseDataFile(const std::string& jsonText, SwDataSet& out) {
  out.clear();
  JsonVal root;
  if (!jsonParse(jsonText, root) || root.kind != JsonVal::Obj) return false;  // malformed → abort (:176-186)

  const JsonVal* channels = jsonFind(root, "Channels");
  if (!channels || channels->kind != JsonVal::Arr) return true;  // no Channels → empty set (TiXL :211-212)

  for (const JsonVal& chTok : channels->arr) {
    if (chTok.kind != JsonVal::Obj) continue;  // TiXL: not a JObject → skip (:216-217)

    // Type: absent → "float"; non-float → skip with (TiXL warns) (:195-204).
    const JsonVal* typeTok = jsonFind(chTok, "Type");
    std::string typeName = (typeTok && typeTok->kind == JsonVal::Str) ? typeTok->str : "float";
    if (typeName != "float") continue;

    DataChannel ch;

    // Path: JSON array of string segments (:260-269).
    const JsonVal* pathTok = jsonFind(chTok, "Path");
    if (pathTok && pathTok->kind == JsonVal::Arr) {
      for (const JsonVal& seg : pathTok->arr)
        if (seg.kind == JsonVal::Str) ch.path.push_back(seg.str);
    }

    // DurationType: "Interval"/"Tick" declared, or absent → sniff per event (:272-275, :299-314).
    const JsonVal* durTok = jsonFind(chTok, "DurationType");
    const bool haveDeclared = durTok && durTok->kind == JsonVal::Str;
    const std::string declared = haveDeclared ? durTok->str : std::string{};
    ch.durationType = (declared == "Interval") ? ChannelDurationType::Interval : ChannelDurationType::Tick;

    // Events (:285-334).
    const JsonVal* evsTok = jsonFind(chTok, "Events");
    if (evsTok && evsTok->kind == JsonVal::Arr) {
      for (const JsonVal& evTok : evsTok->arr) {
        if (evTok.kind != JsonVal::Obj) continue;
        const JsonVal* timeTok = jsonFind(evTok, "Time");
        const JsonVal* valTok = jsonFind(evTok, "Value");
        const JsonVal* endTok = jsonFind(evTok, "EndTime");

        DataEvent ev;
        ev.time = (timeTok && timeTok->kind == JsonVal::Num) ? timeTok->num : 0.0;   // (:294)
        ev.value = (valTok && valTok->kind == JsonVal::Num) ? (float)valTok->num : 0.0f;  // (:295)

        // Promote-to-interval rule (:301-314): declared Interval → always; declared Tick → never; absent →
        // sniff (EndTime present → interval). A Tick channel never produces an interval even with a stray
        // EndTime (defensive against writer bugs).
        bool isInterval;
        if (declared == "Interval") isInterval = true;
        else if (declared == "Tick") isInterval = false;
        else isInterval = (endTok != nullptr);

        ev.isInterval = isInterval;
        if (isInterval) {
          ev.endTime = (endTok && endTok->kind == JsonVal::Num) ? endTok->num
                                                                : std::numeric_limits<double>::infinity();  // (:318)
        }
        ch.events.push_back(ev);
      }
    }
    out.channels.push_back(std::move(ch));
  }

  // Test-only teeth: corrupt the REAL parsed set (drop the last event of the last channel) so a golden's
  // RED bites the parse path (not a flipped expected value). did-not-trip stays inert (nothing to drop).
  if (datasetParseInjectBug() && !out.channels.empty() && !out.channels.back().events.empty())
    out.channels.back().events.pop_back();

  return true;
}

// ─────────────────────────────────────────────────────────────────────────────────────────────
// 2. recordMidiEvent — MidiDataRecording.MessageReceivedHandler (MidiDataRecording.cs:44-135).
// ─────────────────────────────────────────────────────────────────────────────────────────────
namespace {

// Find-or-create the channel whose Path matches `path`, with the given duration type. TiXL keys channels by
// a per-type hash (MidiDataRecording FindOrCreate*), but the OBSERVABLE result is "the one channel with this
// exact recorder path"; matching on the path is faithful and avoids replicating the hash (which only exists
// to dedup — a new path is a new channel, an existing path reuses it). Appends to set.channels when absent.
DataChannel& findOrCreateChannel(SwDataSet& set, const std::vector<std::string>& path,
                                 ChannelDurationType dur) {
  for (DataChannel& c : set.channels)
    if (c.path == path) return c;
  DataChannel ch;
  ch.path = path;
  ch.durationType = dur;
  set.channels.push_back(std::move(ch));
  return set.channels.back();
}

}  // namespace

void recordMidiEvent(SwDataSet& set, const std::string& deviceName, const MidiRecEvent& ev) {
  using namespace DataSetPaths;
  const std::string chSeg = std::string(kChannelPrefix) + std::to_string(ev.channel);  // "Ch<n>"

  switch (ev.kind) {
    case MidiRecKind::NoteOn:
    case MidiRecKind::NoteOff: {
      const std::string typeSeg = std::string(kNoteTag) + std::to_string(ev.number);  // "N<note>"
      DataChannel& ch =
          findOrCreateChannel(set, {kMidiNamespace, deviceName, chSeg, typeSeg}, ChannelDurationType::Interval);

      DataEvent* last = ch.getLastEvent();  // TiXL GetLastEvent as DataIntervalEvent (:68)

      if (ev.kind == MidiRecKind::NoteOff) {
        // Finish the open interval at this time (:73-75). -bug: skip the finish (note stays unfinished).
        if (last && last->isInterval && last->isUnfinished() && !datasetRecordInjectBug())
          last->endTime = ev.time;
        return;
      }

      // NoteOn (:79-92): if the last note is unfinished, finish it first; a velocity-0 NoteOn is a NoteOff
      // in disguise → finish only, open nothing.
      if (last && last->isInterval && last->isUnfinished()) {
        if (!datasetRecordInjectBug()) last->endTime = ev.time;
        if (ev.value == 0) return;
      }
      DataEvent note;
      note.isInterval = true;
      note.time = ev.time;
      note.endTime = std::numeric_limits<double>::infinity();
      note.value = (float)ev.value;
      ch.events.push_back(note);
      return;
    }

    case MidiRecKind::ControlChange: {
      const std::string typeSeg = std::string(kControlChangeTag) + std::to_string(ev.number);  // "CC<num>"
      DataChannel& ch =
          findOrCreateChannel(set, {kMidiNamespace, deviceName, chSeg, typeSeg}, ChannelDurationType::Tick);
      DataEvent e; e.isInterval = false; e.time = ev.time; e.value = (float)ev.value;
      ch.events.push_back(e);
      return;
    }
    case MidiRecKind::PitchWheelChange: {
      DataChannel& ch =
          findOrCreateChannel(set, {kMidiNamespace, deviceName, chSeg, kPitchBendTag}, ChannelDurationType::Tick);
      DataEvent e; e.isInterval = false; e.time = ev.time; e.value = (float)ev.value;
      ch.events.push_back(e);
      return;
    }
    case MidiRecKind::ChannelPressure: {
      DataChannel& ch =
          findOrCreateChannel(set, {kMidiNamespace, deviceName, chSeg, kChannelPressureTag}, ChannelDurationType::Tick);
      DataEvent e; e.isInterval = false; e.time = ev.time; e.value = (float)ev.value;
      ch.events.push_back(e);
      return;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────
// 3. simulateClipWindow — SimulateIoData.DispatchOneClip/DispatchChannel (SimulateIoData.cs:101-251).
// ─────────────────────────────────────────────────────────────────────────────────────────────
namespace {

void dispatchMidiChannel(const DataChannel& ch, double from, double to, std::vector<SimFiredEvent>& out) {
  using namespace DataSetPaths;
  const std::string& device = ch.path[1];
  const std::string& channelSeg = ch.path[2];  // "Ch<n>"
  const std::string& typeSeg = ch.path[3];     // "N<note>" | "CC<num>" | "PB" | "CP"

  int midiChannel = 0;
  if (!parseIntSuffix(channelSeg, kChannelPrefix, midiChannel)) return;  // TryParseChannelNumber (:262-269)

  for (const DataEvent& ev : ch.events) {
    // Note intervals (:194-207): NoteOn at Time, NoteOff at EndTime; each edge gated independently.
    if (ev.isInterval && startsWith(typeSeg, kNoteTag)) {
      int note = 0;
      if (!parseIntSuffix(typeSeg, kNoteTag, note)) continue;
      int velocity = clampi((double)ev.value, 0.0, 127.0);  // (:199)
      if (isInside(ev.time, from, to)) {
        SimFiredEvent f; f.kind = SimEventKind::MidiNoteOn; f.device = device; f.channel = midiChannel;
        f.number = note; f.value = velocity; out.push_back(f);
      }
      if (!std::isinf(ev.endTime) && isInside(ev.endTime, from, to)) {  // (:203)
        SimFiredEvent f; f.kind = SimEventKind::MidiNoteOff; f.device = device; f.channel = midiChannel;
        f.number = note; f.value = 0; out.push_back(f);
      }
      continue;
    }

    if (!isInside(ev.time, from, to)) continue;  // (:209)

    if (startsWith(typeSeg, kControlChangeTag)) {
      int controller = 0;
      if (!parseIntSuffix(typeSeg, kControlChangeTag, controller)) continue;  // (:214)
      SimFiredEvent f; f.kind = SimEventKind::MidiCC; f.device = device; f.channel = midiChannel;
      f.number = controller; f.value = clampi((double)ev.value, 0.0, 127.0); out.push_back(f);  // (:216)
    } else if (typeSeg == kPitchBendTag) {
      SimFiredEvent f; f.kind = SimEventKind::MidiPitchBend; f.device = device; f.channel = midiChannel;
      f.value = clampi((double)ev.value, 0.0, 16383.0); out.push_back(f);  // (:221)
    } else if (typeSeg == kChannelPressureTag) {
      SimFiredEvent f; f.kind = SimEventKind::MidiChannelPressure; f.device = device; f.channel = midiChannel;
      f.value = clampi((double)ev.value, 0.0, 127.0); out.push_back(f);  // (:226)
    }
  }
}

void dispatchOscChannel(const DataChannel& ch, const std::string& prefix, double from, double to,
                        std::vector<SimFiredEvent>& out) {
  // prefix == "OSC:<port>" (:234-235).
  const std::string oscPrefix = std::string(DataSetPaths::kOscNamespace) + ":";
  int port = 0;
  {
    const std::string portStr = prefix.substr(oscPrefix.size());
    if (portStr.empty()) return;
    try {
      size_t consumed = 0;
      port = std::stoi(portStr, &consumed);
      if (consumed != portStr.size()) return;
    } catch (...) {
      return;
    }
  }
  // Address = "/" + join(path[1..], '/') (:240).
  std::string address = "/";
  for (size_t i = 1; i < ch.path.size(); ++i) {
    if (i > 1) address += "/";
    address += ch.path[i];
  }
  for (const DataEvent& ev : ch.events) {
    if (ev.isInterval) continue;  // TiXL: `is not DataEvent ev` skips intervals (:244)
    if (!isInside(ev.time, from, to)) continue;  // (:246)
    SimFiredEvent f; f.kind = SimEventKind::Osc; f.oscPort = port; f.oscAddress = address; f.oscValue = ev.value;
    out.push_back(f);
  }
}

}  // namespace

std::vector<SimFiredEvent> simulateClipWindow(const SwDataClip& clip, double localBars, double lastSourceSecs) {
  std::vector<SimFiredEvent> out;

  if (!clip.hasMapping) return out;  // TiXL: clip.Mapping is null → skip (:105)
  const SwTimeRangeMapping& m = clip.mapping;

  // Source-bar bounds in seconds (:118-119): SourceRange.{Start,End} bars → secs directly (240/BPM).
  const double sourceMinSecs = m.sourceStart * 240.0 / m.bpm;
  const double sourceMaxSecs = m.sourceEnd * 240.0 / m.bpm;

  if (!m.isActive(localBars)) return out;  // playhead outside window → skip (:124)

  double last = lastSourceSecs;
  if (last < sourceMinSecs) last = sourceMinSecs;  // clamp cold-start (:136)

  double current = m.localBarsToSourceSecs(localBars);
  if (current > sourceMaxSecs) current = sourceMaxSecs;  // clamp trailing edge (:139)

  if (current <= last) return out;  // backward scrub / no movement → skip (:142)

  using namespace DataSetPaths;
  const std::string oscPrefix = std::string(kOscNamespace) + ":";
  for (const DataChannel& ch : clip.set.channels) {
    if (ch.path.empty()) continue;                                     // (:163)
    const std::string& prefix = ch.path[0];
    if (prefix == kMidiNamespace && ch.path.size() >= 4) {             // (:168)
      dispatchMidiChannel(ch, last, current, out);
    } else if (startsWith(prefix, oscPrefix) && ch.path.size() >= 2) { // (:172)
      dispatchOscChannel(ch, prefix, last, current, out);
    }
  }
  return out;
}

}  // namespace sw
