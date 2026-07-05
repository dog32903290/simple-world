// platform/dmx_packet — pure closed-form packet codec for the DMX-over-network family (ArtnetOutput /
// ArtnetInput / SacnOutput / SacnInput / DMXOutput serial frame). The socket transport is the same thin
// net_loopback UDP seam; the VALUE of these ops is the byte layout of the packet they assemble/parse,
// which is a deterministic buffer builder (like wled_frame.h for Adalight). Exposing it as pure
// functions keeps the golden byte-for-byte independent of the transport — a golden hand-推s the exact
// wire bytes from the TiXL .cs and asserts them; the real socket send/receive (deferred-hw-verify) just
// hands these bytes to net_loopback.
//
// platform leaf: pure computation (std::vector<uint8_t>), no sockets, no runtime/UI, no upward dep. The
// socket WRITE/READ path lives in net_loopback (UDP); this file is ONLY the codec.
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/dmx/ArtnetOutput.cs:341-374  SendDmxPacket — ArtDmx opcode 0x5000, protVer 14,
//                                                  15-bit universe LE, even-padded data length BE.
//   Operators/Lib/io/dmx/ArtnetInput.cs:763-773   receive parse — validate id/opcode, universe LE,
//                                                  length BE, copy DMX slots.
//   Operators/Lib/io/dmx/SacnOutput.cs:1469-1518  BuildSacnDataPacket — E1.31 root+framing+DMP layers.
//   Operators/Lib/io/dmx/SacnOutput.cs:1448-1467  BuildSacnSyncPacket — E1.31 sync packet.
//   Operators/Lib/io/dmx/SacnInput.cs:1893-1904   receive parse — validate ACN id, universe, prop count.
//   Operators/Lib/io/dmx/SacnOutput.cs:1611-1615  GetSacnMulticastAddress — 239.255.(u>>8).(u&0xFF).
//   Operators/Lib/io/dmx/DMXOutput.cs:1057-1069   serial DMX frame — start code 0 + 512 clamped slots.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sw {

// ── Art-Net (UDP port 6454) ──────────────────────────────────────────────────────────────────────

// Build one ArtDmx packet (ArtnetOutput.cs SendDmxPacket :341-365). `slots` is the raw channel values
// (each clamped to a byte, <0→0 >255→255, cs:360). `universe` is the 15-bit Art-Net universe address.
// `sequence` is the per-universe sequence byte (cs:350). Header is 18 bytes; the DMX data length on the
// wire is even-padded to at least 2 (cs:296-297): sendLength = max(2, chunkCount) rounded up to even;
// padding bytes are zero (cs:363). Returns header(18) + sendLength bytes. `slots.size()` must be ≤ 512.
std::vector<uint8_t> buildArtnetDmxPacket(const std::vector<int>& slots, int universe, uint8_t sequence);

// The parsed contents of an ArtDmx packet (ArtnetInput.cs :763-773). `ok` false = not a valid ArtDmx
// (wrong id / opcode / length). `universe` is the 15-bit universe (data[14] | data[15]<<8). `slots` is
// the DMX data (data[18 .. 18+length), one int per byte, cleared to 512 with trailing zeros like the
// receiver's UniverseData snapshot). Mirrors the validate + BlockCopy in the receive loop.
struct ArtnetParsed {
  bool ok = false;
  int universe = 0;
  std::vector<int> slots;   // 512 entries (received length, zero-padded to 512 — ArtnetInput.cs:773)
};
ArtnetParsed parseArtnetDmxPacket(const std::vector<uint8_t>& packet);

// ── sACN / E1.31 (UDP port 5568) ─────────────────────────────────────────────────────────────────

// Build one E1.31 data packet (SacnOutput.cs BuildSacnDataPacket :1469-1518). `slots` = raw channel
// values (clamped to byte, cs:1514). `universe` 1..63999. `sequence` per-universe seq byte. `priority`
// 0..200 (cs:108). `sourceName` UTF-8, truncated to 63 bytes into the 64-byte field. `syncUniverse` 0
// disables sync (written into the sync-address field, cs:1495). Returns 126 + slots.size() bytes.
std::vector<uint8_t> buildSacnDataPacket(const std::vector<int>& slots, int universe, uint8_t sequence,
                                         uint8_t priority, const std::string& sourceName,
                                         const std::vector<uint8_t>& cid16, uint16_t syncUniverse);

// Build one E1.31 sync packet (SacnOutput.cs BuildSacnSyncPacket :1448-1467). 49 bytes fixed.
std::vector<uint8_t> buildSacnSyncPacket(uint16_t syncUniverse, uint8_t sequence,
                                         const std::vector<uint8_t>& cid16);

// The parsed contents of an E1.31 data packet (SacnInput.cs :1893-1904). `ok` false = not valid ACN
// (wrong id / short / bad prop count). `universe` = data[113]<<8 | data[114]. `slots` = the DMX data
// (propertyValueCount-1 slots, zero-padded to 512).
struct SacnParsed {
  bool ok = false;
  int universe = 0;
  std::vector<int> slots;   // 512 entries, zero-padded (SacnInput.cs:1904)
};
SacnParsed parseSacnDataPacket(const std::vector<uint8_t>& packet);

// sACN multicast address for a universe (SacnOutput.cs:1611-1615): 239.255.(u>>8).(u&0xFF), u clamped
// 1..63999. Returned as the 4 address bytes (a.b.c.d).
struct SacnMulticastAddr { uint8_t a, b, c, d; };
SacnMulticastAddr sacnMulticastAddr(int universe);

// ── Serial DMX (DMXOutput.cs, USB-DMX dongle) ──────────────────────────────────────────────────────

// Build the 513-byte serial DMX frame (DMXOutput.cs :1057-1069): byte[0] = start code 0x00, then up to
// 512 channel slots clamped to byte; slots beyond `universe.size()` are zero. Always 513 bytes.
std::vector<uint8_t> buildDmxSerialFrame(const std::vector<int>& universe);

}  // namespace sw
