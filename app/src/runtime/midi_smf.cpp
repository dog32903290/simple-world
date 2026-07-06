// runtime/midi_smf — impl of the minimal SMF reader + MidiClip accumulate core. See midi_smf.h.
#include "runtime/midi_smf.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace sw {

bool& midiClipInjectBug() { static bool b = false; return b; }

namespace {

// Big-endian fixed-width readers over a byte cursor. Return false (via the caller's bounds check) on EOF.
uint32_t readU32(const std::vector<uint8_t>& b, size_t& i) {
  uint32_t v = (uint32_t(b[i]) << 24) | (uint32_t(b[i + 1]) << 16) | (uint32_t(b[i + 2]) << 8) | b[i + 3];
  i += 4;
  return v;
}
uint16_t readU16(const std::vector<uint8_t>& b, size_t& i) {
  uint16_t v = uint16_t((uint16_t(b[i]) << 8) | b[i + 1]);
  i += 2;
  return v;
}

// SMF variable-length quantity (7 bits/byte, high bit = continue). Advances `i`. Returns false on EOF.
bool readVlq(const std::vector<uint8_t>& b, size_t& i, size_t end, uint32_t& out) {
  out = 0;
  for (int n = 0; n < 4; ++n) {
    if (i >= end) return false;
    uint8_t byte = b[i++];
    out = (out << 7) | (byte & 0x7F);
    if ((byte & 0x80) == 0) return true;
  }
  return false;  // more than 4 continuation bytes = malformed
}

}  // namespace

bool parseSmf(const std::vector<uint8_t>& b, SmfFile& out) {
  out = SmfFile{};
  size_t i = 0;

  // Header: "MThd" len=6 format ntracks division.
  if (b.size() < 14) return false;
  if (!(b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd')) return false;
  i = 4;
  uint32_t headerLen = readU32(b, i);
  if (headerLen != 6) return false;
  out.format = readU16(b, i);
  out.trackCount = readU16(b, i);
  uint16_t division = readU16(b, i);
  if (division & 0x8000) return false;  // SMPTE division (top bit set) out of scope — MidiClip reads PPQN
  out.deltaTicksPerQuarterNote = division ? division : 480;

  // Track chunks.
  for (int t = 0; t < out.trackCount; ++t) {
    if (i + 8 > b.size()) return false;
    if (!(b[i] == 'M' && b[i + 1] == 'T' && b[i + 2] == 'r' && b[i + 3] == 'k')) return false;
    i += 4;
    uint32_t trackLen = readU32(b, i);
    const size_t trackEnd = i + trackLen;
    if (trackEnd > b.size()) return false;

    int64_t absTick = 0;
    uint8_t runningStatus = 0;

    while (i < trackEnd) {
      uint32_t delta = 0;
      if (!readVlq(b, i, trackEnd, delta)) return false;
      absTick += delta;
      if (i >= trackEnd) return false;

      uint8_t status = b[i];
      if (status & 0x80) {
        ++i;                       // a real status byte
        if (status < 0xF0) runningStatus = status;  // channel messages arm running status
      } else {
        status = runningStatus;    // running status: data byte, reuse last channel status
        if (status == 0) return false;  // data before any status = malformed
      }

      const uint8_t hi = status & 0xF0;
      const int channel = status & 0x0F;

      if (status == 0xFF) {
        // Meta event: FF <type> <len:VLQ> <data>. type 0x2F = End of Track.
        if (i >= trackEnd) return false;
        uint8_t metaType = b[i++];
        uint32_t len = 0;
        if (!readVlq(b, i, trackEnd, len)) return false;
        if (i + len > trackEnd) return false;
        i += len;
        if (metaType == 0x2F) break;  // End of Track
        continue;
      }
      if (status == 0xF0 || status == 0xF7) {
        // SysEx: F0/F7 <len:VLQ> <data>.
        uint32_t len = 0;
        if (!readVlq(b, i, trackEnd, len)) return false;
        if (i + len > trackEnd) return false;
        i += len;
        continue;
      }

      // Channel voice messages. Data-byte count: 2 for most, 1 for ProgramChange (0xC0) / ChannelPressure
      // (0xD0). We only surface Note* / CC; the rest are length-consumed.
      const int dataBytes = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
      if (i + dataBytes > trackEnd) return false;
      const uint8_t d1 = b[i];
      const uint8_t d2 = dataBytes == 2 ? b[i + 1] : 0;
      i += dataBytes;

      if (hi == 0x90 && d2 > 0) {
        out.events.push_back({SmfEvent::NoteOn, absTick, channel, d1, d2});
      } else if (hi == 0x80 || (hi == 0x90 && d2 == 0)) {
        // NoteOff, or NoteOn with velocity 0 (note-off in disguise — NAudio maps both to the base NoteEvent
        // path, MidiClip.cs:169 folds to value 0).
        out.events.push_back({SmfEvent::NoteOff, absTick, channel, d1, 0});
      } else if (hi == 0xB0) {
        out.events.push_back({SmfEvent::ControlChange, absTick, channel, d1, d2});
      }
      // 0xA0 poly-aftertouch / 0xC0 program / 0xD0 pressure / 0xE0 pitchbend: consumed, not surfaced.
    }

    i = trackEnd;  // resync to the declared track end (defensive against a track that ran short)
  }

  return true;
}

std::string smfNoteName(int note) {
  static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int pc = ((note % 12) + 12) % 12;
  // NAudio v2.3.0 (the version TiXL pins, Core/Core.csproj:32) NoteEvent.cs:157: `int octave = noteNumber / 12;`
  // — NO "-1". Middle C (60) → "C5". The scientific-pitch "-1" convention that used to sit here produced
  // "/channelN/C4" keys no TiXL .t3 graph would ever match (2026-07-06 audit, parity bug).
  int octave = note / 12;
  return std::string(kNames[pc]) + std::to_string(octave);
}

void accumulateMidiClip(const SmfFile& file, int64_t timeInTicks, SwFloatDict& out) {
  out.entries.clear();
  const bool skipNorm = midiClipInjectBug();

  // MidiClip folds each track in ascending event index (= ascending tick within a well-formed track). Across
  // tracks, walking the merged event list in file order and letting last-write-win by ascending tick gives
  // the same Dict as MidiClip's per-track UpdateTrack (each key's final value is its newest event <= time).
  // We fold in ascending absoluteTick to make "newest wins" exact regardless of track interleave.
  std::vector<const SmfEvent*> upTo;
  for (const SmfEvent& e : file.events)
    if (e.absoluteTick <= timeInTicks) upTo.push_back(&e);
  std::stable_sort(upTo.begin(), upTo.end(),
                   [](const SmfEvent* a, const SmfEvent* b) { return a->absoluteTick < b->absoluteTick; });

  // Fold into an insertion-ordered SwFloatDict (last write wins on a repeated key — SwFloatDict.tryGet keeps
  // the last, and MidiClip's _channels[key]= overwrites). Set a key's value; if the key already exists we
  // overwrite in place to keep insertion order stable (mirrors Dictionary indexer assignment).
  auto setKey = [&out](const std::string& key, float v) {
    for (auto& kv : out.entries)
      if (kv.first == key) { kv.second = v; return; }
    out.entries.push_back({key, v});
  };

  // Key channel is 1-BASED: NAudio MidiEvent.cs decodes `channel = (status & 0x0F) + 1`, and MidiClip.cs:161
  // interpolates that Channel straight into the key — "/channel1/..". SmfEvent.channel stays the raw 0-based
  // status nibble; the +1 lives only here at key-build time (2026-07-06 audit, parity bug).
  for (const SmfEvent* e : upTo) {
    if (e->kind == SmfEvent::ControlChange) {
      const std::string key = "/channel" + std::to_string(e->channel + 1) + "/controller" + std::to_string(e->number);
      setKey(key, skipNorm ? (float)e->value : e->value / 127.0f);  // MidiClip.cs:186
    } else {
      const std::string key = "/channel" + std::to_string(e->channel + 1) + "/" + smfNoteName(e->number);
      if (e->kind == SmfEvent::NoteOn)
        setKey(key, skipNorm ? (float)e->value : e->value / 127.0f);  // MidiClip.cs:160
      else
        setKey(key, 0.0f);                                            // NoteOff → 0 (MidiClip.cs:173)
    }
  }
}

}  // namespace sw
