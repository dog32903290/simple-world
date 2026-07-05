// platform/psn_packet — impl. See psn_packet.h for the wire spec + TiXL line citations.
#include "platform/psn_packet.h"

#include <cstring>

namespace sw {

namespace {

void writeU32LE(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back((uint8_t)(v & 0xFF));
  b.push_back((uint8_t)((v >> 8) & 0xFF));
  b.push_back((uint8_t)((v >> 16) & 0xFF));
  b.push_back((uint8_t)((v >> 24) & 0xFF));
}

void writeU64LE(std::vector<uint8_t>& b, uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
}

void writeFloatLE(std::vector<uint8_t>& b, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  writeU32LE(b, bits);
}

uint32_t readU32LE(const std::vector<uint8_t>& b, size_t off) {
  return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) |
         ((uint32_t)b[off + 3] << 24);
}

float readFloatLE(const std::vector<uint8_t>& b, size_t off) {
  uint32_t bits = readU32LE(b, off);
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

// PSN chunk header = id | (length<<16) | (hasSubChunks<<31), little-endian u32 (WriteChunkHeader :312-318).
void writeChunkHeader(std::vector<uint8_t>& b, uint16_t id, uint16_t length, bool hasSubChunks) {
  uint32_t header = id;
  header |= (uint32_t)length << 16;
  if (hasSubChunks) header |= 1u << 31;
  writeU32LE(b, header);
}

}  // namespace

std::vector<uint8_t> buildPsnDataPacket(const std::vector<PsnTracker>& trackers,
                                        uint8_t frameId, uint64_t timestampMicros) {
  // ── Inner packet stream (everything inside the root 0x6755 wrapper) ────────────────────────────
  std::vector<uint8_t> packet;

  // Header chunk 0x0000, 12 bytes, no sub-chunks (cs:155-160): u64 timestamp, ver 2.3, frameId, count 1.
  writeChunkHeader(packet, 0x0000, 12, /*hasSubChunks*/ false);
  writeU64LE(packet, timestampMicros);
  packet.push_back(2);        // version high (cs:157)
  packet.push_back(3);        // version low  (cs:158)
  packet.push_back(frameId);  // frame id      (cs:159)
  packet.push_back(1);        // frame packet count (cs:160)

  // Tracker-list stream (cs:163-191): concatenation of per-tracker sub-chunks.
  std::vector<uint8_t> trackerList;
  for (size_t i = 0; i < trackers.size(); ++i) {
    const auto& t = trackers[i];

    // Per-tracker data stream: position 0x0000 (12B) + orientation 0x0002 (12B) (cs:172-186).
    std::vector<uint8_t> trackerData;
    writeChunkHeader(trackerData, kPsnPosFieldId, 12, false);
    writeFloatLE(trackerData, t.posX);
    writeFloatLE(trackerData, t.posY);
    writeFloatLE(trackerData, -t.posZ);  // Z-axis flip (cs:179)
    writeChunkHeader(trackerData, kPsnOriFieldId, 12, false);
    writeFloatLE(trackerData, t.oriX);
    writeFloatLE(trackerData, t.oriY);
    writeFloatLE(trackerData, -t.oriZ);  // Z-axis flip (cs:186)

    // Tracker sub-chunk: id=trackerId, has sub-chunks, length = data length (cs:189-190).
    writeChunkHeader(trackerList, t.id, (uint16_t)trackerData.size(), true);
    trackerList.insert(trackerList.end(), trackerData.begin(), trackerData.end());
  }

  // Tracker-list chunk 0x0001, has sub-chunks (cs:194-195).
  writeChunkHeader(packet, kPsnListId, (uint16_t)trackerList.size(), true);
  packet.insert(packet.end(), trackerList.begin(), trackerList.end());

  // ── Root 0x6755 wrapper (cs:198-203) ──────────────────────────────────────────────────────────
  std::vector<uint8_t> finalPacket;
  writeChunkHeader(finalPacket, kPsnRootDataId, (uint16_t)packet.size(), true);
  finalPacket.insert(finalPacket.end(), packet.begin(), packet.end());
  return finalPacket;
}

PsnParsed parsePsnDataPacket(const std::vector<uint8_t>& data) {
  PsnParsed out;
  const size_t len = data.size();
  if (len < 4) return out;

  // Root header (cs:183): validate low 16 bits == 0x6755.
  uint32_t rootHeader = readU32LE(data, 0);
  if ((rootHeader & 0xFFFF) != kPsnRootDataId) return out;
  size_t pos = 4;  // BinaryReader consumed the root header (cs:183); it then walks the REMAINDER.

  // Walk top-level chunks (cs:186-233). Each chunk header is 4 bytes; length excludes the header.
  while (pos + 4 <= len) {
    uint32_t header = readU32LE(data, pos);
    pos += 4;
    uint16_t id = (uint16_t)(header & 0xFFFF);
    uint16_t chunkLen = (uint16_t)((header >> 16) & 0x7FFF);
    size_t nextChunk = pos + chunkLen;
    if (nextChunk > len) break;

    if (id == kPsnListId) {  // tracker list (cs:194)
      size_t tpos = pos;
      while (tpos + 4 <= nextChunk) {
        uint32_t th = readU32LE(data, tpos);
        tpos += 4;
        uint16_t trackerId = (uint16_t)(th & 0xFFFF);
        uint16_t trackerLen = (uint16_t)((th >> 16) & 0x7FFF);
        size_t trackerEnd = tpos + trackerLen;
        if (trackerEnd > nextChunk) break;

        PsnTracker tr;
        tr.id = trackerId;
        size_t fpos = tpos;
        while (fpos + 4 <= trackerEnd) {  // per-field walk (cs:206)
          uint32_t fh = readU32LE(data, fpos);
          fpos += 4;
          uint16_t fieldId = (uint16_t)(fh & 0xFFFF);
          uint16_t fieldLen = (uint16_t)((fh >> 16) & 0x7FFF);
          if (fpos + fieldLen > trackerEnd) break;
          if (fieldId == kPsnPosFieldId && fieldLen >= 12) {   // position (cs:217)
            tr.posX = readFloatLE(data, fpos);
            tr.posY = readFloatLE(data, fpos + 4);
            tr.posZ = readFloatLE(data, fpos + 8);
          } else if (fieldId == kPsnOriFieldId && fieldLen >= 12) {  // orientation (cs:220)
            tr.oriX = readFloatLE(data, fpos);
            tr.oriY = readFloatLE(data, fpos + 4);
            tr.oriZ = readFloatLE(data, fpos + 8);
          }
          fpos += fieldLen;  // default: skip field body (cs:224)
        }
        out.trackers.push_back(tr);
        tpos = trackerEnd;  // (cs:229)
      }
    }
    pos = nextChunk;  // (cs:232)
  }

  out.ok = true;
  return out;
}

}  // namespace sw
