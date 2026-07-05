// io_onvif_digest_golden — OnvifCamera WS-Security PasswordDigest golden
// (--selftest-io-onvif-digest).
//
// Layer of the io/ptz OnvifCamera: the ONE closed-form piece worth a tooth — the auth digest, a
// deterministic function of (nonce, created, password). Everything else (ONVIF SOAP discovery, RTSP
// stream, OpenCV VideoCapture) is device/network-bound → DEFERRED (census io/ptz R3,
// deferred-hw-verify).
//
// TiXL authority: OnvifCamera.cs:708-713 — digest = Base64(SHA1(nonce ++ created ++ password)),
// WS-Security UsernameToken Profile 1.0 #PasswordDigest. Canonical vector cross-checked against
// Python hashlib/base64 (offline). injectBug drives platform/onvif_digest.h's setOnvifBug (real
// SHA1-input corruption, NOT a want-flip). did-not-trip → return 0 (GOLDEN_STANDARD P1).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/onvif_digest.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

int runIoOnvifDigestSelfTest(bool injectBug) {
  using namespace sw::platform;
  bool ok = true;

  // Canonical vector: nonce = bytes 0x00..0x0F, created = "2020-01-01T00:00:00.000Z", password varies.
  // Cross-checked: base64(SHA1(nonce ++ created ++ pw)) via Python hashlib/base64.
  std::vector<uint8_t> nonce(16);
  for (int i = 0; i < 16; ++i) nonce[i] = static_cast<uint8_t>(i);
  const std::string created = "2020-01-01T00:00:00.000Z";

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G1. password "password" → digest "tWFdVdn67OYFRmnYqy45hASnpyg=".
  //     Bug 1 reorders the SHA1 input (password before nonce) → a totally different digest → RED.
  setOnvifBug(injectBug ? 1 : 0);
  ok = ok && onvifPasswordDigest(nonce, created, "password") == "tWFdVdn67OYFRmnYqy45hASnpyg=";
  setOnvifBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G2. A DIFFERENT password ("admin") → a DIFFERENT digest ("8Ei9i4fq0Xtr6pOMBUH5x33SNtE="). Two
  //     distinct vectors prove the assert is bound to the real password bytes, not a constant.
  ok = ok && onvifPasswordDigest(nonce, created, "admin") == "8Ei9i4fq0Xtr6pOMBUH5x33SNtE=";

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G3. Digest length + base64 shape: SHA1 is 20 bytes → base64 is 28 chars ending in '='.
  {
    std::string d = onvifPasswordDigest(nonce, created, "password");
    ok = ok && d.size() == 28u && d.back() == '=';
  }

  // ── verdict ────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-onvif-digest] injectBug did NOT trip (the digest tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-onvif-digest] injectBug correctly RED (corrupted WS-Security digest)\n");
    return 1;
  }
  std::printf("[selftest-io-onvif-digest] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-onvif-digest (order 714, after io-visca-protocol at 713).
REGISTER_SELFTESTS(/*orderBase=*/714, {"io-onvif-digest", sw::runIoOnvifDigestSelfTest});
