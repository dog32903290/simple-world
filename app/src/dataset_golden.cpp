// dataset_golden — --selftest-dataset. THE SwDataSet currency golden (the dataset-currency seam). Proves the
// three cores over the currency end-to-end, closed-form, against the TiXL .cs:
//   • parseDataFile     — LoadDataClip's parse (DataSetCache.ParseDataSet, DataSetCache.cs:190-340).
//   • recordMidiEvent   — MidiRecording's ingest (MidiDataRecording.cs:44-135), incl. note-interval pairing.
//   • simulateClipWindow— SimulateIoData's replay windowing (SimulateIoData.cs:101-260) + TimeRangeMapping
//                         bars→secs (TimeRangeMapping.cs:47-89) — the currency-VALIDATION consumer.
//
// EXPECTED VALUES — hand-derived from the .cs (impl-independent, GOLDEN_STANDARD three-features):
//   The probes sit at DIVERGENT points, not identity edges: the (last, current] window straddles events so
//   the boundary predicate is exercised (an event AT `from` must NOT fire; one just past must); the remap
//   uses TimeRange≠SourceRange so a wrong rate reddens; note pairing uses a held-then-finished interval so a
//   missing finish reddens; parse mixes a Tick and an Interval channel so the sniff/duration branch is real.
//
// TEETH (three real cook seams, NOT want-flip — each corrupts the REAL core, want stays FIXED):
//   • datasetParseInjectBug()    — parse drops the last parsed event → the parse assert diverges.
//   • datasetRecordInjectBug()   — record skips the NoteOff finish → the paired EndTime assert diverges.
//   • datasetSimulateInjectBug() — window uses the wrong left-inclusive predicate → the at-`from` event
//                                  wrongly fires → the fired-count / boundary assert diverges.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "runtime/dataset.h"
#include "runtime/dataset_clip.h"
#include "runtime/dataset_ops.h"

namespace sw {
namespace {

bool nearf(double a, double b) { return std::fabs(a - b) < 1e-4; }

}  // namespace

int runDataSetSelfTest(bool injectBug) {
  bool ok = true;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // LEG 1 — parseDataFile (LoadDataClip core). A v1 `.data` with ONE Tick channel (CC) and ONE Interval
  // channel (a note with EndTime). Expected content hand-read from the JSON per ParseDataSet (:285-334).
  //   Midi/dev/Ch1/CC74 : Tick, events [(0.10,20),(0.30,70)]  → 2 tick events.
  //   Midi/dev/Ch1/N60  : Interval, event (0.50, end 0.90, value 100) → 1 interval event.
  // A wrong duration/sniff branch (e.g. treating the note channel as Tick) drops the EndTime → RED (P2).
  // ─────────────────────────────────────────────────────────────────────────────────────────────
  const std::string dataJson = R"JSON({
    "Version": 1,
    "Guid": "ignored-by-currency",
    "Channels": [
      { "Path": ["Midi","dev","Ch1","CC74"], "Type": "float", "DurationType": "Tick",
        "Events": [ {"Time":0.10,"Value":20.0}, {"Time":0.30,"Value":70.0} ] },
      { "Path": ["Midi","dev","Ch1","N60"], "Type": "float", "DurationType": "Interval",
        "Events": [ {"Time":0.50,"EndTime":0.90,"Value":100.0} ] }
    ]
  })JSON";
  {
    datasetParseInjectBug() = injectBug;
    SwDataSet parsed;
    bool okParse = parseDataFile(dataJson, parsed);
    datasetParseInjectBug() = false;

    // FIXED wants (P3-safe): 2 channels; CC channel 2 events (0.30→70 the last); Note channel interval 0.50→0.90.
    // Under -bug the parse drops the LAST channel's last event → the note channel has 0 events → RED.
    bool pass = okParse && parsed.channels.size() == 2 &&
                parsed.channels[0].durationType == ChannelDurationType::Tick &&
                parsed.channels[0].events.size() == 2 &&
                nearf(parsed.channels[0].events[1].time, 0.30) &&
                nearf(parsed.channels[0].events[1].value, 70.0) &&
                parsed.channels[1].durationType == ChannelDurationType::Interval &&
                parsed.channels[1].events.size() == 1 &&                       // -bug drops this → 0 → RED
                parsed.channels[1].events[0].isInterval &&
                nearf(parsed.channels[1].events[0].time, 0.50) &&
                nearf(parsed.channels[1].events[0].endTime, 0.90) &&
                nearf(parsed.channels[1].events[0].value, 100.0);
    ok = ok && pass;
    std::printf("[selftest-dataset] LEG1 parse: chans=%zu noteEvents=%zu -> %s\n",
                parsed.channels.size(),
                parsed.channels.size() >= 2 ? parsed.channels[1].events.size() : 0,
                pass ? "PASS" : "FAIL");
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // LEG 2 — recordMidiEvent (MidiRecording core). Ingest a NoteOn then a NoteOff on the same note; the
  // recorder must open ONE interval then FINISH it (MidiDataRecording.cs:73-93). Plus a CC → one Tick event.
  //   NoteOn  N60 vel=100 @ t=1.0  → open interval (1.0, +inf, 100)
  //   NoteOff N60      @ t=2.5      → finish → endTime 2.5
  //   CC 74 val=40     @ t=1.2      → Tick event (1.2, 40)
  // Expected: 2 channels; note channel one interval (1.0→2.5, 100); CC channel one tick (1.2, 40). Under
  // -bug the finish is skipped → endTime stays +inf → RED (bites the real record pairing).
  // ─────────────────────────────────────────────────────────────────────────────────────────────
  {
    datasetRecordInjectBug() = injectBug;
    SwDataSet rec;
    recordMidiEvent(rec, "dev", {MidiRecKind::NoteOn, 1, 60, 100, 1.0});
    recordMidiEvent(rec, "dev", {MidiRecKind::ControlChange, 1, 74, 40, 1.2});
    recordMidiEvent(rec, "dev", {MidiRecKind::NoteOff, 1, 60, 0, 2.5});
    datasetRecordInjectBug() = false;

    // Locate channels by their type segment (path[3]) — order is insertion (note, then CC).
    const DataChannel* note = nullptr;
    const DataChannel* cc = nullptr;
    for (const DataChannel& c : rec.channels) {
      if (c.path.size() == 4 && c.path[3] == "N60") note = &c;
      if (c.path.size() == 4 && c.path[3] == "CC74") cc = &c;
    }
    bool pass = rec.channels.size() == 2 && note && cc &&
                note->durationType == ChannelDurationType::Interval &&
                note->events.size() == 1 && note->events[0].isInterval &&
                nearf(note->events[0].time, 1.0) &&
                nearf(note->events[0].endTime, 2.5) &&   // -bug leaves +inf → RED
                nearf(note->events[0].value, 100.0) &&
                cc->durationType == ChannelDurationType::Tick &&
                cc->events.size() == 1 && !cc->events[0].isInterval &&
                nearf(cc->events[0].time, 1.2) && nearf(cc->events[0].value, 40.0);
    ok = ok && pass;
    std::printf("[selftest-dataset] LEG2 record: noteEnd=%.3f (want 2.500) -> %s\n",
                note && !note->events.empty() ? note->events[0].endTime : -1.0,
                pass ? "PASS" : "FAIL");
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // LEG 3 — simulateClipWindow (SimulateIoData core + TimeRangeMapping). Build a DataClip whose set has a CC
  // channel with three events at source-secs 0.10, 0.30, 0.50; a mapping TimeRange[0,4]→SourceRange[0,4] at
  // BPM 120 → 1 bar = 2 secs (240/120). So localBars→sourceSecs = localBars*2.
  //   Playhead sweeps from lastSourceSecs=0.20 to localBars=0.20 (→ current=0.40 secs).
  //   Window (0.20, 0.40]: event 0.10 (<= from, EXCLUDED), 0.30 (fires), 0.50 (> to, EXCLUDED) → exactly 1
  //   fired CC (controller 74, value 70).
  // BOUNDARY tooth: an event exactly AT `from`=0.20 must NOT fire (IsInside is t>from). -bug flips to t>=from
  // → the 0.20 event wrongly fires → count 2 ≠ 1 → RED. So add a 4th event at 0.20 to make the edge live.
  // ─────────────────────────────────────────────────────────────────────────────────────────────
  {
    datasetSimulateInjectBug() = injectBug;
    SwDataClip clip;
    clip.hasMapping = true;
    clip.mapping.timeStart = 0.0; clip.mapping.timeEnd = 4.0;
    clip.mapping.sourceStart = 0.0; clip.mapping.sourceEnd = 4.0;
    clip.mapping.bpm = 120.0;  // 1 bar = 2 secs

    DataChannel ch;
    ch.path = {"Midi", "dev", "Ch1", "CC74"};
    ch.durationType = ChannelDurationType::Tick;
    for (double t : {0.10, 0.20, 0.30, 0.50}) {
      DataEvent e; e.isInterval = false; e.time = t; e.value = 70.0f; ch.events.push_back(e);
    }
    clip.set.channels.push_back(ch);

    // localBars=0.20 → sourceSecs current = 0.20*2 = 0.40. lastSourceSecs = 0.20. Window (0.20, 0.40].
    std::vector<SimFiredEvent> fired = simulateClipWindow(clip, /*localBars=*/0.20, /*lastSourceSecs=*/0.20);
    datasetSimulateInjectBug() = false;

    // FIXED want: exactly 1 fired (the 0.30 event); 0.20 excluded (t>from), 0.10 excluded, 0.50 excluded.
    // -bug (t>=from) also fires the 0.20 event → count 2 → RED.
    bool pass = fired.size() == 1 &&
                fired[0].kind == SimEventKind::MidiCC &&
                fired[0].device == "dev" && fired[0].channel == 1 &&
                fired[0].number == 74 && fired[0].value == 70;
    ok = ok && pass;
    std::printf("[selftest-dataset] LEG3 simulate: fired=%zu (want 1) -> %s\n", fired.size(),
                pass ? "PASS" : "FAIL");
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // LEG 3b — simulate NOTE interval edges + remap rate. A note interval [source 1.0 .. 3.0] under a
  // NON-IDENTITY mapping: TimeRange[0,2]→SourceRange[0,4], BPM 120. rate = (4-0)/(2-0) = 2 (source-bars per
  // timeline-bar). localBars→sourceBars = localBars*2; →secs *2 again → localBars→sourceSecs = localBars*4.
  //   Sweep localBars 0.20 (current sourceSecs = 0.80) with last = 0.0. Window (0.0, 0.80]: the NoteOn at
  //   source-secs 0.50 fires (inside), the NoteOff at 3.0 does NOT (outside) → exactly 1 NoteOn.
  // A wrong remap rate (identity, missing the *2) would put current at 0.40 secs → still fires the 0.50? No —
  // 0.50 > 0.40 → would NOT fire → count 0 ≠ 1 → the remap rate is genuinely load-bearing here (P2 guard).
  // No teeth here (covered by LEG3); pure boundary. Only run on the non-bug pass.
  // ─────────────────────────────────────────────────────────────────────────────────────────────
  if (!injectBug) {
    SwDataClip clip;
    clip.hasMapping = true;
    clip.mapping.timeStart = 0.0; clip.mapping.timeEnd = 2.0;
    clip.mapping.sourceStart = 0.0; clip.mapping.sourceEnd = 4.0;  // rate 2 → localBars→sourceSecs = *4
    clip.mapping.bpm = 120.0;

    DataChannel ch;
    ch.path = {"Midi", "dev", "Ch2", "N64"};
    ch.durationType = ChannelDurationType::Interval;
    DataEvent note; note.isInterval = true; note.time = 0.50; note.endTime = 3.0; note.value = 90.0f;
    ch.events.push_back(note);
    clip.set.channels.push_back(ch);

    std::vector<SimFiredEvent> fired = simulateClipWindow(clip, /*localBars=*/0.20, /*lastSourceSecs=*/0.0);
    bool pass = fired.size() == 1 && fired[0].kind == SimEventKind::MidiNoteOn &&
                fired[0].channel == 2 && fired[0].number == 64 && fired[0].value == 90;
    ok = ok && pass;
    std::printf("[selftest-dataset] LEG3b noteOn-remap: fired=%zu (want 1) -> %s\n", fired.size(),
                pass ? "PASS" : "FAIL");
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // LEG 3c — mapping guards (pure boundary, non-bug): no mapping → nothing fires (SimulateIoData.cs:105);
  // playhead outside the timeline window → nothing fires (:124); backward scrub → nothing fires (:142).
  // ─────────────────────────────────────────────────────────────────────────────────────────────
  if (!injectBug) {
    SwDataClip clip;
    DataChannel ch; ch.path = {"Midi","dev","Ch1","CC74"}; ch.durationType = ChannelDurationType::Tick;
    DataEvent e; e.time = 0.30; e.value = 50.0f; ch.events.push_back(e);
    clip.set.channels.push_back(ch);

    // (a) no mapping → skip.
    clip.hasMapping = false;
    bool passNoMap = simulateClipWindow(clip, 0.20, 0.0).empty();

    // (b) playhead outside window [1.0, 2.0] with localBars=0.20 → skip.
    clip.hasMapping = true;
    clip.mapping.timeStart = 1.0; clip.mapping.timeEnd = 2.0;
    clip.mapping.sourceStart = 0.0; clip.mapping.sourceEnd = 4.0; clip.mapping.bpm = 120.0;
    bool passOutside = simulateClipWindow(clip, /*localBars=*/0.20, 0.0).empty();

    // (c) backward scrub: window [0,4], last=1.00 secs, current=0.20*2=0.40 secs < last → skip.
    clip.mapping.timeStart = 0.0; clip.mapping.timeEnd = 4.0;
    bool passBackward = simulateClipWindow(clip, /*localBars=*/0.20, /*last=*/1.00).empty();

    bool pass = passNoMap && passOutside && passBackward;
    ok = ok && pass;
    std::printf("[selftest-dataset] LEG3c guards: noMap=%d outside=%d backward=%d -> %s\n",
                passNoMap, passOutside, passBackward, pass ? "PASS" : "FAIL");
  }

  // Harness convention (run_all_selftests.sh --bite): the -bug variant MUST exit non-zero. Each tooth
  // corrupts the REAL core (parse drop / record no-finish / window edge-leak) so a leg diverges → ok=false.
  std::printf("[selftest-dataset] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
