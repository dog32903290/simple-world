// platform/dmx_packet — pure DMX-over-network packet codec. See dmx_packet.h.
//
// TiXL authority: Operators/Lib/io/dmx/{ArtnetOutput,ArtnetInput,SacnOutput,SacnInput,DMXOutput}.cs.
#include "platform/dmx_packet.h"

#include <algorithm>
#include <cstring>

namespace sw {

namespace {
// Clamp an int channel value to a DMX byte (ArtnetOutput.cs:360 / SacnOutput.cs:1514 / DMXOutput.cs:1065).
uint8_t clampSlot(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
// Art-Net protocol constants (ArtnetOutput.cs:15,345-349).
const uint8_t kArtnetId[8] = {'A', 'r', 't', '-', 'N', 'e', 't', 0};
}  // namespace

// ── Art-Net ────────────────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> buildArtnetDmxPacket(const std::vector<int>& slots, int universe, uint8_t sequence) {
  const int chunkCount = (int)slots.size();
  // sendLength = max(2, chunkCount), rounded up to even (ArtnetOutput.cs:296-297).
  int sendLength = std::max(2, chunkCount);
  if (sendLength % 2 != 0) sendLength++;

  std::vector<uint8_t> p(18 + sendLength, 0);
  std::memcpy(p.data(), kArtnetId, 8);          // "Art-Net\0" (cs:345)
  p[8]  = 0x00;                                  // OpDmx low  (cs:346)
  p[9]  = 0x50;                                  // OpDmx high → 0x5000 (cs:347)
  p[10] = 0x00;                                  // ProtVerHi  (cs:348)
  p[11] = 14;                                    // ProtVerLo  (cs:349)
  p[12] = sequence;                              // Sequence   (cs:350)
  p[13] = 0x00;                                  // Physical   (cs:351)
  p[14] = (uint8_t)(universe & 0xFF);            // SubUni (universe low, cs:352)
  p[15] = (uint8_t)((universe >> 8) & 0x7F);     // Net (universe high 7 bits, cs:353)
  p[16] = (uint8_t)(sendLength >> 8);            // Length hi (BE, cs:354)
  p[17] = (uint8_t)(sendLength & 0xFF);          // Length lo (cs:355)
  for (int i = 0; i < chunkCount; i++) p[18 + i] = clampSlot(slots[i]);  // data (cs:357-361)
  // p already zero-filled for the [chunkCount, sendLength) padding (cs:363).
  return p;
}

ArtnetParsed parseArtnetDmxPacket(const std::vector<uint8_t>& d) {
  ArtnetParsed r;
  // Validate: >=18 bytes, id match, opcode 0x5000 (ArtnetInput.cs:763).
  if (d.size() < 18) return r;
  if (std::memcmp(d.data(), kArtnetId, 8) != 0) return r;
  if (d[8] != 0x00 || d[9] != 0x50) return r;
  const int universe = d[14] | (d[15] << 8);              // (cs:765)
  const int length = (d[16] << 8) | d[17];                // (cs:766, BE)
  if (length == 0 || length > 512 || (int)d.size() < 18 + length) return r;  // (cs:767)
  r.ok = true;
  r.universe = universe;
  r.slots.assign(512, 0);                                 // zero-padded to 512 (cs:773)
  for (int i = 0; i < length; i++) r.slots[i] = d[18 + i];
  return r;
}

// ── sACN / E1.31 ─────────────────────────────────────────────────────────────────────────────────

namespace {
// Write a big-endian 16-bit value (mirrors IPAddress.HostToNetworkOrder((short)v) + copy).
void putBE16(std::vector<uint8_t>& p, int off, uint16_t v) {
  p[off] = (uint8_t)((v >> 8) & 0xFF);
  p[off + 1] = (uint8_t)(v & 0xFF);
}
// Write a big-endian 32-bit value (mirrors HostToNetworkOrder(int) + copy).
void putBE32(std::vector<uint8_t>& p, int off, uint32_t v) {
  p[off] = (uint8_t)((v >> 24) & 0xFF);
  p[off + 1] = (uint8_t)((v >> 16) & 0xFF);
  p[off + 2] = (uint8_t)((v >> 8) & 0xFF);
  p[off + 3] = (uint8_t)(v & 0xFF);
}
// ACN packet identifier "ASC-E1.17\0\0\0" — bytes 4..15 (SacnOutput.cs:1451,1474 write "ASC-E1.17"
// into [4..12]; [13..15] stay 0 from the [1]=0x10 preamble init). Full 12-byte id validated by SacnInput.
const char kAcnId[9] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7'};
}  // namespace

std::vector<uint8_t> buildSacnDataPacket(const std::vector<int>& slots, int universe, uint8_t sequence,
                                         uint8_t priority, const std::string& sourceName,
                                         const std::vector<uint8_t>& cid16, uint16_t syncUniverse) {
  const int chunkCount = (int)slots.size();
  const int dmxLength = chunkCount;                        // (short)chunkCount (cs:1471)
  std::vector<uint8_t> p(126 + dmxLength, 0);

  // Root layer preamble (cs:1473-1475): preamble size 0x0010, postamble 0, then ACN id.
  p[0] = 0x00; p[1] = 0x10; p[2] = 0x00; p[3] = 0x00;
  std::memcpy(p.data() + 4, kAcnId, 9);                    // "ASC-E1.17" into [4..12] (cs:1474)
  // p[13..15] already 0 (cs:1475).
  putBE16(p, 16, (uint16_t)(0x7000 | (108 + dmxLength)));  // root flags+length (cs:1476)
  putBE32(p, 18, 0x00000004);                              // root vector VECTOR_ROOT_E131_DATA (cs:1478)
  for (int i = 0; i < 16; i++) p[22 + i] = (i < (int)cid16.size() ? cid16[i] : 0);  // CID (cs:1480)

  // Framing layer (cs:1481-1497).
  putBE16(p, 38, (uint16_t)(0x7000 | (86 + dmxLength)));   // framing flags+length (cs:1481)
  putBE32(p, 40, 0x00000002);                              // framing vector VECTOR_E131_DATA_PACKET (cs:1483)
  // Source name: 64-byte field at [44], already zero-cleared (cs:1486); copy up to 63 bytes (cs:1489-1491).
  {
    int copyCount = std::min((int)sourceName.size(), 63);
    for (int i = 0; i < copyCount; i++) p[44 + i] = (uint8_t)sourceName[i];
  }
  p[108] = priority;                                       // priority (cs:1494)
  putBE16(p, 109, syncUniverse);                           // sync address (cs:1495-1496)
  p[111] = sequence;                                       // sequence number (cs:1497)
  p[112] = 0x00;                                           // options (cs:1498)
  putBE16(p, 113, (uint16_t)universe);                     // universe (cs:1499-1500)

  // DMP layer (cs:1501-1508).
  putBE16(p, 115, (uint16_t)(0x7000 | (9 + dmxLength)));   // dmp flags+length (cs:1501)
  p[117] = 0x02;                                           // VECTOR_DMP_SET_PROPERTY (cs:1503)
  p[118] = 0xa1;                                           // address+data type (cs:1503)
  p[119] = 0x00; p[120] = 0x00;                            // first prop addr (cs:1504)
  p[121] = 0x00; p[122] = 0x01;                            // addr increment (cs:1505)
  putBE16(p, 123, (uint16_t)(dmxLength + 1));              // prop value count = slots+startcode (cs:1506-1507)
  p[125] = 0x00;                                           // DMX start code (cs:1508)

  for (int i = 0; i < chunkCount; i++) p[126 + i] = clampSlot(slots[i]);  // channel data (cs:1511-1515)
  return p;
}

std::vector<uint8_t> buildSacnSyncPacket(uint16_t syncUniverse, uint8_t sequence,
                                         const std::vector<uint8_t>& cid16) {
  std::vector<uint8_t> p(49, 0);
  p[0] = 0x00; p[1] = 0x10; p[2] = 0x00; p[3] = 0x00;      // (cs:1450)
  std::memcpy(p.data() + 4, kAcnId, 9);                    // "ASC-E1.17" (cs:1451)
  putBE16(p, 16, (uint16_t)(0x7000 | 31));                 // root flags+length (cs:1453)
  putBE32(p, 18, 0x00000008);                              // root vector VECTOR_ROOT_E131_EXTENDED (cs:1455)
  for (int i = 0; i < 16; i++) p[22 + i] = (i < (int)cid16.size() ? cid16[i] : 0);  // CID (cs:1457)
  putBE16(p, 38, (uint16_t)(0x7000 | 9));                  // framing flags+length (cs:1458)
  putBE32(p, 40, 0x00000001);                              // framing vector VECTOR_E131_EXTENDED_SYNCHRONIZATION (cs:1460)
  p[44] = sequence;                                        // sequence (cs:1462)
  putBE16(p, 45, syncUniverse);                            // sync address (cs:1463-1464)
  p[47] = 0x00; p[48] = 0x00;                              // reserved (cs:1465)
  return p;
}

SacnParsed parseSacnDataPacket(const std::vector<uint8_t>& d) {
  SacnParsed r;
  // Validate: >=126 bytes, ACN id at [4..16) == "ASC-E1.17\0\0\0" (SacnInput.cs:1893).
  if (d.size() < 126) return r;
  static const uint8_t acnFull[12] = {'A','S','C','-','E','1','.','1','7',0,0,0};
  if (std::memcmp(d.data() + 4, acnFull, 12) != 0) return r;
  const int universe = (d[113] << 8) | d[114];            // (cs:1895)
  const int propertyValueCount = (d[123] << 8) | d[124];  // (cs:1896)
  const int dmxLength = propertyValueCount - 1;           // minus start code (cs:1897)
  if (dmxLength <= 0 || dmxLength > 512 || (int)d.size() < 126 + dmxLength) return r;  // (cs:1898)
  r.ok = true;
  r.universe = universe;
  r.slots.assign(512, 0);                                 // zero-padded (cs:1904)
  for (int i = 0; i < dmxLength; i++) r.slots[i] = d[126 + i];  // (cs:1903)
  return r;
}

SacnMulticastAddr sacnMulticastAddr(int universe) {
  int u = universe < 1 ? 1 : (universe > 63999 ? 63999 : universe);  // Clamp(1,63999) (cs:1613)
  SacnMulticastAddr m;
  m.a = 239; m.b = 255;
  m.c = (uint8_t)((u >> 8) & 0xFF);
  m.d = (uint8_t)(u & 0xFF);
  return m;
}

// ── Serial DMX ─────────────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> buildDmxSerialFrame(const std::vector<int>& universe) {
  std::vector<uint8_t> p(513, 0);       // [0]=start code 0, [1..512]=slots (DMXOutput.cs:945,1060)
  const int count = std::min((int)universe.size(), 512);  // (cs:1063)
  for (int i = 0; i < count; i++) p[i + 1] = clampSlot(universe[i]);  // (cs:1064-1065)
  return p;
}

}  // namespace sw
