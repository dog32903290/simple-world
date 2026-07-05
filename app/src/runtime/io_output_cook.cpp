// runtime/io_output_cook — the remaining MIDI/OSC OUTPUT-node cooks (MidiOutput / MidiNoteOutput /
// MidiPitchbendOutput / MidiTriggerOutput / MidiSysexOutput / OscOutput). Split from io_node_cook.cpp
// (rule 4 line-count) — same frozen output pattern: resolve inputs, evaluate the TiXL send condition
// (rising edge / continuous), build the EXACT short message TiXL's GetAsShortMessage() produces, emit
// it to the device out bus (io_device_bus), echo a golden probe onto extOut[0].
//
// TiXL authority (Operators/Lib/io/midi/*.cs, Operators/Lib/io/osc/OscOutput.cs, read-only). All NAMED
// FORK fork-midioutput-shared-transport: the app owns ONE CoreMIDI destination / UDP sender (deferred-
// hw-verify), so the per-Device-name select in TiXL's MidiOutsWithDevices loop drops here.
//
// runtime leaf: pure computation, emits to the runtime out bus, no hardware / no UI / no upward dep.
#include "runtime/io_node_cook.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/io_device_bus.h"
#include "runtime/resident_eval_graph.h"

namespace sw {
namespace {

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// TiXL NAudio PitchWheelChangeEvent short message: status 0xE0|(ch-1), data1 = pitch & 0x7F (LSB),
// data2 = (pitch >> 7) & 0x7F (MSB). pitch is the 14-bit unsigned value (0..16383, center 8192).
void emitPitchWheel(int channel, int pitch14) {
  emitMidiShort(0xE0 | (channel - 1), pitch14 & 0x7F, (pitch14 >> 7) & 0x7F, 3);
}

// Resolve the SendMode(0 continuous/whileTriggered vs edge) + edge helpers shared by the note-style
// outputs. Returns the rising/falling edge flags and updates the latch.
struct Edge { bool active, justOn, justOff; };
Edge edgeOf(bool trigActive, bool& latch) {
  Edge e{trigActive, trigActive && !latch, !trigActive && latch};
  latch = trigActive;
  return e;
}

}  // namespace

// ── MidiOutput ─── TiXL MidiOutput.cs. Note/CC/sequence sender. SendModes: 0 Notes_FixedDuration,
// 1 Note_WhileTriggered, 2 ControllerChange, 3 StartSequence, 4 StopSequence, 5 ContinueSequence,
// 6 TempoEvent. velocity = Velocity(float 0..1)*127 unless Velocity127>=0 (cs:35-40). NoteOrController
// clamped 0..127. TempoEvent → NAudio TempoEvent (a META event, NOT a short channel message — no
// GetAsShortMessage on the wire; NAMED FORK fork-midioutput-tempo-meta-dropped: sw emits nothing for
// TempoEvent, echo 0, since a MIDI TEMPO meta has no 3-byte short form — documented, not faked).
void cookMidiOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiNoteOutputState>& state,
                         const IoOutClock& clk) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "MidiOutput") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const int sendMode = (int)std::lround(P["SendMode"]);
    const bool trigActive = P["TriggerSend"] > 0.5f;
    int channel = clampi((int)std::lround(P["ChannelNumber"]), 1, 16);
    int noteOrCtl = clampi((int)std::lround(P["NoteOrController"]), 0, 127);
    int velocity = clampi((int)(std::fmin(std::fmax(P["Velocity"], 0.0f), 1.0f) * 127.0f), 0, 127);
    const int velo127 = (int)std::lround(P["Velocity127"]);
    if (velo127 >= 0) velocity = clampi(velo127, 0, 127);
    const int durationMs = clampi((int)(P["DurationInSecs"] * 1000.0f), 1, 100000);
    const double absMs = clk.runTimeSecs * 1000.0;

    MidiNoteOutputState& st = state[rn.path];
    Edge e = edgeOf(trigActive, st.triggered);
    float echo = 0.0f;
    switch (sendMode) {
      case 1:  // Note_WhileTriggered: NoteOn on edge-up, NoteOff (vel 0) on edge-down
        if (e.justOn)  { emitMidiShort(0x90 | (channel - 1), noteOrCtl, velocity, 3); echo = (float)velocity; }
        else if (e.justOff) { emitMidiShort(0x90 | (channel - 1), noteOrCtl, 0, 3); echo = 0.0f; }
        break;
      case 0:  // Notes_FixedDuration: NoteOn on edge-up (stamp time), NoteOff after duration elapses
        if (e.justOn) {
          emitMidiShort(0x90 | (channel - 1), noteOrCtl, velocity, 3);
          st.lastNoteOnMs = absMs; st.offNote = noteOrCtl; st.offStatus = 0x90 | (channel - 1); st.haveOff = true;
          echo = (float)velocity;
        } else if (st.haveOff && absMs - st.lastNoteOnMs > durationMs) {
          emitMidiShort(st.offStatus, st.offNote, 0, 3); st.haveOff = false;
        }
        break;
      case 2:  // ControllerChange (continuous)
        emitMidiShort(0xB0 | (channel - 1), noteOrCtl, velocity, 3); echo = (float)velocity; break;
      case 3:  emitMidiShort(0xFA, 0, 0, 1); break;  // StartSequence
      case 4:  emitMidiShort(0xFC, 0, 0, 1); break;  // StopSequence
      case 5:  emitMidiShort(0xFB, 0, 0, 1); break;  // ContinueSequence
      case 6:  break;  // TempoEvent: no short form (fork-midioutput-tempo-meta-dropped)
      default: break;
    }
    rn.extOut[0] = echo;
  }
}

// ── MidiNoteOutput ─── TiXL MidiNoteOutput.cs. SendModes: 0 Note_FixedDuration, 1 Note_WhileTriggered.
// velocity = VelocityFloat*127 (UseVelocityFloat) else Velocity 0..127 (cs:41-48).
void cookMidiNoteOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiNoteOutputState>& state,
                             const IoOutClock& clk) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "MidiNoteOutput") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const int sendMode = (int)std::lround(P["SendMode"]);
    const bool trigActive = P["TriggerSend"] > 0.5f;
    int channel = clampi((int)std::lround(P["ChannelNumber"]), 1, 16);
    int note = clampi((int)std::lround(P["NoteNumber"]), 0, 127);
    const bool useFloatV = P["UseVelocityFloat"] > 0.5f;
    int velocity = useFloatV ? clampi((int)(std::fmin(std::fmax(P["VelocityFloat"], 0.0f), 1.0f) * 127.0f), 0, 127)
                             : clampi((int)std::lround(P["Velocity"]), 0, 127);
    const int durationMs = clampi((int)(P["DurationInSecs"] * 1000.0f), 1, 100000);
    const double absMs = clk.runTimeSecs * 1000.0;

    MidiNoteOutputState& st = state[rn.path];
    Edge e = edgeOf(trigActive, st.triggered);
    float echo = 0.0f;
    if (sendMode == 1) {  // Note_WhileTriggered
      if (e.justOn) { emitMidiShort(0x90 | (channel - 1), note, velocity, 3); st.offNote = note; st.offStatus = 0x90 | (channel - 1); st.haveOff = true; echo = (float)velocity; }
      else if (e.justOff && st.haveOff) { emitMidiShort(st.offStatus, st.offNote, 0, 3); st.haveOff = false; }
    } else {  // Note_FixedDuration
      if (e.justOn) {
        if (st.haveOff) { emitMidiShort(st.offStatus, st.offNote, 0, 3); }  // flush pending off (cs:103-107)
        emitMidiShort(0x90 | (channel - 1), note, velocity, 3);
        st.lastNoteOnMs = absMs; st.offNote = note; st.offStatus = 0x90 | (channel - 1); st.haveOff = true;
        echo = (float)velocity;
      } else if (st.haveOff && absMs - st.lastNoteOnMs > durationMs) {
        emitMidiShort(st.offStatus, st.offNote, 0, 3); st.haveOff = false;
      }
    }
    rn.extOut[0] = echo;
  }
}

// ── MidiPitchbendOutput ─── TiXL MidiPitchbendOutput.cs. intPitch = Pitch(-8192..8191)+8192 or
// (PitchFloat(-1..1)*8192 clamped)+8192 (cs:37/41). SendModes: 0 SendContinuously, 1 SendWhenTriggered.
void cookMidiPitchbendOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "MidiPitchbendOutput") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const int sendMode = (int)std::lround(P["SendMode"]);
    const bool trigActive = P["TriggerSend"] > 0.5f;
    int channel = clampi((int)std::lround(P["ChannelNumber"]), 1, 16);
    const bool useFloat = P["UsePitchFloat"] > 0.5f;
    int intPitch = useFloat
        ? clampi((int)(std::fmin(std::fmax(P["PitchFloat"], -1.0f), 1.0f) * 8192.0), -8192, 8191) + 8192
        : clampi((int)std::lround(P["Pitch"]), -8192, 8191) + 8192;
    if (ioNodeBug() == 2) intPitch = useFloat ? (int)P["PitchFloat"] + 8192 : (int)std::lround(P["Pitch"]) + 8192;  // drop clamp

    MidiOutputState& st = state[rn.path];
    const bool justOn = trigActive && !st.triggered; st.triggered = trigActive;
    bool doSend = (sendMode == 0) || (sendMode == 1 && justOn);
    if (ioNodeBug() == 1) doSend = (sendMode == 0) || (sendMode == 1 && trigActive);  // drop edge
    float echo = 0.0f;
    if (doSend) { emitPitchWheel(channel, intPitch); echo = (float)intPitch; }
    rn.extOut[0] = echo;
  }
}

// ── MidiTriggerOutput ─── TiXL MidiTriggerOutput.cs. Five independent rising-edge triggers → Program
// Change (0xC0|ch-1, program) / Start(0xFA) / Stop(0xFC) / Continue(0xFB) / TempoEvent(meta, dropped).
void cookMidiTriggerOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiTriggerOutputState>& state,
                                const IoOutClock& /*clk*/) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "MidiTriggerOutput") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    int channel = clampi((int)std::lround(P["ChannelNumber"]), 1, 16);
    int program = clampi((int)std::lround(P["ProgramChangeNumber"]), 0, 127);
    const bool tPC = P["TriggerProgramChange"] > 0.5f, tStart = P["TriggerStart"] > 0.5f;
    const bool tStop = P["TriggerStop"] > 0.5f, tCont = P["TriggerContinue"] > 0.5f;

    MidiTriggerOutputState& st = state[rn.path];
    // Each trigger fires ONLY on its own rising edge (cs:56-98). ioNodeBug 1 drops the edge (level-fire).
    auto rise = [&](bool now, bool& latch) { bool r = ioNodeBug() == 1 ? now : (now && !latch); latch = now; return r; };
    float echo = 0.0f;
    if (rise(tPC, st.pc))     { emitMidiShort(0xC0 | (channel - 1), program, 0, 2); echo = (float)program; }
    if (rise(tStart, st.start)) { emitMidiShort(0xFA, 0, 0, 1); }
    if (rise(tStop, st.stop))   { emitMidiShort(0xFC, 0, 0, 1); }
    if (rise(tCont, st.cont))   { emitMidiShort(0xFB, 0, 0, 1); }
    rise(P["TriggerTempoEvent"] > 0.5f, st.tempo);  // TempoEvent meta dropped (advance latch only)
    rn.extOut[0] = echo;
  }
}

// ── MidiSysexOutput ─── TiXL MidiSysexOutput.cs. On a rising TriggerSend edge, parse SysexString as
// space-separated hex bytes (cs:66-75) and emit the SysEx buffer. Non-hex tokens skipped (TiXL catches
// the parse exception per token). echo = byte count.
void cookMidiSysexOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "MidiSysexOutput") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const bool trigActive = P["TriggerSend"] > 0.5f;
    std::string hex;
    auto it = rn.strInputs.find("SysexString");
    if (it != rn.strInputs.end()) hex = it->second;

    MidiOutputState& st = state[rn.path];
    const bool justOn = trigActive && !st.triggered; st.triggered = trigActive;
    const bool doSend = ioNodeBug() == 1 ? trigActive : justOn;  // bug: level-fire
    float echo = 0.0f;
    if (doSend) {
      std::vector<uint8_t> bytes;
      size_t i = 0;
      while (i < hex.size()) {
        while (i < hex.size() && hex[i] == ' ') ++i;
        size_t j = i; while (j < hex.size() && hex[j] != ' ') ++j;
        if (j > i) {
          const std::string tok = hex.substr(i, j - i);
          char* end = nullptr;
          long v = std::strtol(tok.c_str(), &end, 16);
          if (end && *end == '\0' && v >= 0 && v <= 255) bytes.push_back((uint8_t)v);  // byte.Parse hex
        }
        i = j;
      }
      emitMidiSysex(bytes);
      echo = (float)bytes.size();
    }
    rn.extOut[0] = echo;
  }
}

// ── OscOutput (PARTIAL) ─── TiXL OscOutput.cs. shouldSend = SendTrigger && (somethingHasChanged ||
// !OnlySendChanges) (cs:273). Sends OscMessage(Address, parameters). PARTIAL: TiXL's Values/IntValues/
// Strings are MultiInput LISTS (dict/list-currency seam) — this ships the SINGLE-value path only: one
// scalar Value float → OscMessage(address, [value]). The MultiInput list path is DEFERRED (TODO
// seam/dict-currency). change-detect (somethingHasChanged) simplified to the OnlySendChanges gate on
// the single value's delta. echo = the sent value.
void cookOscOutputNodes(ResidentEvalGraph& g, std::map<std::string, MidiOutputState>& state) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "OscOutput") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const bool sendTrigger = P["SendTrigger"] > 0.5f;
    const bool onlyChanges = P["OnlySendChanges"] > 0.5f;
    const float value = P["Value"];
    std::string addr;
    auto it = rn.strInputs.find("Address");
    if (it != rn.strInputs.end()) addr = it->second;

    MidiOutputState& st = state[rn.path];  // reuse the bool latch as "hasSentBefore"; offNote-less
    // somethingHasChanged: the single value changed since last send (host-side delta). We stash the
    // last value in the golden-probe channel isn't available here, so approximate with the latch: first
    // send always fires; subsequent sends gated by OnlySendChanges are DEFERRED to the list seam (a
    // faithful single-value change-detect needs a stored prior value — parked with the MultiInput list).
    (void)onlyChanges;
    bool doSend = sendTrigger;  // TODO(seam/dict-currency): && (valueChanged || !onlyChanges) once the
                                // prior-value store rides the list seam; single-value ships trigger-gated.
    float echo = 0.0f;
    if (doSend && !addr.empty()) { emitOscMessage(addr, {value}); echo = value; }
    st.triggered = sendTrigger;
    rn.extOut[0] = echo;
  }
}

}  // namespace sw
