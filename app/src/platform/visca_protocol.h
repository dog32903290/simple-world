// platform/visca_protocol — the CLOSED-FORM VISCA-over-IP byte protocol of the ViscaCamera PTZ
// operator, factored out of the UDP socket so it verifies against hand-derived bytes without a camera.
//
// ZONE: platform (native interface family — the io byte-protocol siblings live here: wled_frame.h,
// websocket_frame.h, net_loopback.h). Pure byte assembly/parse, no socket, no zone crossing (includes
// no runtime/app/ui). The live UDP send/recv (System.Net.Sockets.UdpClient in TiXL; macOS would reuse
// net_loopback's POSIX UDP) is a DEFERRED device leg (census io/ptz R3, deferred-hw-verify) — exactly
// the split wled_frame/net_loopback already models (closed-form frame builder verified; live transport
// deferred).
//
// TiXL authority: external/tixl/Operators/Lib/io/ptz/ViscaCamera.cs
//   Set4ByteVal (cs:316-323)            — 16-bit value → 4 nibble bytes.
//   SendAbsoluteMoveAsync (cs:278-313)  — the pan/tilt + zoom Absolute-Move VISCA commands.
//   SendViscaCommandAsync (cs:325-342)  — the VISCA-over-IP header wrap (payloadType/length/seq).
//   ProcessPacket (cs:187-253)          — the pan/tilt + zoom Inquiry-Response parse.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace sw {
namespace platform {

// TiXL ViscaCamera.cs:316-323 — split a 16-bit value into 4 low-nibble bytes (MSN first). Note the
// C# cast to `short` first: negative values (e.g. a negative pan) wrap two's-complement into 16 bits
// BEFORE the nibble split, so the sign survives round-trip through the nibble reassembly on parse.
inline std::array<uint8_t, 4> viscaSet4ByteVal(int value) {
  const int16_t v = static_cast<int16_t>(value);
  const uint16_t u = static_cast<uint16_t>(v);
  return {static_cast<uint8_t>((u >> 12) & 0x0F), static_cast<uint8_t>((u >> 8) & 0x0F),
          static_cast<uint8_t>((u >> 4) & 0x0F), static_cast<uint8_t>(u & 0x0F)};
}

// TiXL ViscaCamera.cs:281-285 — map the normalized inputs to the device integer ranges:
//   p = (int)(clamp(pan,-1,1) * panRange)  ; t = (int)(clamp(tilt,-1,1) * tiltRange)
//   z = (int)(clamp(zoom, 0,1) * 0x4000)   (Sony standard zoom range)
// C# (int) truncates toward zero.
inline int viscaMapPan(float norm, int range) {
  float c = norm < -1.f ? -1.f : (norm > 1.f ? 1.f : norm);
  return static_cast<int>(c * range);
}
inline int viscaMapZoom(float norm) {
  float c = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);
  return static_cast<int>(c * static_cast<float>(0x4000));
}

// TiXL ViscaCamera.cs:289-300 — the Pan/Tilt Absolute-Move VISCA command (15 bytes):
//   81 01 06 02 <speed> <speed> <p0 p1 p2 p3> <t0 t1 t2 t3> FF
inline std::vector<uint8_t> viscaAbsoluteMovePanTiltCmd(int p, int t, uint8_t speed = 0x18) {
  std::vector<uint8_t> cmd(15);
  cmd[0] = 0x81; cmd[1] = 0x01; cmd[2] = 0x06; cmd[3] = 0x02;
  cmd[4] = speed; cmd[5] = speed;
  auto pn = viscaSet4ByteVal(p);
  auto tn = viscaSet4ByteVal(t);
  for (int i = 0; i < 4; ++i) cmd[6 + i] = pn[i];
  for (int i = 0; i < 4; ++i) cmd[10 + i] = tn[i];
  cmd[14] = 0xFF;
  return cmd;
}

// TiXL ViscaCamera.cs:305-311 — the Zoom Absolute-Move VISCA command (9 bytes):
//   81 01 04 47 <z0 z1 z2 z3> FF
inline std::vector<uint8_t> viscaAbsoluteZoomCmd(int z) {
  std::vector<uint8_t> cmd(9);
  cmd[0] = 0x81; cmd[1] = 0x01; cmd[2] = 0x04; cmd[3] = 0x47;
  auto zn = viscaSet4ByteVal(z);
  for (int i = 0; i < 4; ++i) cmd[4 + i] = zn[i];
  cmd[8] = 0xFF;
  return cmd;
}

// TiXL ViscaCamera.cs:325-342 — wrap a VISCA command in the VISCA-over-IP header (8 bytes) + payload:
//   [0-1] payloadType (big-endian)  [2-3] cmd length (big-endian)  [4-7] sequence (big-endian) then cmd
inline std::vector<uint8_t> viscaWrapOverIp(const std::vector<uint8_t>& cmd, uint16_t payloadType,
                                            uint32_t seq) {
  std::vector<uint8_t> pkt(8 + cmd.size());
  pkt[0] = static_cast<uint8_t>((payloadType >> 8) & 0xFF);
  pkt[1] = static_cast<uint8_t>(payloadType & 0xFF);
  pkt[2] = static_cast<uint8_t>((cmd.size() >> 8) & 0xFF);
  pkt[3] = static_cast<uint8_t>(cmd.size() & 0xFF);
  pkt[4] = static_cast<uint8_t>((seq >> 24) & 0xFF);
  pkt[5] = static_cast<uint8_t>((seq >> 16) & 0xFF);
  pkt[6] = static_cast<uint8_t>((seq >> 8) & 0xFF);
  pkt[7] = static_cast<uint8_t>(seq & 0xFF);
  for (size_t i = 0; i < cmd.size(); ++i) pkt[8 + i] = cmd[i];
  return pkt;
}

// TiXL ViscaCamera.cs:213-229 — parse a Pan/Tilt Inquiry Response payload. Reassemble two signed
// 16-bit values from their low nibbles, then normalize by the range:
//   panRaw  = nibbles[p2..p5] ; tiltRaw = nibbles[p6..p9]  (each masked & 0x0F)
//   pan = (short)panRaw / panRange ; tilt = (short)tiltRaw / tiltRange
// Returns ok=false when the packet is not a valid 0x0111 reply of payloadLen 11 with the 0x50/0xFF
// framing (matches the .cs guards cs:198/206/211).
struct ViscaPanTilt { bool ok = false; float pan = 0, tilt = 0; };
inline ViscaPanTilt viscaParsePanTiltReply(const uint8_t* data, size_t len, int panRange,
                                           int tiltRange) {
  ViscaPanTilt r;
  if (len < 9) return r;
  if (data[0] != 0x01 || data[1] != 0x11) return r;
  const size_t p = 8;  // payloadIndex
  if (len < p + 11) return r;
  if (data[p + 1] != 0x50 || data[p + 10] != 0xFF) return r;
  const int payloadLen = (data[2] << 8) | data[3];
  if (payloadLen != 11) return r;
  const int panRaw = ((data[p + 2] & 0x0F) << 12) | ((data[p + 3] & 0x0F) << 8) |
                     ((data[p + 4] & 0x0F) << 4) | (data[p + 5] & 0x0F);
  const int tiltRaw = ((data[p + 6] & 0x0F) << 12) | ((data[p + 7] & 0x0F) << 8) |
                      ((data[p + 8] & 0x0F) << 4) | (data[p + 9] & 0x0F);
  r.pan = static_cast<float>(static_cast<int16_t>(panRaw)) / static_cast<float>(panRange);
  r.tilt = static_cast<float>(static_cast<int16_t>(tiltRaw)) / static_cast<float>(tiltRange);
  r.ok = true;
  return r;
}

// TiXL ViscaCamera.cs:242-251 — parse a Zoom Inquiry Response payload (payloadLen 7):
//   zoomRaw = nibbles[p2..p5] ; zoom = zoomRaw / 0x4000
struct ViscaZoom { bool ok = false; float zoom = 0; };
inline ViscaZoom viscaParseZoomReply(const uint8_t* data, size_t len) {
  ViscaZoom r;
  if (len < 9) return r;
  if (data[0] != 0x01 || data[1] != 0x11) return r;
  const size_t p = 8;
  if (len < p + 7) return r;
  const int payloadLen = (data[2] << 8) | data[3];
  if (payloadLen != 7 || data[p + 1] != 0x50 || data[p + 6] != 0xFF) return r;
  const int zoomRaw = ((data[p + 2] & 0x0F) << 12) | ((data[p + 3] & 0x0F) << 8) |
                      ((data[p + 4] & 0x0F) << 4) | (data[p + 5] & 0x0F);
  r.zoom = static_cast<float>(zoomRaw) / static_cast<float>(0x4000);
  r.ok = true;
  return r;
}

// ── Golden TEETH hook (--selftest-io-visca-protocol). Sticky module switch; golden restores 0.
// 0 = production. Corrupts ONE real cook term so the closed-form assert goes RED (NOT a want-flip):
//   1 = big-endian → little-endian on the over-IP length/seq header (byte order flips)
//   2 = drop the &0x0F nibble mask on parse (high bits leak into the reassembled value)
inline int& viscaBugRef() { static int mode = 0; return mode; }
inline void setViscaBug(int mode) { viscaBugRef() = mode; }
inline int  viscaBug() { return viscaBugRef(); }

inline std::vector<uint8_t> viscaWrapOverIpCooked(const std::vector<uint8_t>& cmd,
                                                  uint16_t payloadType, uint32_t seq) {
  std::vector<uint8_t> pkt(8 + cmd.size());
  if (viscaBug() == 1) {  // TEETH: little-endian header
    pkt[0] = static_cast<uint8_t>(payloadType & 0xFF);
    pkt[1] = static_cast<uint8_t>((payloadType >> 8) & 0xFF);
    pkt[2] = static_cast<uint8_t>(cmd.size() & 0xFF);
    pkt[3] = static_cast<uint8_t>((cmd.size() >> 8) & 0xFF);
    pkt[4] = static_cast<uint8_t>(seq & 0xFF);
    pkt[5] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    pkt[6] = static_cast<uint8_t>((seq >> 16) & 0xFF);
    pkt[7] = static_cast<uint8_t>((seq >> 24) & 0xFF);
    for (size_t i = 0; i < cmd.size(); ++i) pkt[8 + i] = cmd[i];
    return pkt;
  }
  return viscaWrapOverIp(cmd, payloadType, seq);
}

inline ViscaPanTilt viscaParsePanTiltReplyCooked(const uint8_t* data, size_t len, int panRange,
                                                 int tiltRange) {
  if (viscaBug() != 2) return viscaParsePanTiltReply(data, len, panRange, tiltRange);
  // TEETH: reassemble WITHOUT the &0x0F mask so any non-zero high nibble corrupts the value.
  ViscaPanTilt r;
  if (len < 9 || data[0] != 0x01 || data[1] != 0x11) return r;
  const size_t p = 8;
  if (len < p + 11 || data[p + 1] != 0x50 || data[p + 10] != 0xFF) return r;
  if (((data[2] << 8) | data[3]) != 11) return r;
  const int panRaw = (data[p + 2] << 12) | (data[p + 3] << 8) | (data[p + 4] << 4) | data[p + 5];
  const int tiltRaw = (data[p + 6] << 12) | (data[p + 7] << 8) | (data[p + 8] << 4) | data[p + 9];
  r.pan = static_cast<float>(static_cast<int16_t>(panRaw)) / static_cast<float>(panRange);
  r.tilt = static_cast<float>(static_cast<int16_t>(tiltRaw)) / static_cast<float>(tiltRange);
  r.ok = true;
  return r;
}

}  // namespace platform
}  // namespace sw
