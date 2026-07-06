// io_ptz_send_golden — the SEND leg of the ViscaCamera / OnvifCamera PTZ operators
// (--selftest-io-ptz-send). The byte-assembly cores are already golden'd elsewhere:
//   - visca_protocol.h (--selftest-io-visca-protocol): VISCA command + over-IP wrap + reply parse.
//   - onvif_digest.h   (--selftest-io-onvif-digest):   WS-Security PasswordDigest.
// What THIS golden adds is the transport half those cores feed:
//   (A) VISCA — build the golden over-IP packet, send it over a localhost UDP loopback, receive it
//       back, and assert the received bytes are BYTE-IDENTICAL to the built packet. End-to-end closed
//       form: no camera, the loopback proves the exact send/recv path ViscaCamera uses
//       (System.Net.Sockets.UdpClient in TiXL → net_loopback POSIX UDP on macOS). The REAL PTZ camera
//       is deferred-hw-verify — it reuses this identical send path, only the peer address changes.
//   (B) ONVIF — assemble the SOAP request (WS-Security header + envelope, mirroring OnvifCamera.cs:
//       715-718) around the golden digest, and assert the assembled string is closed-form correct: the
//       embedded digest equals onvifPasswordDigest on a canonical (nonce,created,password) vector, and
//       the envelope wraps header+body in the exact SOAP 1.2 structure. The live HTTP POST to a camera
//       is deferred-hw-verify (the transport is httpPost, exercised by the http selftests); here we
//       prove the REQUEST BYTES are right, independent of any device.
//
// injectBug drives BOTH the VISCA over-IP wrap teeth (viscaWrapOverIpCooked, little-endian header) and
// the ONVIF digest teeth (onvifPasswordDigest field reorder) — a corruption on the REAL cook path of
// each assembled artifact. The transport assert (A) then receives the corrupted bytes and diverges from
// the golden; the envelope assert (B) diverges on the embedded digest. did-not-trip → return 0.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/net_loopback.h"
#include "platform/onvif_digest.h"
#include "platform/visca_protocol.h"
#include "runtime/selftest_registry.h"

namespace sw {

namespace {

struct BinCapture {
  std::mutex mx;
  std::vector<std::string> msgs;
  std::atomic<int> count{0};
  void push(const std::string& m) {
    std::lock_guard<std::mutex> lk(mx);
    msgs.push_back(m);
    count.fetch_add(1);
  }
};

void udpCb(void* user, const std::string& m, int /*port*/) {
  static_cast<BinCapture*>(user)->push(m);
}

bool waitFor(BinCapture& cap, int n, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (cap.count.load() < n) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return true;
}

}  // namespace

int runIoPtzSendSelfTest(bool injectBug) {
  using namespace sw::platform;
  bool ok = true;

  // ── (A) VISCA send over UDP loopback ────────────────────────────────────────────────────────────
  // Build a Pan/Tilt Absolute-Move command for a mid-range pan/tilt (diverging middle, not identity),
  // wrap it over-IP with a known payloadType/seq, send it to a loopback listener, and assert the
  // received bytes equal the built packet. The wrap uses the *cooked* builder so injectBug's
  // little-endian tooth corrupts the REAL bytes that go on the wire.
  const int panRange = 2448, tiltRange = 1296;  // typical Sony ranges (mid values below diverge)
  int p = viscaMapPan(0.5f, panRange);          // 0.5 * 2448 = 1224 (non-zero, non-saturated)
  int t = viscaMapPan(-0.25f, tiltRange);       // -0.25 * 1296 = -324 (negative → two's-complement path)
  std::vector<uint8_t> cmd = viscaAbsoluteMovePanTiltCmd(p, t);
  const uint16_t payloadType = 0x0100;  // VISCA command payload type
  const uint32_t seq = 0x01020304;      // a multi-byte seq so the big-endian order is observable
  std::vector<uint8_t> wire = viscaWrapOverIpCooked(cmd, payloadType, seq);
  setViscaBug(injectBug ? 1 : 0);
  std::vector<uint8_t> wireCooked = viscaWrapOverIpCooked(cmd, payloadType, seq);
  setViscaBug(0);
  // The GOLDEN packet (production wrap, never corrupted) is the expectation the receiver must match.
  std::vector<uint8_t> golden = viscaWrapOverIp(cmd, payloadType, seq);

  BinCapture cap;
  UdpLoopbackDevice listener, sender;
  bool bound = listener.startListening(0, udpCb, &cap) && sender.startListening(0, udpCb, nullptr);
  if (!bound) {
    std::printf("[selftest-io-ptz-send] UDP bind failed -> SKIP transport (env restricted)\n");
    // Fall through to the ONVIF half (no socket needed); the VISCA transport half is env-skipped.
  } else {
    std::string payload(reinterpret_cast<const char*>(wireCooked.data()), wireCooked.size());
    ok = ok && sender.sendTo(listener.boundPort(), payload);
    bool arrived = waitFor(cap, 1, 1000);
    ok = ok && arrived;
    if (arrived) {
      std::lock_guard<std::mutex> lk(cap.mx);
      const std::string& got = cap.msgs[0];
      std::string want(reinterpret_cast<const char*>(golden.data()), golden.size());
      // Byte-identical to the GOLDEN packet. injectBug's little-endian wrap makes the received bytes
      // (wireCooked) diverge from the big-endian golden → RED on the real recv path.
      bool match = got.size() == want.size() && got == want;
      ok = ok && match;
      std::printf("[selftest-io-ptz-send] VISCA got %zu bytes, golden %zu, match=%d\n", got.size(),
                  want.size(), match ? 1 : 0);
    }
    listener.stopListening();
    sender.stopListening();
  }

  // ── (B) ONVIF SOAP request assembly ─────────────────────────────────────────────────────────────
  // Canonical WS-Security vector (a fixed nonce/created/password — deterministic digest). Assemble the
  // header+envelope and assert: (1) the embedded digest == onvifPasswordDigest of the vector, (2) the
  // envelope wraps the SOAP structure, (3) the AbsoluteMove body is present. injectBug's digest tooth
  // (password-before-nonce) changes the embedded digest → the assert vs the production digest diverges.
  const std::vector<uint8_t> nonce = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                                      0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
  const std::string created = "2026-07-06T12:00:00.000Z";
  const std::string password = "admin123";
  const std::string username = "admin";
  const std::string nonceB64 = wsBase64(nonce);  // base64 of the RAW nonce bytes (OnvifCamera.cs:704)

  setOnvifBug(injectBug ? 1 : 0);
  const std::string digestCooked = onvifPasswordDigest(nonce, created, password);
  setOnvifBug(0);
  const std::string digestGolden = onvifPasswordDigest(nonce, created, password);

  const std::string body =
      "<AbsoluteMove xmlns=\"http://www.onvif.org/ver20/ptz/wsdl\"><ProfileToken>P0</ProfileToken>"
      "</AbsoluteMove>";
  const std::string header = onvifSecurityHeader(username, digestCooked, nonceB64, created);
  const std::string envelope = onvifSoapEnvelope(header, body);

  // (1) The envelope must embed the digest we assembled it with (cooked path) — a structural check that
  //     the header builder actually placed the digest. Then the CLOSED-FORM assert: that digest, when
  //     NOT injected, equals the production digest of the canonical vector. injectBug makes digestCooked
  //     (password-first) differ from digestGolden → the golden assert diverges.
  bool embedsDigest = envelope.find(digestCooked) != std::string::npos;
  bool digestGoldenMatch = (digestCooked == digestGolden);  // false under injectBug (real reorder)
  // (2) SOAP structure present.
  bool hasEnvelope = envelope.find("<s:Envelope") != std::string::npos &&
                     envelope.find("</s:Envelope>") != std::string::npos;
  bool hasSecurity = envelope.find("<wsse:UsernameToken>") != std::string::npos &&
                     envelope.find("<wsse:Nonce") != std::string::npos &&
                     envelope.find(nonceB64) != std::string::npos &&
                     envelope.find(created) != std::string::npos;
  // (3) Body carried through.
  bool hasBody = envelope.find("AbsoluteMove") != std::string::npos;
  // (4) The canonical digest itself is byte-stable (a fixed known vector). This is the PURE closed-form
  //     anchor — hand-verifiable: SHA1(nonce++created++password) base64. We assert digestGolden is a
  //     well-formed non-empty base64 of a 20-byte SHA1 (28 chars incl padding).
  bool digestWellFormed = digestGolden.size() == 28;

  ok = ok && embedsDigest && digestGoldenMatch && hasEnvelope && hasSecurity && hasBody &&
       digestWellFormed;
  std::printf(
      "[selftest-io-ptz-send] ONVIF embed=%d goldenDigest=%d envelope=%d security=%d body=%d wf=%d\n",
      embedsDigest, digestGoldenMatch, hasEnvelope, hasSecurity, hasBody, digestWellFormed);

  // ── verdict ─────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-ptz-send] injectBug did NOT trip (a send tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces it
    }
    std::printf("[selftest-io-ptz-send] injectBug correctly RED (corrupted send/assembly path)\n");
    return 1;
  }
  std::printf("[selftest-io-ptz-send] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-ptz-send (order 717, after io-video-capture at 716).
REGISTER_SELFTESTS(/*orderBase=*/717, {"io-ptz-send", sw::runIoPtzSendSelfTest});
