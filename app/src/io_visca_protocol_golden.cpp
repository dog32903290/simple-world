// io_visca_protocol_golden — ViscaCamera VISCA-over-IP byte-protocol golden
// (--selftest-io-visca-protocol).
//
// Layer of the io/ptz ViscaCamera: the CLOSED-FORM byte protocol (command build + reply parse),
// verified against hand-derived bytes — the wled_frame/websocket_frame precedent. The live UDP
// send/recv (System.Net.Sockets.UdpClient; macOS would reuse net_loopback's POSIX UDP) is a DEFERRED
// device leg (census io/ptz R3, deferred-hw-verify), same split those siblings already model.
//
// TiXL authority: external/tixl/Operators/Lib/io/ptz/ViscaCamera.cs — Set4ByteVal (cs:316-323),
// SendAbsoluteMoveAsync (cs:278-313), SendViscaCommandAsync header wrap (cs:325-342), ProcessPacket
// reply parse (cs:187-253). injectBug drives platform/visca_protocol.h's setViscaBug (real cook-term
// corruption, NOT a want-flip). did-not-trip → return 0 (GOLDEN_STANDARD P1).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "platform/visca_protocol.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

namespace {
bool approx(float a, float b, float eps = 1e-5f) { return std::abs(a - b) <= eps; }
}  // namespace

int runIoViscaProtocolSelfTest(bool injectBug) {
  using namespace sw::platform;
  bool ok = true;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G1. Set4ByteVal nibble split (cs:316-323). 500 = 0x01F4 → [0,1,15,4].
  //     -250 → (int16)0xFF06 → [15,15,0,6] (the sign wraps two's-complement BEFORE the split — the
  //     diverging middle a naive positive-only split would get wrong).
  {
    auto pn = viscaSet4ByteVal(500);
    ok = ok && pn[0] == 0 && pn[1] == 1 && pn[2] == 15 && pn[3] == 4;
    auto tn = viscaSet4ByteVal(-250);
    ok = ok && tn[0] == 15 && tn[1] == 15 && tn[2] == 0 && tn[3] == 6;
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G2. Input mapping (cs:281-285). pan 0.5 × range 1000 → 500 ; tilt -0.25 × 1000 → -250 ;
  //     zoom 0.5 × 0x4000 → 8192. Clamp: pan 2.0 clamps to 1.0 → range ; zoom -1 clamps to 0.
  ok = ok && viscaMapPan(0.5f, 1000) == 500;
  ok = ok && viscaMapPan(-0.25f, 1000) == -250;
  ok = ok && viscaMapPan(2.0f, 1000) == 1000;   // clamp high
  ok = ok && viscaMapPan(-3.0f, 1000) == -1000;  // clamp low
  ok = ok && viscaMapZoom(0.5f) == 8192;
  ok = ok && viscaMapZoom(-1.0f) == 0;           // clamp low

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G3. Pan/Tilt Absolute-Move command (cs:289-300). p=500, t=-250, speed 0x18:
  //     81 01 06 02 18 18 | 00 01 0F 04 | 0F 0F 00 06 | FF   (15 bytes).
  {
    auto cmd = viscaAbsoluteMovePanTiltCmd(/*p=*/500, /*t=*/-250);
    ok = ok && cmd.size() == 15u;
    const uint8_t exp[15] = {0x81, 0x01, 0x06, 0x02, 0x18, 0x18,
                             0x00, 0x01, 0x0F, 0x04, 0x0F, 0x0F, 0x00, 0x06, 0xFF};
    for (int i = 0; i < 15; ++i) ok = ok && cmd[i] == exp[i];
  }
  //     Zoom Absolute command (cs:305-311). z=8192 → 81 01 04 47 | 02 00 00 00 | FF (9 bytes).
  {
    auto cmd = viscaAbsoluteZoomCmd(8192);
    ok = ok && cmd.size() == 9u;
    const uint8_t exp[9] = {0x81, 0x01, 0x04, 0x47, 0x02, 0x00, 0x00, 0x00, 0xFF};
    for (int i = 0; i < 9; ++i) ok = ok && cmd[i] == exp[i];
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G4. VISCA-over-IP header wrap (cs:325-342). Wrap the 15-byte PT command with payloadType 0x0100
  //     (Command), sequence 0x01020304:
  //     header = 01 00 | 00 0F | 01 02 03 04   then the 15 cmd bytes → total 23 bytes.
  //     Bug 1 (little-endian header) flips the length/seq/type byte order → RED.
  setViscaBug(injectBug ? 1 : 0);
  {
    auto cmd = viscaAbsoluteMovePanTiltCmd(500, -250);
    auto pkt = viscaWrapOverIpCooked(cmd, /*payloadType=*/0x0100, /*seq=*/0x01020304);
    ok = ok && pkt.size() == 23u;
    // Header (big-endian): type 01 00, length 00 0F (15), seq 01 02 03 04.
    ok = ok && pkt[0] == 0x01 && pkt[1] == 0x00;
    ok = ok && pkt[2] == 0x00 && pkt[3] == 0x0F;
    ok = ok && pkt[4] == 0x01 && pkt[5] == 0x02 && pkt[6] == 0x03 && pkt[7] == 0x04;
    // Payload begins with the command's 0x81 sync.
    ok = ok && pkt[8] == 0x81;
  }
  setViscaBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G5. Pan/Tilt Inquiry-Response parse (cs:213-229). Build a canonical reply and read it back:
  //     header 01 11 | 00 0B (payloadLen 11) | 00 00 00 00 | payload[Y0 50 <pan nibbles> <tilt
  //     nibbles> FF]. pan nibbles = 500 → 0 1 F 4 ; tilt nibbles = -250 → F F 0 6. Range 1000 →
  //     pan 0.5, tilt -0.25 (the signed round-trip is the anchor). Bug 2 drops the &0x0F mask; here
  //     all nibbles are already ≤0x0F so we ALSO feed a dirty reply below to make the mask bite.
  {
    std::vector<uint8_t> reply = {0x01, 0x11, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00,
                                  0x90, 0x50, 0x00, 0x01, 0x0F, 0x04, 0x0F, 0x0F, 0x00, 0x06, 0xFF};
    auto pt = viscaParsePanTiltReply(reply.data(), reply.size(), /*panRange=*/1000, /*tiltRange=*/1000);
    ok = ok && pt.ok && approx(pt.pan, 0.5f) && approx(pt.tilt, -0.25f);
    // A non-reply (wrong 0x0111 magic) → ok=false.
    reply[0] = 0x02;
    auto bad = viscaParsePanTiltReply(reply.data(), reply.size(), 1000, 1000);
    ok = ok && !bad.ok;
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G6. The &0x0F mask (cs:214). A reply whose pan bytes carry HIGH bits (0xF1 instead of 0x01) must
  //     still parse as if masked (0x0F...) — TiXL masks each byte. Bug 2 drops the mask so the high
  //     bits leak → the reassembled pan diverges → RED. This is the mask's closed-form anchor.
  setViscaBug(injectBug ? 2 : 0);
  {
    // pan nibbles for 500 but with dirty high bits set on every payload byte: 0xF0 0xF1 0xFF 0xF4.
    std::vector<uint8_t> dirty = {0x01, 0x11, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00,
                                  0x90, 0x50, 0xF0, 0xF1, 0xFF, 0xF4, 0xFF, 0xFF, 0xF0, 0xF6, 0xFF};
    auto pt = viscaParsePanTiltReplyCooked(dirty.data(), dirty.size(), 1000, 1000);
    // Correctly masked → same as G5: pan 0.5, tilt -0.25.
    ok = ok && pt.ok && approx(pt.pan, 0.5f) && approx(pt.tilt, -0.25f);
  }
  setViscaBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G7. Zoom Inquiry-Response parse (cs:242-251). payloadLen 7 reply, zoom nibbles 8192 → 2 0 0 0.
  //     zoom = 8192 / 0x4000 = 0.5.
  {
    std::vector<uint8_t> reply = {0x01, 0x11, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00,
                                  0x90, 0x50, 0x02, 0x00, 0x00, 0x00, 0xFF};
    auto z = viscaParseZoomReply(reply.data(), reply.size());
    ok = ok && z.ok && approx(z.zoom, 0.5f);
  }

  // ── verdict ────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-visca-protocol] injectBug did NOT trip (a protocol tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-visca-protocol] injectBug correctly RED (corrupted VISCA byte path)\n");
    return 1;
  }
  std::printf("[selftest-io-visca-protocol] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-visca-protocol (order 713, after io-video-transform at 712).
REGISTER_SELFTESTS(/*orderBase=*/713, {"io-visca-protocol", sw::runIoViscaProtocolSelfTest});
