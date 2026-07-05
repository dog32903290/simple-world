// app/src/selftests_io_misc.cpp — L5 IO/雜項 codec harness (--selftest area leaf).
//
// 機器驗半 for the io-misc atoms whose VALUE is a closed-form byte layout or numeric conversion:
//   FreeDInput/FreeDOutput    — 29-byte "D1" camera-tracking packet (platform/freed_packet).
//   PosiStageInput/Output     — nested PSN chunk packet, root 0x6755 (platform/psn_packet).
//   PointsToRGBList           — per-point Color→byte quantise (platform/points_dmx).
//   PointsToDmxLights         — SetDmxValue 8-bit / 16-bit channel map (platform/points_dmx).
//   WriteToFile               — write-on-change file gate + byte round-trip (platform/write_to_file).
// Every expected value is hand-推d from the TiXL .cs (each assert cites its line). The socket transport is
// the same net_loopback UDP seam (+ deferred-hw-verify for real hardware) — only the packet/value bytes
// are load-bearing here, and they are fully verifiable now.
//
// Shell-tier leaf (app/src/ root): self-registers into selftestRegistry() during pre-main dynamic init.
// LEAF-LOCAL — includes its own platform headers directly.
//
// Ground truth mirrored: external/tixl/Operators/Lib/io/{freed,posistage,dmx,file}/*.cs.
#include "runtime/selftest_registry.h"
#include "platform/freed_packet.h"
#include "platform/psn_packet.h"
#include "platform/points_dmx.h"
#include "platform/write_to_file.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace sw {
namespace {

// ===== FreeD output packet (FreeDOutput.cs BuildFreeDPacket :83-120) ===============================
// --selftest-io-freed-build : build a "D1" packet at NON-TRIVIAL rot/pos/zoom/focus/user and assert every
// field byte + the int24-BE scale/round + the checksum. EXPECTED bytes are FIXED (hand-推d, P3-safe).
// injectBug corrupts the rotation-X INPUT (adds a whole-degree offset) so the real int24 field DIVERGES
// from the fixed expect 0x008000 → RED — the round/scale/BE math + checksum stay the real cook path.
int runFreeDBuildSelfTest(bool injectBug) {
  bool ok = true;

  // cameraId=7; rot=(1,-2,0.5)°; pos=(0.25,-0.5,1.0)m; zoom=0x1234; focus=0x00ABCD; user=0x0102.
  // Values chosen so ×scale lands on integers (no .5 → banker's-vs-away rounding can't diverge).
  const float rotX = injectBug ? 2.0f : 1.0f;  // bug: 2° → int24 0x010000 ≠ fixed 0x008000 → RED
  auto p = buildFreeDPacket(/*cameraId*/ 7, rotX, -2.0f, 0.5f, 0.25f, -0.5f, 1.0f,
                            /*zoom*/ 0x1234, /*focus*/ 0x00ABCD, /*user*/ 0x0102);

  ok = ok && p.size() == 29;                           // (cs:245 FreeDPacketLength)
  ok = ok && p[0] == 0xD1;                             // identifier (cs:86)
  ok = ok && p[1] == 0x07;                             // cameraId (cs:87)
  // rotX = round(1°*32768)=0x008000 BE (cs:89) — bug feeds 2° → real 0x010000 → RED.
  ok = ok && p[2] == 0x00 && p[3] == 0x80 && p[4] == 0x00;
  // rotY = round(-2°*32768) = -65536 = 0xFF0000 int24 BE (cs:90).
  ok = ok && p[5] == 0xFF && p[6] == 0x00 && p[7] == 0x00;
  // rotZ = round(0.5°*32768) = 16384 = 0x004000 (cs:91).
  ok = ok && p[8] == 0x00 && p[9] == 0x40 && p[10] == 0x00;
  // posX = round(0.25*64000)=16000=0x003E80 (cs:93); posY = -32000 = 0xFF8300 (cs:94).
  ok = ok && p[11] == 0x00 && p[12] == 0x3E && p[13] == 0x80;
  ok = ok && p[14] == 0xFF && p[15] == 0x83 && p[16] == 0x00;
  // posZ = round(1.0*64000)=64000=0x00FA00 (cs:95).
  ok = ok && p[17] == 0x00 && p[18] == 0xFA && p[19] == 0x00;
  ok = ok && p[20] == 0x00 && p[21] == 0x12 && p[22] == 0x34;   // zoom (cs:97)
  ok = ok && p[23] == 0x00 && p[24] == 0xAB && p[25] == 0xCD;   // focus (cs:98)
  ok = ok && p[26] == 0x01 && p[27] == 0x02;                    // user u16 BE (cs:100-101)
  // Checksum over bytes[0..27]: (byte)(0x40-Σ) = 0xAE for the fixed packet (cs:104,116-119). The injected
  // rotX bug changes Σ → the checksum ALSO diverges, but the fixed int24 assert above already trips first.
  ok = ok && p[28] == freeDChecksum(p, 28);            // internal consistency (real checksum path)
  if (!injectBug) ok = ok && p[28] == 0xAE;            // FIXED expect for the clean packet

  std::printf("[selftest-io-freed-build] size=%zuB rotX=[%02X %02X %02X] chk=0x%02X -> %s\n",
              p.size(), p[2], p[3], p[4], p[28], ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== FreeD input parse round-trip (FreeDInput.cs ParseFreeDDataPacket :163-203) ==================
// --selftest-io-freed-parse : parse a hand-built "D1" packet, assert decoded fields. Two teeth.
// (a) VALIDATOR: a REAL corrupted checksum must be rejected (ok=false) — asserted unconditionally.
// (b) FIELD tooth: FIXED expect cameraId==42 + zoom==0x1234; injectBug corrupts the built packet's
// cameraId byte (offset 1) so the parser reads a DIVERGING value → RED.
int runFreeDParseSelfTest(bool injectBug) {
  bool ok = true;

  // (a) Validator: a corrupted-checksum packet is always rejected (cs:166) — bug-independent.
  {
    auto bad = buildFreeDPacket(1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    bad[28] ^= 0xFF;                                    // wreck the checksum byte
    ok = ok && !parseFreeDPacket(bad).ok;
  }

  // (b) Field tooth: valid packet, cameraId=42, zoom=0x1234. injectBug rewrites the cameraId byte
  // (wire offset 1) to 99 BEFORE re-fixing the checksum, so the parser (cs:173) returns 99 ≠ fixed 42 → RED.
  auto pkt = buildFreeDPacket(/*cameraId*/ 42, 1.0f, 0, 0, 0, 0, 0, /*zoom*/ 0x1234, 0, /*user*/ 0x0005);
  if (injectBug) {
    pkt[1] = 99;                                        // corrupt the real DATA byte…
    pkt[28] = freeDChecksum(pkt, 28);                   // …and re-checksum so it still validates → RED on field
  }
  auto r = parseFreeDPacket(pkt);
  ok = ok && r.ok;
  ok = ok && r.cameraId == 42;                          // FIXED expect (cs:173) — bug → 99 → RED
  ok = ok && r.zoom == 0x1234;                          // uint24 (cs:168)
  ok = ok && r.user == 0x0005;                          // u16 (cs:170)
  // rotX round-trips 1° (int24 16384 / 32768 = 0.5 — wait: 1°*32768=32768 /32768 =1.0). Assert exact.
  ok = ok && r.rotX > 0.999f && r.rotX < 1.001f;        // 1° decoded (cs:174)

  std::printf("[selftest-io-freed-parse] ok=%d cam=%d zoom=0x%X -> %s\n", (int)r.ok, r.cameraId, r.zoom,
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== PosiStage output packet (PosiStageOutput.cs BuildPsnDataPacket :149-204) ====================
// --selftest-io-psn-build : build a PSN_DATA packet with one tracker at a non-trivial pos/ori, assert the
// root 0x6755 header, the chunk headers (id|len<<16|subBit LE), the LE floats, and the cs:179/186 Z-flip.
// injectBug corrupts the tracker's posX INPUT so the LE float at the position field DIVERGES → RED.
int runPsnBuildSelfTest(bool injectBug) {
  bool ok = true;

  PsnTracker t;
  t.id = 0;
  t.posX = injectBug ? 9.0f : 1.0f;  // bug: 9.0 → LE float 0x41100000 ≠ fixed 1.0 (0x3F800000) → RED
  t.posY = 2.0f;
  t.posZ = 3.0f;                     // wire is FLIPPED to -3.0 (cs:179)
  t.oriX = 0.1f; t.oriY = 0.2f; t.oriZ = 0.3f;
  auto p = buildPsnDataPacket({t}, /*frameId*/ 5, /*ts*/ 0x0102030405060708ULL);

  ok = ok && p.size() == 60;                            // 4(root)+20(hdr)+4(list)+4(tracker)+4+12+4+12
  // Root chunk 0x6755, len=56, hasSubChunks → header u32 0x80386755 LE = 55 67 38 80 (cs:200).
  ok = ok && p[0] == 0x55 && p[1] == 0x67 && p[2] == 0x38 && p[3] == 0x80;
  // Header chunk 0x0000, len 12, no sub → 00 00 0C 00 (cs:155).
  ok = ok && p[4] == 0x00 && p[5] == 0x00 && p[6] == 0x0C && p[7] == 0x00;
  // Timestamp u64 LE (cs:156): 0x0102030405060708 → 08 07 06 05 04 03 02 01.
  ok = ok && p[8] == 0x08 && p[15] == 0x01;
  ok = ok && p[16] == 0x02 && p[17] == 0x03 && p[18] == 0x05 && p[19] == 0x01;  // ver 2.3, frame 5, count 1
  // List chunk 0x0001 len 36 sub → 01 00 24 80 (cs:194).
  ok = ok && p[20] == 0x01 && p[21] == 0x00 && p[22] == 0x24 && p[23] == 0x80;
  // Tracker chunk id 0 len 32 sub → 00 00 20 80 (cs:189).
  ok = ok && p[24] == 0x00 && p[25] == 0x00 && p[26] == 0x20 && p[27] == 0x80;
  // Position field 0x0000 len 12 (cs:176) then X=1.0 LE = 00 00 80 3F (cs:177) — bug → 9.0 → RED.
  ok = ok && p[28] == 0x00 && p[29] == 0x00 && p[30] == 0x0C && p[31] == 0x00;
  ok = ok && p[32] == 0x00 && p[33] == 0x00 && p[34] == 0x80 && p[35] == 0x3F;
  // Z flipped: -3.0 LE = 00 00 40 C0 (cs:179) — the flip is the tell (unflipped 3.0 = 00 00 40 40).
  ok = ok && p[40] == 0x00 && p[41] == 0x00 && p[42] == 0x40 && p[43] == 0xC0;
  // Orientation field 0x0002 (cs:183): 02 00 0C 00.
  ok = ok && p[44] == 0x02 && p[45] == 0x00 && p[46] == 0x0C && p[47] == 0x00;

  std::printf("[selftest-io-psn-build] size=%zuB posX=[%02X %02X %02X %02X] -> %s\n", p.size(),
              p[32], p[33], p[34], p[35], ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== PosiStage input parse round-trip (PosiStageInput.cs ParsePsnDataPacket :179-234) ============
// --selftest-io-psn-parse : parse a hand-built PSN_DATA packet, assert tracker count + decoded pos/ori.
// Two teeth. (a) VALIDATOR: a REAL corrupted root magic must be rejected (ok=false). (b) FIELD tooth:
// FIXED expect posY==2.0; injectBug rewrites the built packet's posY float bytes so the parser DIVERGES.
int runPsnParseSelfTest(bool injectBug) {
  bool ok = true;

  PsnTracker t0; t0.id = 3; t0.posX = 5.0f; t0.posY = 2.0f; t0.posZ = 4.0f;
  t0.oriX = 0.5f; t0.oriY = -0.5f; t0.oriZ = 0.25f;

  // (a) Validator: a corrupted root magic is always rejected (cs:184).
  {
    auto bad = buildPsnDataPacket({t0}, 1, 0);
    bad[0] ^= 0xFF;                                     // root low byte no longer 0x55
    ok = ok && !parsePsnDataPacket(bad).ok;
  }

  // (b) Field tooth: valid packet. posY float lives at wire offset 36..39 (position field body +4). bug
  // rewrites it to 8.0 (0x41000000 → 00 00 00 41) so the parser (cs:218) reads 8.0 ≠ fixed 2.0 → RED.
  auto pkt = buildPsnDataPacket({t0}, /*frameId*/ 9, /*ts*/ 0);
  if (injectBug) { pkt[36] = 0x00; pkt[37] = 0x00; pkt[38] = 0x00; pkt[39] = 0x41; }
  auto r = parsePsnDataPacket(pkt);
  ok = ok && r.ok;
  ok = ok && r.trackers.size() == 1;
  if (r.trackers.size() == 1) {
    const auto& tr = r.trackers[0];
    ok = ok && tr.id == 3;                              // (cs:199)
    ok = ok && tr.posX > 4.999f && tr.posX < 5.001f;    // (cs:218)
    ok = ok && tr.posY > 1.999f && tr.posY < 2.001f;    // FIXED expect — bug → 8.0 → RED
    // posZ round-trips FLIPPED: sender wrote -4.0, parser stores the wire value -4.0 (no un-flip).
    ok = ok && tr.posZ > -4.001f && tr.posZ < -3.999f;
    ok = ok && tr.oriY > -0.501f && tr.oriY < -0.499f;  // orientation Y (cs:221)
  }

  std::printf("[selftest-io-psn-parse] ok=%d n=%zu posY=%.3f -> %s\n", (int)r.ok, r.trackers.size(),
              r.trackers.empty() ? -1.0 : (double)r.trackers[0].posY, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== PointsToRGBList (PointsToRGBList.cs :95-107) ================================================
// --selftest-io-points-rgb : per-point Color→round(c*255) as (R,G,B). FIXED expect from hand-推d rounds.
// injectBug corrupts the SECOND point's G INPUT so the real conversion DIVERGES from the fixed expect → RED.
int runPointsRgbSelfTest(bool injectBug) {
  bool ok = true;

  // 2 points. p0 color=(1.0, 0.0, 0.5); p1 color=(0.2, 0.6, 1.0). Expect round: (255,0,128),(51,153,255).
  // 0.5*255=127.5 → banker's 128, away 128 (both agree — chosen to avoid the .5 ambiguity trap: 127.5
  // rounds to 128 under away-from-zero AND to 128 under round-half-to-even). 0.2*255=51, 0.6*255=153.
  const float g1 = injectBug ? 0.9f : 0.6f;   // bug: 0.9*255=230 (round 0.9*255=229.5→230) ≠ fixed 153 → RED
  std::vector<float> colors = {1.0f, 0.0f, 0.5f,  0.2f, g1, 1.0f};
  auto rgb = pointsToRgbList(colors);

  ok = ok && rgb.size() == 6;
  ok = ok && rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 128;   // p0 (cs:104-106) 0.5→128
  ok = ok && rgb[3] == 51;                                    // p1 R = round(0.2*255)=51
  ok = ok && rgb[4] == 153;                                   // p1 G = round(0.6*255)=153 — bug → 230 → RED
  ok = ok && rgb[5] == 255;                                   // p1 B = round(1.0*255)=255

  std::printf("[selftest-io-points-rgb] n=%zu p1G=%d -> %s\n", rgb.size(), rgb.size() >= 5 ? rgb[4] : -1,
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== PointsToDmxLights SetDmxValue (PointsToDMXLights.cs :1239-1284) =============================
// --selftest-io-dmx-value : 8-bit + 16-bit DMX channel mapping. FIXED expects hand-推d. injectBug shifts
// the 16-bit VALUE input so the coarse/fine BE split DIVERGES from the fixed expect → RED. The clamp + the
// BE coarse=hi/fine=lo split (cs:1262-1263) are the real cook path.
int runDmxValueSelfTest(bool injectBug) {
  bool ok = true;

  // 8-bit: value 0.5 in [0,1] → round(0.5*255)=128 (0.5*255=127.5→128). value 2.0 clamps to 255.
  ok = ok && dmxValue8(0.5f, 0.0f, 1.0f) == 128;   // (cs:1268-1269)
  ok = ok && dmxValue8(2.0f, 0.0f, 1.0f) == 255;   // clamp01 (cs:1268)
  ok = ok && dmxValue8(-1.0f, 0.0f, 1.0f) == 0;    // clamp01 low

  // 16-bit: value 0.25 in [0,1] → round(0.25*65535)=16384 (0x4000). Split BE: coarse=0x40, fine=0x00.
  // injectBug feeds 0.75 → round(0.75*65535)=49151 (0xBFFF) coarse 0xBF ≠ fixed 0x40 → RED.
  const float v16 = injectBug ? 0.75f : 0.25f;
  auto s = dmxSplit16(v16, 0.0f, 1.0f);
  ok = ok && s.coarse == 0x40;                     // FIXED expect (cs:1262) — bug → 0xBF → RED
  ok = ok && s.fine == 0x00;                       // (cs:1263)
  // Independent 16-bit magnitude check at the fixed value (not gated) — full-scale maps to 0xFFFF.
  ok = ok && dmxValue16(1.0f, 0.0f, 1.0f) == 65535;   // (cs:1283)

  std::printf("[selftest-io-dmx-value] 8b(0.5)=128 16b.coarse=0x%02X -> %s\n", s.coarse,
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== WriteToFile write-on-change gate + byte round-trip (WriteToFile.cs :17-42) =================
// --selftest-io-writefile : (a) write content to a temp file, read it back byte-for-byte. (b) the
// change-gate: re-feeding the SAME content is a no-op (returns false); NEW content writes again. injectBug
// breaks the gate (treats every call as changed) so the "same content → no write" assert DIVERGES → RED.
int runWriteToFileSelfTest(bool injectBug) {
  bool ok = true;

  std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
                     "/sw_writetofile_golden.txt";
  std::remove(path.c_str());

  WriteToFileState st;
  // Build the exact test bytes explicitly (an embedded NUL + a high byte) so the round-trip proves the
  // writer is byte-exact, not std::string-length-truncated at the NUL.
  std::string c1;
  c1.push_back('h'); c1.push_back('i'); c1.push_back('\0'); c1.push_back((char)0xFF); c1.push_back('!');

  bool wroteOk = false;
  bool wrote1 = writeToFileUpdate(st, c1, path, wroteOk);
  ok = ok && wrote1 && wroteOk;                        // first call always writes (cs:21 never-written)

  // Byte round-trip: the on-disk file equals c1 exactly, including the NUL + 0xFF.
  {
    std::ifstream f(path, std::ios::binary);
    std::string disk((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    ok = ok && disk == c1;                             // File.WriteAllText round-trip (cs:26)
  }

  // (b) Gate: same content again → NO write (cs:21 content != _lastContent is false). injectBug models a
  // broken gate by resetting hasWritten+lastContent so the "unchanged" call writes anyway → RED.
  if (injectBug) { st.hasWritten = false; st.lastContent.clear(); }
  bool wrote2 = writeToFileUpdate(st, c1, path, wroteOk);
  ok = ok && (wrote2 == false);                        // FIXED expect: unchanged → no write — bug → true → RED

  // New content → writes again (real gate opens on a genuine change).
  bool wrote3 = writeToFileUpdate(st, std::string("changed"), path, wroteOk);
  ok = ok && wrote3;                                   // (cs:21 changed → write)

  std::remove(path.c_str());
  std::printf("[selftest-io-writefile] w1=%d w2=%d(want 0) w3=%d -> %s\n", (int)wrote1, (int)wrote2,
              (int)wrote3, ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

REGISTER_SELFTESTS(/*orderBase=*/720,
    {"io-freed-build",   runFreeDBuildSelfTest},
    {"io-freed-parse",   runFreeDParseSelfTest},
    {"io-psn-build",     runPsnBuildSelfTest},
    {"io-psn-parse",     runPsnParseSelfTest},
    {"io-points-rgb",    runPointsRgbSelfTest},
    {"io-dmx-value",     runDmxValueSelfTest},
    {"io-writefile",     runWriteToFileSelfTest});

}  // namespace sw
