// app/src/selftests_io_dmx.cpp — L5 IO/硬體 DMX-over-network codec harness (--selftest area leaf).
//
// 機器驗半 for the DMX family (ArtnetOutput/ArtnetInput/SacnOutput/SacnInput/DMXOutput). Every op's
// VALUE is a closed-form packet byte layout; this harness hand-推s the exact wire bytes from the TiXL
// .cs (line-cited) and asserts them against the pure builders in platform/dmx_packet. The socket send/
// receive is the same net_loopback UDP seam (proven in selftests_io_socket) + deferred-hw-verify for a
// real lighting node — only the packet bytes are load-bearing here, and they are fully verifiable now.
//
// Shell-tier leaf (app/src/ root): self-registers into selftestRegistry() during pre-main dynamic init.
// LEAF-LOCAL — includes its own platform header directly.
//
// Ground truth mirrored: external/tixl/Operators/Lib/io/dmx/*.cs (each expected value cites its line).
#include "runtime/selftest_registry.h"
#include "platform/dmx_packet.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sw {
namespace {

// ===== Art-Net output packet (ArtnetOutput.cs SendDmxPacket :341-374) =============================
// --selftest-io-artnet-packet : build an ArtDmx packet at a NON-TRIVIAL universe (0x1234, 15-bit) with
// mid channel values, assert every header byte + the even-length-padding rule + the data bytes.
// injectBug corrupts the universe input so the 15-bit split (SubUni/Net) diverges on the real builder.
int runArtnetPacketSelfTest(bool injectBug) {
  bool ok = true;

  // Universe 0x1234 = 4660. SubUni (low byte) = 0x34, Net (high 7 bits) = 0x12. Channels: 3 slots
  // [10, 260, 200] → clamped [10, 255, 200] (260>255). chunkCount=3 → sendLength = max(2,3)=3, odd →
  // padded to 4; byte[18+3] (padding) = 0. Sequence 7. EXPECTED bytes are all FIXED (P3-safe).
  // injectBug corrupts the universe INPUT (masks off the high 7 bits) so the real builder emits Net=0x00,
  // DIVERGING from the fixed expect 0x12 → RED.
  const int universe = injectBug ? (0x1234 & 0x00FF) : 0x1234;  // bug drops the Net bits
  std::vector<int> slots = {10, 260, 200};
  auto p = buildArtnetDmxPacket(slots, universe, /*sequence*/ 7);

  // Header (ArtnetOutput.cs:345-355). "Art-Net\0" + OpDmx 0x5000 + protVer 14.
  ok = ok && p.size() == 18 + 4;                       // sendLength padded 3→4
  const uint8_t artId[8] = {'A','r','t','-','N','e','t',0};
  for (int i = 0; i < 8; i++) ok = ok && p[i] == artId[i];
  ok = ok && p[8] == 0x00 && p[9] == 0x50;             // OpDmx (cs:346-347)
  ok = ok && p[10] == 0x00 && p[11] == 14;             // ProtVer 14 (cs:348-349)
  ok = ok && p[12] == 7;                               // Sequence (cs:350)
  ok = ok && p[13] == 0x00;                            // Physical (cs:351)
  ok = ok && p[14] == 0x34;                            // SubUni = universe & 0xFF (cs:352)
  ok = ok && p[15] == 0x12;                            // Net = (universe>>8)&0x7F (cs:353) — bug → 0x00 → RED
  ok = ok && p[16] == 0x00 && p[17] == 0x04;           // Length BE = 4 (padded) (cs:354-355)
  // Data (cs:357-361): clamped bytes + zero pad.
  ok = ok && p[18] == 10 && p[19] == 255 && p[20] == 200 && p[21] == 0;

  std::printf("[selftest-io-artnet-packet] size=%zuB net=0x%02X -> %s\n", p.size(), p[15],
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== Art-Net input parse round-trip (ArtnetInput.cs :763-773) ===================================
// --selftest-io-artnet-parse : parse a hand-built ArtDmx packet, assert universe + zero-padded slots.
// Two teeth. (a) VALIDATOR tooth: a REAL corrupted opcode must be rejected (ok=false) — asserted
// unconditionally, not gated on injectBug. (b) OFFSET tooth: FIXED expect slot0==77; injectBug corrupts
// the built packet's first DATA byte (at wire offset 18) so the real parser reads a DIVERGING value → RED.
int runArtnetParseSelfTest(bool injectBug) {
  bool ok = true;

  // (a) Validator: a corrupted-opcode packet is always rejected (bug-independent real behavior).
  {
    auto bad = buildArtnetDmxPacket({1, 2}, 5, 0);
    bad[9] = 0x00;                                      // OpDmx high → 0x0000, not 0x5000 (cs:763)
    ok = ok && !parseArtnetDmxPacket(bad).ok;
  }

  // (b) Offset tooth: valid packet, universe 5, slots [77, 88]. injectBug rewrites the first data byte
  // (wire offset 18, cs:357) to 200 so the parser (reading offset 18, cs:772) returns 200 ≠ fixed 77 → RED.
  auto pkt = buildArtnetDmxPacket({77, 88}, 5, 3);
  if (injectBug) pkt[18] = 200;
  auto r = parseArtnetDmxPacket(pkt);
  ok = ok && r.ok;
  ok = ok && r.universe == 5;                          // (cs:765)
  ok = ok && (int)r.slots.size() == 512;              // zero-padded to 512 (cs:773)
  ok = ok && r.slots[0] == 77;                        // FIXED expect — bug rewrites offset 18 → 200 → RED
  ok = ok && r.slots[1] == 88;
  ok = ok && r.slots[2] == 0 && r.slots[511] == 0;    // padding

  std::printf("[selftest-io-artnet-parse] ok=%d uni=%d slot0=%d -> %s\n", (int)r.ok, r.universe,
              r.ok ? r.slots[0] : -1, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== sACN / E1.31 data packet (SacnOutput.cs BuildSacnDataPacket :1469-1518) ====================
// --selftest-io-sacn-packet : build an E1.31 data packet at a non-trivial universe with a source name +
// priority + sync, assert the ACN root/framing/DMP layer bytes (all big-endian) + the BE length fields.
// injectBug corrupts the universe so the BE universe field [113..114] diverges on the real builder.
int runSacnPacketSelfTest(bool injectBug) {
  bool ok = true;

  // Universe 300 = 0x012C. 3 channels [1,2,300→255]. priority 150. sourceName "sw". sync universe 7.
  // CID = 16 distinct bytes 0x10..0x1F for the CID-field assert.
  std::vector<uint8_t> cid(16);
  for (int i = 0; i < 16; i++) cid[i] = (uint8_t)(0x10 + i);
  const int universe = injectBug ? 0 : 300;            // bug: zero universe → real BE field goes 0 → RED
  std::vector<int> slots = {1, 2, 300};
  auto p = buildSacnDataPacket(slots, universe, /*seq*/ 42, /*priority*/ 150, "sw", cid, /*sync*/ 7);

  const int dmxLength = 3;
  ok = ok && p.size() == (size_t)(126 + dmxLength);

  // Root layer (cs:1473-1480).
  ok = ok && p[0] == 0x00 && p[1] == 0x10 && p[2] == 0x00 && p[3] == 0x00;   // preamble (cs:1473)
  const char acn[9] = {'A','S','C','-','E','1','.','1','7'};
  for (int i = 0; i < 9; i++) ok = ok && p[4 + i] == (uint8_t)acn[i];        // ACN id (cs:1474)
  ok = ok && p[13] == 0 && p[14] == 0 && p[15] == 0;
  // root flags+length = 0x7000 | (108+3) = 0x7000|111 = 0x706F, BE → [0x70,0x6F] (cs:1476).
  ok = ok && p[16] == 0x70 && p[17] == 0x6F;
  // root vector 0x00000004 BE (cs:1478).
  ok = ok && p[18] == 0x00 && p[19] == 0x00 && p[20] == 0x00 && p[21] == 0x04;
  for (int i = 0; i < 16; i++) ok = ok && p[22 + i] == (uint8_t)(0x10 + i);  // CID (cs:1480)

  // Framing layer (cs:1481-1497).
  // framing flags+length = 0x7000 | (86+3) = 0x7000|89 = 0x7059, BE → [0x70,0x59] (cs:1481).
  ok = ok && p[38] == 0x70 && p[39] == 0x59;
  ok = ok && p[40] == 0x00 && p[41] == 0x00 && p[42] == 0x00 && p[43] == 0x02;  // framing vector (cs:1483)
  ok = ok && p[44] == 's' && p[45] == 'w' && p[46] == 0x00;                  // source name field (cs:1489)
  ok = ok && p[108] == 150;                                                 // priority (cs:1494)
  ok = ok && p[109] == 0x00 && p[110] == 0x07;                              // sync addr 7 BE (cs:1495)
  ok = ok && p[111] == 42;                                                  // sequence (cs:1497)
  ok = ok && p[112] == 0x00;                                               // options (cs:1498)
  // Universe BE — FIXED expect 0x012C; the injected bug (universe 0) makes real [113..114] go 0x0000 → RED.
  ok = ok && p[113] == 0x01;                                                // (cs:1499)
  ok = ok && p[114] == 0x2C;

  // DMP layer (cs:1501-1508).
  // dmp flags+length = 0x7000 | (9+3) = 0x7000|12 = 0x700C, BE → [0x70,0x0C] (cs:1501).
  ok = ok && p[115] == 0x70 && p[116] == 0x0C;
  ok = ok && p[117] == 0x02 && p[118] == 0xA1;                              // set-property + addr/type (cs:1503)
  ok = ok && p[119] == 0x00 && p[120] == 0x00;                             // first prop addr (cs:1504)
  ok = ok && p[121] == 0x00 && p[122] == 0x01;                             // addr increment (cs:1505)
  ok = ok && p[123] == 0x00 && p[124] == 0x04;                             // prop count = 3+1 BE (cs:1506)
  ok = ok && p[125] == 0x00;                                               // DMX start code (cs:1508)
  // Channel data (cs:1511-1515): [1, 2, 255] (300 clamped).
  ok = ok && p[126] == 1 && p[127] == 2 && p[128] == 255;

  std::printf("[selftest-io-sacn-packet] size=%zuB uni=[%02X%02X] -> %s\n", p.size(), p[113], p[114],
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== sACN sync packet (SacnOutput.cs BuildSacnSyncPacket :1448-1467) ============================
// --selftest-io-sacn-sync : 49-byte fixed sync packet. injectBug corrupts the sync universe input so
// the BE sync-address field diverges. (Distinct root vector 0x08 vs data's 0x04 — the sync-vs-data tell.)
int runSacnSyncSelfTest(bool injectBug) {
  bool ok = true;

  std::vector<uint8_t> cid(16, 0xAB);
  const uint16_t syncUni = injectBug ? 0 : 0x0102;     // 258; bug: 0 → real BE field goes 0 → RED
  auto p = buildSacnSyncPacket(syncUni, /*seq*/ 9, cid);

  ok = ok && p.size() == 49;                                                // (cs:1466)
  ok = ok && p[0] == 0x00 && p[1] == 0x10;                                  // preamble (cs:1450)
  const char acn[9] = {'A','S','C','-','E','1','.','1','7'};
  for (int i = 0; i < 9; i++) ok = ok && p[4 + i] == (uint8_t)acn[i];        // ACN id (cs:1451)
  // root flags+length = 0x7000|31 = 0x701F BE (cs:1453).
  ok = ok && p[16] == 0x70 && p[17] == 0x1F;
  // root vector 0x00000008 (EXTENDED) BE (cs:1455) — the sync tell vs data's 0x04.
  ok = ok && p[18] == 0x00 && p[19] == 0x00 && p[20] == 0x00 && p[21] == 0x08;
  // framing flags+length 0x7000|9 = 0x7009 BE (cs:1458).
  ok = ok && p[38] == 0x70 && p[39] == 0x09;
  ok = ok && p[40] == 0x00 && p[41] == 0x00 && p[42] == 0x00 && p[43] == 0x01;  // framing vector (cs:1460)
  ok = ok && p[44] == 9;                                                    // sequence (cs:1462)
  // sync address BE — FIXED expect 0x0102; bug (syncUni 0) makes real [45..46] go 0x0000 → RED.
  ok = ok && p[45] == 0x01;                                                 // (cs:1463)
  ok = ok && p[46] == 0x02;
  ok = ok && p[47] == 0x00 && p[48] == 0x00;                               // reserved (cs:1465)

  std::printf("[selftest-io-sacn-sync] size=%zuB -> %s\n", p.size(), ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== sACN input parse round-trip (SacnInput.cs :1893-1904) ======================================
// --selftest-io-sacn-parse : parse a hand-built E1.31 data packet, assert universe + zero-padded slots.
// Two teeth. (a) VALIDATOR: a REAL corrupted ACN id must be rejected (bug-independent). (b) OFFSET tooth:
// FIXED expect slot0==33; injectBug rewrites the built packet's first data byte (wire offset 126) so the
// real parser (reading offset 126, cs:1903) returns a DIVERGING value → RED.
int runSacnParseSelfTest(bool injectBug) {
  bool ok = true;
  std::vector<uint8_t> cid(16, 0x22);

  // (a) Validator: corrupted ACN id is always rejected (cs:1893).
  {
    auto bad = buildSacnDataPacket({1, 2, 3}, 12, 1, 100, "src", cid, 0);
    bad[4] = 'X';                                       // ACN id first byte → not "ASC-E1.17"
    ok = ok && !parseSacnDataPacket(bad).ok;
  }

  // (b) Offset tooth: valid packet, universe 12, slots [33,44,55]. injectBug rewrites the first data byte
  // (wire offset 126, cs:1511) to 199 so the parser reads 199 ≠ fixed 33 → RED.
  auto pkt = buildSacnDataPacket({33, 44, 55}, /*uni*/ 12, 1, 100, "src", cid, 0);
  if (injectBug) pkt[126] = 199;
  auto r = parseSacnDataPacket(pkt);
  ok = ok && r.ok;
  ok = ok && r.universe == 12;                          // (cs:1895)
  ok = ok && (int)r.slots.size() == 512;               // zero-padded (cs:1904)
  ok = ok && r.slots[0] == 33;                          // FIXED expect — bug rewrites offset 126 → 199 → RED
  ok = ok && r.slots[1] == 44 && r.slots[2] == 55;
  ok = ok && r.slots[3] == 0 && r.slots[511] == 0;

  std::printf("[selftest-io-sacn-parse] ok=%d uni=%d slot0=%d -> %s\n", (int)r.ok, r.universe,
              r.ok ? r.slots[0] : -1, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== sACN multicast address (SacnOutput.cs :1611-1615) ==========================================
// --selftest-io-sacn-mcast : 239.255.(u>>8).(u&0xFF), clamped 1..63999. injectBug not needed for a
// pure lookup with distinct probes; a wrong split would show on the middle probe. Probe u=300 (0x012C)
// → 239.255.1.44, and clamp ends 0→1 and 70000→63999.
int runSacnMcastSelfTest(bool injectBug) {
  bool ok = true;

  auto m = sacnMulticastAddr(injectBug ? 0x012D : 0x012C);  // 300; bug feeds 301 → real d goes 0x2D → RED
  ok = ok && m.a == 239 && m.b == 255;
  ok = ok && m.c == 0x01;                              // high byte 0x012C>>8
  ok = ok && m.d == 0x2C;                              // low byte — FIXED expect; bug's 301 → 0x2D → RED

  // Clamp ends (cs:1613): 0 → 1 → 239.255.0.1; 70000 → 63999 (0xF9FF) → 239.255.249.255.
  auto lo = sacnMulticastAddr(0);
  ok = ok && lo.c == 0x00 && lo.d == 0x01;
  auto hi = sacnMulticastAddr(70000);
  ok = ok && hi.c == 0xF9 && hi.d == 0xFF;

  std::printf("[selftest-io-sacn-mcast] 300→239.255.%d.%d -> %s\n", m.c, m.d, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== Serial DMX frame (DMXOutput.cs :1057-1069) =================================================
// --selftest-io-dmx-serial : 513-byte frame = start code 0 + up to 512 clamped slots (rest zero).
// The EXPECTED bytes are FIXED (bug-independent, P3-safe). injectBug corrupts an INPUT slot value (feeds
// an over-range 261 in place of the valid 5) so the real builder produces 255 for that slot, DIVERGING
// from the fixed expect 5 → RED — the byte-offset math + start-code placement stay the real cook path.
int runDmxSerialSelfTest(bool injectBug) {
  bool ok = true;

  // 3 slots. Slot0 is 5 (valid); injectBug replaces it with 261 (clamps to 255 ≠ the fixed expect 5).
  // 300 always clamps to 255 (that's the clamp-is-applied probe); 250 passes through.
  std::vector<int> uni = {injectBug ? 261 : 5, 300, 250};
  auto f = buildDmxSerialFrame(uni);

  ok = ok && f.size() == 513;                          // (cs:945)
  ok = ok && f[0] == 0x00;                             // start code — byte[0] never written = 0 (cs:1060)
  // Slot 0 lives at byte[1] (cs:1064: _dmxFrameBuffer[i+1]). FIXED expect 5. Bug feeds 261 → real=255 → RED.
  ok = ok && f[1] == 5;
  ok = ok && f[2] == 255;                              // 300 clamped (cs:1065) — clamp-is-applied probe
  ok = ok && f[3] == 250;
  ok = ok && f[4] == 0 && f[512] == 0;                 // beyond count → zero (cs:1060 clear)

  std::printf("[selftest-io-dmx-serial] size=%zuB slot0=%d -> %s\n", f.size(), f[1], ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

REGISTER_SELFTESTS(/*orderBase=*/710,
    {"io-artnet-packet", runArtnetPacketSelfTest},
    {"io-artnet-parse",  runArtnetParseSelfTest},
    {"io-sacn-packet",   runSacnPacketSelfTest},
    {"io-sacn-sync",     runSacnSyncSelfTest},
    {"io-sacn-parse",    runSacnParseSelfTest},
    {"io-sacn-mcast",    runSacnMcastSelfTest},
    {"io-dmx-serial",    runDmxSerialSelfTest});

}  // namespace sw
