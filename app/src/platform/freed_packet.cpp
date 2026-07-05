// platform/freed_packet — impl. See freed_packet.h for the wire spec + TiXL line citations.
#include "platform/freed_packet.h"

#include <cmath>

namespace sw {

namespace {

// int24 big-endian write into buf (FreeDOutput.cs WriteInt24BigEndian :109-114).
void writeInt24BE(std::vector<uint8_t>& buf, int value) {
  buf.push_back((uint8_t)((value >> 16) & 0xFF));
  buf.push_back((uint8_t)((value >> 8) & 0xFF));
  buf.push_back((uint8_t)(value & 0xFF));
}

// int24 big-endian read, SIGN-EXTENDED (FreeDInput.cs ReadInt24BigEndian :187-193).
int readInt24BE(const std::vector<uint8_t>& b, int off) {
  int value = (b[off] << 16) | (b[off + 1] << 8) | b[off + 2];
  if ((b[off] & 0x80) != 0) value |= (int)0xFF000000;  // sign-extend
  return value;
}

// uint24 big-endian read (FreeDInput.cs ReadUInt24BigEndian :195-198).
int readUInt24BE(const std::vector<uint8_t>& b, int off) {
  return (b[off] << 16) | (b[off + 1] << 8) | b[off + 2];
}

}  // namespace

uint8_t freeDChecksum(const std::vector<uint8_t>& data, int len) {
  // (byte)(0x40 - Σ data[0..len-1]) — unsigned 32-bit accumulate then narrow (FreeD*.cs).
  uint32_t sum = 0;
  for (int i = 0; i < len; ++i) sum += data[i];
  return (uint8_t)(0x40u - sum);
}

std::vector<uint8_t> buildFreeDPacket(uint8_t cameraId,
                                      float rotX, float rotY, float rotZ,
                                      float posX, float posY, float posZ,
                                      int zoom, int focus, int user) {
  std::vector<uint8_t> p;
  p.reserve(kFreeDPacketLength);
  p.push_back(kFreeDIdentifier);  // byte[0] (cs:86)
  p.push_back(cameraId);          // byte[1] (cs:87)

  // Pan/Tilt/Roll: round(deg * 32768) int24 BE (cs:89-91). std::lround = half-away-from-zero; TiXL
  // MathF.Round = banker's — the goldens choose values that don't land on .5 so both agree.
  writeInt24BE(p, (int)std::lround(rotX * kFreeDAngleScale));
  writeInt24BE(p, (int)std::lround(rotY * kFreeDAngleScale));
  writeInt24BE(p, (int)std::lround(rotZ * kFreeDAngleScale));
  // PosX/Y/Z: round(m * 64000) int24 BE (cs:93-95).
  writeInt24BE(p, (int)std::lround(posX * kFreeDPositionScale));
  writeInt24BE(p, (int)std::lround(posY * kFreeDPositionScale));
  writeInt24BE(p, (int)std::lround(posZ * kFreeDPositionScale));
  // Zoom / Focus int24 BE (cs:97-98).
  writeInt24BE(p, zoom);
  writeInt24BE(p, focus);
  // User u16 BE (cs:100-101).
  p.push_back((uint8_t)((user >> 8) & 0xFF));
  p.push_back((uint8_t)(user & 0xFF));

  // Checksum over the first 28 bytes into byte[28] (cs:104).
  p.push_back(0);
  p[kFreeDPacketLength - 1] = freeDChecksum(p, kFreeDPacketLength - 1);
  return p;
}

FreeDParsed parseFreeDPacket(const std::vector<uint8_t>& data) {
  FreeDParsed r;
  // Validate length + id + checksum (FreeDInput.cs:165-166).
  if ((int)data.size() != kFreeDPacketLength) return r;
  if (data[0] != kFreeDIdentifier) return r;
  if (data[kFreeDPacketLength - 1] != freeDChecksum(data, kFreeDPacketLength - 1)) return r;

  r.cameraId = data[1];                                          // cs:173
  r.rotX = readInt24BE(data, 2) / kFreeDAngleScale;              // cs:174
  r.rotY = readInt24BE(data, 5) / kFreeDAngleScale;
  r.rotZ = readInt24BE(data, 8) / kFreeDAngleScale;
  r.posX = readInt24BE(data, 11) / kFreeDPositionScale;          // cs:176-178
  r.posY = readInt24BE(data, 14) / kFreeDPositionScale;
  r.posZ = readInt24BE(data, 17) / kFreeDPositionScale;
  r.zoom  = readUInt24BE(data, 20);                              // cs:168
  r.focus = readUInt24BE(data, 23);                              // cs:169
  r.user  = (data[26] << 8) | data[27];                         // cs:170
  r.ok = true;
  return r;
}

}  // namespace sw
