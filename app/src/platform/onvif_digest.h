// platform/onvif_digest — the CLOSED-FORM WS-Security UsernameToken PasswordDigest of the OnvifCamera
// operator, factored out of the SOAP/HTTP/RTSP stack so it verifies against a canonical vector without
// a camera.
//
// ZONE: platform (native interface family; sits beside websocket_frame.h whose SHA1+base64 it reuses).
// The rest of OnvifCamera — ONVIF SOAP discovery, RTSP stream, OpenCV VideoCapture decode — is
// device/network-bound and DEFERRED (census io/ptz R3, deferred-hw-verify). What is closed-form and
// worth a tooth is the auth digest: it is a fixed function of (nonce, created, password).
//
// TiXL authority: external/tixl/Operators/Lib/io/ptz/OnvifCamera.cs:708-713 —
//   combined = nonce ++ UTF8(created) ++ UTF8(password)
//   digest   = Base64( SHA1(combined) )
// (WS-Security UsernameToken Profile 1.0 #PasswordDigest.) The nonce is random per request in
// production, but the digest FUNCTION is deterministic — that determinism is what this verifies.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/websocket_frame.h"  // wsSha1Raw + wsBase64 (RFC3174 SHA1 + RFC4648 base64)

namespace sw {
namespace platform {

// ── Golden TEETH hook (--selftest-io-onvif-digest). Sticky module switch; golden restores 0.
// 0 = production. 1 = concatenate password BEFORE nonce (wrong field order) → digest diverges (this
// is a real cook-path corruption of the SHA1 input, NOT a want-flip).
inline int& onvifBugRef() { static int mode = 0; return mode; }
inline void setOnvifBug(int mode) { onvifBugRef() = mode; }
inline int  onvifBug() { return onvifBugRef(); }

// TiXL OnvifCamera.cs:708-713 — the WS-Security PasswordDigest. `nonce` is the RAW random bytes (NOT
// the base64 form that goes in the <Nonce> element); `created` and `password` are UTF-8 strings.
inline std::string onvifPasswordDigest(const std::vector<uint8_t>& nonce,
                                       const std::string& created,
                                       const std::string& password) {
  std::vector<uint8_t> combined;
  combined.reserve(nonce.size() + created.size() + password.size());
  if (onvifBug() == 1) {
    // TEETH: password before nonce — a real reordering of the hashed input.
    combined.insert(combined.end(), password.begin(), password.end());
    combined.insert(combined.end(), nonce.begin(), nonce.end());
    combined.insert(combined.end(), created.begin(), created.end());
  } else {
    combined.insert(combined.end(), nonce.begin(), nonce.end());
    combined.insert(combined.end(), created.begin(), created.end());
    combined.insert(combined.end(), password.begin(), password.end());
  }
  return wsBase64(wsSha1Raw(std::string(combined.begin(), combined.end())));
}

}  // namespace platform
}  // namespace sw
