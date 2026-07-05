// platform/freed_packet — pure closed-form packet codec for the FreeD camera-tracking family
// (FreeDInput parse / FreeDOutput build). The socket transport is the same thin net_loopback UDP seam;
// the VALUE of these ops is the 29-byte FreeD "D1" message layout — a deterministic buffer builder /
// parser (identical shape to platform/dmx_packet). Exposing it as pure functions keeps the golden
// byte-for-byte independent of the transport: a golden hand-推s the exact wire bytes from the TiXL .cs
// and asserts them; the real socket send/receive (deferred-hw-verify) just hands these bytes to
// net_loopback.
//
// platform leaf: pure computation (std::vector<uint8_t>), no sockets, no runtime/UI, no upward dep. The
// socket WRITE/READ path lives in net_loopback (UDP); this file is ONLY the codec.
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/freed/FreeDOutput.cs:83-120  BuildFreeDPacket — id 0xD1, cameraId, 8×int24 BE
//     (rot XYZ ×32768, pos XYZ ×64000, zoom, focus) rounded, then user u16 BE, then checksum byte.
//   Operators/Lib/io/freed/FreeDInput.cs:163-203   ParseFreeDDataPacket — validate len==29 + id 0xD1 +
//     checksum; read int24 BE (sign-extended) rot/pos ÷scale, uint24 BE zoom/focus, u16 user.
//   Checksum (both files): (byte)(0x40 - Σ data[0..len-1]).  AngleScale=32768, PositionToMeterScale=64000.
#pragma once
#include <cstdint>
#include <vector>

namespace sw {

// FreeD "D1" wire constants (FreeDInput.cs:309-312 / FreeDOutput.cs:245-248).
constexpr int   kFreeDPacketLength = 29;
constexpr uint8_t kFreeDIdentifier = 0xD1;
constexpr float kFreeDAngleScale   = 32768.0f;   // rotation degrees → int24 (×), int24 → degrees (÷)
constexpr float kFreeDPositionScale = 64000.0f;  // position metres  → int24 (×), int24 → metres  (÷)

// Build one 29-byte FreeD "D1" packet (FreeDOutput.cs BuildFreeDPacket :83-120).
//   byte[0]      = 0xD1 identifier
//   byte[1]      = cameraId
//   byte[2..10]  = Pan/Tilt/Roll  = round(rotation.{x,y,z} * 32768) as int24 BE
//   byte[11..19] = PosX/PosY/PosZ = round(position.{x,y,z} * 64000) as int24 BE
//   byte[20..22] = zoom  (int24 BE, caller clamps 0..0xFFFFFF)
//   byte[23..25] = focus (int24 BE, caller clamps 0..0xFFFFFF)
//   byte[26..27] = user  (u16 BE, caller clamps 0..0xFFFF)
//   byte[28]     = checksum = (byte)(0x40 - Σ bytes[0..27])
// rotation/position are the raw degrees / metres (the ×scale + round happen here).
std::vector<uint8_t> buildFreeDPacket(uint8_t cameraId,
                                      float rotX, float rotY, float rotZ,
                                      float posX, float posY, float posZ,
                                      int zoom, int focus, int user);

// The parsed contents of a FreeD "D1" packet (FreeDInput.cs ParseFreeDDataPacket :163-203). `ok` false =
// not a valid packet (wrong length / wrong id / bad checksum). Rotation/position are the DECODED degrees
// / metres (int24 ÷ scale); zoom/focus are uint24; user is u16.
struct FreeDParsed {
  bool  ok = false;
  uint8_t cameraId = 0;
  float rotX = 0, rotY = 0, rotZ = 0;  // degrees (int24 / 32768)
  float posX = 0, posY = 0, posZ = 0;  // metres  (int24 / 64000)
  int   zoom = 0, focus = 0, user = 0;
};
FreeDParsed parseFreeDPacket(const std::vector<uint8_t>& packet);

// The checksum byte for `len` bytes of `data` (FreeD*.cs CalculateFreeDChecksum): (byte)(0x40 - Σ).
uint8_t freeDChecksum(const std::vector<uint8_t>& data, int len);

}  // namespace sw
