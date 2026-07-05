// platform/psn_packet — pure closed-form codec for the PosiStage / PSN tracking family
// (PosiStageInput parse / PosiStageOutput build). Same net_loopback UDP-multicast transport as the other
// io families; the VALUE is the PSN chunk byte layout — a nested TLV builder/parser. Pure functions keep
// the golden byte-for-byte independent of the socket (deferred-hw-verify), like platform/dmx_packet and
// platform/freed_packet.
//
// PSN chunk header (both files): a little-endian uint32 = id(16) | length(15) | hasSubChunks(bit31).
//   header = id | (length << 16) | (hasSubChunks ? 1u<<31 : 0)   (PosiStageOutput.cs WriteChunkHeader
//   :312-318 / PosiStageInput.cs decode :188-190). PSN_DATA root chunk id = 0x6755; PSN_INFO = 0x6756.
//   All payload floats are IEEE-754 LE (BinaryWriter.Write(float)).
//
// platform leaf: pure computation (std::vector<uint8_t>), no sockets, no runtime/UI, no upward dep.
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/posistage/PosiStageOutput.cs:149-204  BuildPsnDataPacket — root 0x6755 → header
//     chunk 0x0000 (timestamp u64 + ver 2.3 + frameId + count) + tracker-list chunk 0x0001 → per-tracker
//     sub-chunk (id=trackerId, subChunks) → position 0x0000 (X,Y,-Z) + orientation 0x0002 (axisX,axisY,-axisZ).
//   Operators/Lib/io/posistage/PosiStageInput.cs:179-234   ParsePsnDataPacket — validate root & 0x6755,
//     walk chunks; in list chunk 0x0001 walk trackers; per tracker read field 0x0000 (pos, 3 floats) +
//     0x0002 (orientation axis-angle, 3 floats).
#pragma once
#include <cstdint>
#include <vector>

namespace sw {

// PSN wire constants.
constexpr uint16_t kPsnRootDataId = 0x6755;  // PSN_DATA root chunk (PosiStageOutput.cs:200 / Input :184)
constexpr uint16_t kPsnRootInfoId = 0x6756;  // PSN_INFO root chunk (PosiStageOutput.cs:244)
constexpr uint16_t kPsnListId     = 0x0001;  // tracker-list chunk (Input.cs:194)
constexpr uint16_t kPsnPosFieldId = 0x0000;  // position field within a tracker (Input.cs:217)
constexpr uint16_t kPsnOriFieldId = 0x0002;  // orientation field within a tracker (Input.cs:220)

// One tracker's decoded state (position metres, orientation as an axis-angle vector — the PSN wire form).
struct PsnTracker {
  uint16_t id = 0;
  float posX = 0, posY = 0, posZ = 0;      // metres
  float oriX = 0, oriY = 0, oriZ = 0;      // axis-angle vector (radians·axis)
};

// Build one PSN_DATA packet (PosiStageOutput.cs BuildPsnDataPacket :149-204). `frameId` is the per-frame
// counter byte; `timestampMicros` is the u64 header timestamp (=stopwatch µs; a golden pins it to a fixed
// value). Each tracker is emitted with its position (X,Y,Z) and orientation axis-angle (oriX,oriY,oriZ)
// WITH the cs:179/186 Z-axis flip applied to posZ and oriZ. Trackers keep input order; PSN ids start at 0
// on the wire (cs:168) — the caller supplies the id it wants stamped (== index for the round-trip golden).
std::vector<uint8_t> buildPsnDataPacket(const std::vector<PsnTracker>& trackers,
                                        uint8_t frameId, uint64_t timestampMicros);

// The parsed contents of a PSN_DATA packet (PosiStageInput.cs ParsePsnDataPacket :179-234). `ok` false =
// not a valid PSN_DATA (short / wrong root magic). posZ/oriZ come back WITH the on-wire sign (the parser
// does NOT flip — it stores the wire bytes; the Z-flip is a sender-side convention, so build→parse
// round-trips the FLIPPED z). Trackers are returned in wire order.
struct PsnParsed {
  bool ok = false;
  std::vector<PsnTracker> trackers;
};
PsnParsed parsePsnDataPacket(const std::vector<uint8_t>& packet);

}  // namespace sw
