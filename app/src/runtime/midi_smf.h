// runtime/midi_smf — a minimal Standard MIDI File (SMF) reader + the MidiClip accumulate core (the MidiClip
// half of the dataset seam). MidiClip.cs (external/tixl/Operators/Lib/io/midi/MidiClip.cs) reads a .mid file
// via NAudio's MidiFile, then as the timeline playhead sweeps it walks each track's events up to the current
// tick and folds NoteOn / NoteOff / ControlChange into a Dict<float> keyed "/channel<n>/<name>" (note) or
// "/channel<n>/controller<id>" (CC). sw has no NAudio, so this file implements the tiny slice of SMF the op
// needs: the header chunk (format / ntracks / division) + track chunks (delta-time VLQ + running-status
// channel events), enough to reconstruct the (absoluteTick, channel, kind, note/controller, value) stream.
//
// SMF FORMAT (the closed-form spec — self-contained, no external authority needed beyond the SMF standard):
//   Header:  "MThd" <len:uint32=6> <format:uint16> <ntracks:uint16> <division:uint16>.
//            division here is ticks-per-quarter-note (top bit 0; SMPTE division top-bit-1 is out of scope —
//            MidiClip reads DeltaTicksPerQuarterNote, the PPQN form).
//   Track:   "MRk"... actually "MTrk" <len:uint32> <event>*  where each event = <delta:VLQ> <message>.
//   VLQ:     variable-length quantity, 7 bits/byte, high bit = "more bytes follow" (big-endian).
//   Message: channel voice (status 0x80..0xEF, running status supported), meta (0xFF <type> <len:VLQ> data),
//            or sysex (0xF0 / 0xF7 <len:VLQ> data). We decode NoteOn/NoteOff/CC (what MidiClip folds); other
//            channel messages are length-skipped; meta/sysex are length-skipped; End-of-Track (FF 2F) ends
//            a track.
//
// NoteName: MidiClip keys note events by NAudio NoteEvent.NoteName. NAudio v2.3.0 (the version TiXL pins,
// Core/Core.csproj:32) NoteEvent.cs:157:
//   name = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}[note % 12] + (note / 12)   // NO "-1"
//   → note 60 = "C5" (middle C), note 61 = "C#5", note 72 = "C6". Reproduced in smfNoteName().
// Key channel is 1-BASED (NAudio MidiEvent.cs: (status & 0x0F) + 1) → "/channel1/.." (MidiClip.cs:161).
//
// runtime leaf: pure computation (byte parse), no GPU/UI/hardware/upward dep.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/dict_op_registry.h"  // SwFloatDict (MidiClip.Values output currency — the existing dict rail)

namespace sw {

// One decoded SMF channel event (the fields MidiClip.UpdateTrack reads, MidiClip.cs:154-196). Meta/sysex are
// consumed during parse but not surfaced (MidiClip ignores them). Time is the ABSOLUTE tick within the file.
struct SmfEvent {
  enum Kind { NoteOn, NoteOff, ControlChange } kind = NoteOn;
  int64_t absoluteTick = 0;  // sum of delta-times up to this event (MidiClip compares against timeInTicks)
  int channel = 0;           // 0-based status nibble; keys are 1-based "/channel<channel+1>/..." (NAudio)
  int number = 0;            // note number (Note*) / controller id (CC)
  int value = 0;             // velocity (Note*) / controller value (CC) — 0..127
};

// The parsed file: the PPQN division + all channel events across all tracks, in per-track order (MidiClip
// walks tracks independently, but for the Dict fold the merged tick-ordered set gives the same result since
// _channels[key] is last-write-wins by ascending tick — see accumulateMidiClip).
struct SmfFile {
  int deltaTicksPerQuarterNote = 480;  // MidiClip _deltaTicksPerQuarterNote (MidiClip.cs:109) — file division
  int format = 0;
  int trackCount = 0;
  std::vector<SmfEvent> events;  // all NoteOn/NoteOff/CC events, ascending absoluteTick within each track
};

// Parse `bytes` (a full .mid file image) into `out`. Returns false on a malformed header / truncated chunk
// (MidiClip wraps SetupMidiFile in try/catch and bails on error, MidiClip.cs:85-88). A well-formed file with
// no channel events yields an empty event list (true).
bool parseSmf(const std::vector<uint8_t>& bytes, SmfFile& out);

// NAudio NoteEvent.NoteName for `note` (0..127): pitch-class name + octave (note/12 - 1). note 60 = "C4".
std::string smfNoteName(int note);

// MidiClip.UpdateTrack fold (MidiClip.cs:131-204): accumulate every event whose absoluteTick <= `timeInTicks`
// into `out` (a SwFloatDict), last-write-wins by ascending tick:
//   NoteOn  → out["/channel<ch>/<NoteName>"]        = velocity / 127            (cs:156-167)
//   NoteOff → out["/channel<ch>/<NoteName>"]        = 0                          (cs:169-181; NoteEvent base)
//   CC      → out["/channel<ch>/controller<id>"]    = controllerValue / 127     (cs:182-195)
// `timeInTicks` = MidiClip's (bars * 4 * deltaTicksPerQuarterNote) — the caller (the clip cook / golden)
// converts the timeline playhead to ticks; here we take ticks directly so the fold is closed-form testable.
// Events are folded in ascending absoluteTick so a later same-key event overwrites an earlier one (a NoteOff
// after a NoteOn zeroes the channel key — the observable MidiClip behaviour).
void accumulateMidiClip(const SmfFile& file, int64_t timeInTicks, SwFloatDict& out);

// Test-only injection seam (golden): when true, accumulateMidiClip SKIPS the /127 normalisation (writes the
// raw velocity/cc instead) so a golden asserting the normalised value goes RED on the real fold path. Bites
// the value transform, not by flipping the expected value.
bool& midiClipInjectBug();

}  // namespace sw
