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

// The Content-Type of an ONVIF SOAP request (OnvifCamera.cs:720 StringContent .. "application/soap+xml").
inline const char* onvifSoapContentType() { return "application/soap+xml"; }

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

// TiXL OnvifCamera.cs:715 — the WS-Security UsernameToken SOAP header. `nonceBase64` is the base64 of
// the RAW nonce bytes (the same nonce fed to onvifPasswordDigest); `digest` is that function's output;
// `created` is the UTC timestamp string. Assembled VERBATIM from the .cs interpolated string so the
// header is byte-identical to what TiXL sends (only the runtime nonce/created/digest values vary). Empty
// username → no header (cs:699 the if-guard), matching TiXL's unauthenticated request.
inline std::string onvifSecurityHeader(const std::string& username, const std::string& digest,
                                       const std::string& nonceBase64, const std::string& created) {
  if (username.empty()) return "";
  return
      "<s:Header><wsse:Security xmlns:wsse=\"http://docs.oasis-open.org/wss/2004/01/"
      "oasis-200401-wss-wssecurity-secext-1.0.xsd\" xmlns:wsu=\"http://docs.oasis-open.org/wss/2004/01/"
      "oasis-200401-wss-wssecurity-utility-1.0.xsd\"><wsse:UsernameToken><wsse:Username>" +
      username +
      "</wsse:Username><wsse:Password Type=\"http://docs.oasis-open.org/wss/2004/01/"
      "oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">" +
      digest +
      "</wsse:Password><wsse:Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/"
      "oasis-200401-wss-soap-message-security-1.0#Base64Binary\">" +
      nonceBase64 + "</wsse:Nonce><wsu:Created>" + created +
      "</wsu:Created></wsse:UsernameToken></wsse:Security></s:Header>";
}

// TiXL OnvifCamera.cs:718 — wrap a SOAP body in the full SOAP 1.2 envelope with the (optional) security
// header. `header` is onvifSecurityHeader's output (or ""); `body` is the operation body (e.g. the
// AbsoluteMove / GetSystemDateAndTime XML). Byte-identical to the .cs interpolation.
inline std::string onvifSoapEnvelope(const std::string& header, const std::string& body) {
  return
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">" +
      header +
      "<s:Body xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
      "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">" +
      body + "</s:Body></s:Envelope>";
}

}  // namespace platform
}  // namespace sw
