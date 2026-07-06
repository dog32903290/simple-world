// midiclip_golden — --selftest-midiclip. THE MidiClip SMF-reader golden (dataset seam, MidiClip half). Proves
// the minimal Standard MIDI File reader (parseSmf) + the MidiClip Dict fold (accumulateMidiClip) against a
// HAND-BUILT minimal SMF byte buffer and the closed-form SMF spec + MidiClip.cs value math.
//
// THE FIXTURE (a format-0 SMF, 1 track, PPQN division 480), hand-assembled byte-by-byte below:
//   tick 0   : NoteOn  ch0 note 60 (C5) vel 100   → key "/channel1/C5"          value 100/127 = 0.7874
//   tick 0   : CC      ch0 controller 74 val 64   → key "/channel1/controller74" value 64/127 = 0.5039
//   tick 240 : NoteOn  ch1 note 67 (G5) vel 127   → key "/channel2/G5"           value 127/127 = 1.0
//   tick 480 : NoteOff ch0 note 60 (C5)           → key "/channel1/C5"           value 0 (overwrites)
//
// EXPECTED (hand-derived, impl-independent — GOLDEN_STANDARD three-features):
//   • parse: division 480; 4 channel events at the ticks above; note-name 60→"C5",67→"G5" (NAudio algorithm).
//   • fold @ tick 300 (BETWEEN the G5 and the NoteOff): C5=0.7874 (still on), controller74=0.5039, G5=1.0.
//   • fold @ tick 500 (AFTER the NoteOff): C5=0.0 (NoteOff overwrote), controller74=0.5039, G5=1.0.
//   The two folds sit at DIVERGENT playhead points (one before, one after the note-off) so the tick-window +
//   last-write-wins are exercised (P2: a fold that ignored the NoteOff, or used the wrong tick gate, reddens).
//
// TOOTH (one real transform seam, NOT want-flip): midiClipInjectBug() skips the /127 normalisation → the fold
// writes raw velocity/cc → the normalised assert (0.7874) diverges. Bites the REAL value transform.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/dict_op_registry.h"  // SwFloatDict
#include "runtime/midi_smf.h"

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// Append a variable-length quantity (SMF VLQ) to `b`.
void putVlq(std::vector<uint8_t>& b, uint32_t v) {
  uint8_t stack[4];
  int n = 0;
  stack[n++] = v & 0x7F;
  v >>= 7;
  while (v) { stack[n++] = (v & 0x7F) | 0x80; v >>= 7; }
  for (int k = n - 1; k >= 0; --k) b.push_back(stack[k]);  // big-endian, high bit on all but the last
}
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back((v >> 24) & 0xFF); b.push_back((v >> 16) & 0xFF);
  b.push_back((v >> 8) & 0xFF);  b.push_back(v & 0xFF);
}
void putU16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((v >> 8) & 0xFF); b.push_back(v & 0xFF); }

// Build the fixture SMF image (see header). Returns the raw .mid bytes.
std::vector<uint8_t> buildFixtureSmf() {
  // --- track body first (so we can prefix its byte length) ---
  std::vector<uint8_t> trk;
  // tick 0: NoteOn ch0 note60 vel100  (status 0x90, delta 0)
  putVlq(trk, 0);   trk.push_back(0x90); trk.push_back(60);  trk.push_back(100);
  // tick 0: CC ch0 controller74 val64  (status 0xB0, delta 0)
  putVlq(trk, 0);   trk.push_back(0xB0); trk.push_back(74);  trk.push_back(64);
  // tick 240: NoteOn ch1 note67 vel127 (status 0x91, delta 240)
  putVlq(trk, 240); trk.push_back(0x91); trk.push_back(67);  trk.push_back(127);
  // tick 480: NoteOff ch0 note60       (status 0x80, delta 240 from tick 240)
  putVlq(trk, 240); trk.push_back(0x80); trk.push_back(60);  trk.push_back(0);
  // End of Track meta (FF 2F 00), delta 0.
  putVlq(trk, 0);   trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0x00);

  // --- assemble the file ---
  std::vector<uint8_t> b;
  b.push_back('M'); b.push_back('T'); b.push_back('h'); b.push_back('d');
  putU32(b, 6);        // header length
  putU16(b, 0);        // format 0
  putU16(b, 1);        // ntracks 1
  putU16(b, 480);      // division = 480 PPQN
  b.push_back('M'); b.push_back('T'); b.push_back('r'); b.push_back('k');
  putU32(b, (uint32_t)trk.size());
  b.insert(b.end(), trk.begin(), trk.end());
  return b;
}

float get(const SwFloatDict& d, const std::string& key) {
  float v = -999.0f;
  d.tryGet(key, v);
  return v;
}

}  // namespace

int runMidiClipSelfTest(bool injectBug) {
  bool ok = true;

  const std::vector<uint8_t> smf = buildFixtureSmf();

  // ── LEG 1 — parseSmf: the reader reconstructs division + the 4 channel events with correct ticks. ──
  {
    SmfFile f;
    bool okParse = parseSmf(smf, f);
    // FIXED wants: division 480, 4 events (2 NoteOn, 1 CC, 1 NoteOff), ticks 0/0/240/480. No teeth here
    // (a corrupt parse would already redden LEG 2/3); pure structural boundary — only run on the clean pass.
    bool pass = okParse && f.deltaTicksPerQuarterNote == 480 && f.events.size() == 4;
    if (pass) {
      // Locate by kind+tick (parse preserves per-track order → insertion order here).
      pass = f.events[0].kind == SmfEvent::NoteOn && f.events[0].absoluteTick == 0 &&
             f.events[0].channel == 0 && f.events[0].number == 60 && f.events[0].value == 100 &&
             f.events[1].kind == SmfEvent::ControlChange && f.events[1].absoluteTick == 0 &&
             f.events[1].channel == 0 && f.events[1].number == 74 && f.events[1].value == 64 &&
             f.events[2].kind == SmfEvent::NoteOn && f.events[2].absoluteTick == 240 &&
             f.events[2].channel == 1 && f.events[2].number == 67 && f.events[2].value == 127 &&
             f.events[3].kind == SmfEvent::NoteOff && f.events[3].absoluteTick == 480 &&
             f.events[3].channel == 0 && f.events[3].number == 60;
    }
    if (injectBug) pass = true;  // LEG 1 is structural (no tooth); the bug variant relies on LEG 2/3 to redden.
    ok = ok && pass;
    std::printf("[selftest-midiclip] LEG1 parse: division=%d events=%zu -> %s\n",
                f.deltaTicksPerQuarterNote, f.events.size(), pass ? "PASS" : "FAIL");
  }

  // ── LEG 1b — smfNoteName: NAudio v2.3.0 note→name, octave = note/12 with NO "-1" (NoteEvent.cs:157):
  //    60→C5, 61→C#5, 67→G5, 72→C6, 69→A5. Closed-form spec anchor, non-bug. ──
  if (!injectBug) {
    bool pass = smfNoteName(60) == "C5" && smfNoteName(61) == "C#5" &&
                smfNoteName(67) == "G5" && smfNoteName(72) == "C6" && smfNoteName(69) == "A5";
    ok = ok && pass;
    std::printf("[selftest-midiclip] LEG1b noteName: 60=%s 67=%s -> %s\n",
                smfNoteName(60).c_str(), smfNoteName(67).c_str(), pass ? "PASS" : "FAIL");
  }

  SmfFile f;
  parseSmf(smf, f);

  // ── LEG 2 — fold @ tick 300 (before the NoteOff): C5 still on (0.7874), controller74 0.5039, G5 1.0. The
  //    /127 normalisation tooth rides C5's value. FIXED want (P3-safe); -bug (skip norm) → 100 ≠ 0.7874. ──
  {
    midiClipInjectBug() = injectBug;
    SwFloatDict d;
    accumulateMidiClip(f, /*timeInTicks=*/300, d);
    midiClipInjectBug() = false;

    float c4 = get(d, "/channel1/C5");
    float cc = get(d, "/channel1/controller74");
    float g4 = get(d, "/channel2/G5");
    bool pass = nearf(c4, 100.0f / 127.0f) && nearf(cc, 64.0f / 127.0f) && nearf(g4, 1.0f);
    ok = ok && pass;
    std::printf("[selftest-midiclip] LEG2 fold@300: C5=%.4f (want 0.7874) cc=%.4f G5=%.4f -> %s\n",
                c4, cc, g4, pass ? "PASS" : "FAIL");
  }

  // ── LEG 3 — fold @ tick 500 (after the NoteOff): C5 overwritten to 0.0; controller74 0.5039; G5 1.0. The
  //    NoteOff-overwrites-NoteOn behaviour is load-bearing (a fold ignoring NoteOff would leave C5=0.7874).
  //    Tooth rides controller74 (0.5039) so -bug reddens here too even though C5 is 0 both ways. ──
  {
    midiClipInjectBug() = injectBug;
    SwFloatDict d;
    accumulateMidiClip(f, /*timeInTicks=*/500, d);
    midiClipInjectBug() = false;

    float c4 = get(d, "/channel1/C5");
    float cc = get(d, "/channel1/controller74");
    float g4 = get(d, "/channel2/G5");
    bool pass = nearf(c4, 0.0f) && nearf(cc, 64.0f / 127.0f) && nearf(g4, 1.0f);
    ok = ok && pass;
    std::printf("[selftest-midiclip] LEG3 fold@500: C5=%.4f (want 0) cc=%.4f (want 0.5039) G5=%.4f -> %s\n",
                c4, cc, g4, pass ? "PASS" : "FAIL");
  }

  // ── LEG 3b — fold @ tick 100 (before G5 arrives at 240): only C5 + controller74 present, NO G5 key. The
  //    tick gate (event.absoluteTick <= time) is load-bearing (a fold ignoring the gate would include G5).
  //    Pure boundary, non-bug. ──
  if (!injectBug) {
    SwFloatDict d;
    accumulateMidiClip(f, /*timeInTicks=*/100, d);
    bool hasG5 = d.has("/channel2/G5");
    bool pass = !hasG5 && nearf(get(d, "/channel1/C5"), 100.0f / 127.0f) && d.count() == 2;
    ok = ok && pass;
    std::printf("[selftest-midiclip] LEG3b fold@100: keys=%zu G5present=%d -> %s\n",
                d.count(), hasG5, pass ? "PASS" : "FAIL");
  }

  std::printf("[selftest-midiclip] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
