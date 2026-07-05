// platform/websocket_frame.cpp — RFC6455 handshake + frame codec + minimal SHA-1/base64.
// Ground-truth lines cited in websocket_frame.h. Pure functions, no I/O.
#include "platform/websocket_frame.h"

#include <array>
#include <cstring>

namespace sw {
namespace {

// ---- minimal SHA-1 (RFC3174) --------------------------------------------------------------------
uint32_t rotl32(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }

// Compute the raw 20-byte SHA-1 digest of `in`.
std::array<uint8_t, 20> sha1Raw(const std::string& in) {
  uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

  std::vector<uint8_t> msg(in.begin(), in.end());
  const uint64_t bitLen = uint64_t(msg.size()) * 8;
  msg.push_back(0x80);
  while (msg.size() % 64 != 56) msg.push_back(0x00);
  for (int i = 7; i >= 0; --i) msg.push_back(uint8_t((bitLen >> (i * 8)) & 0xFF));

  for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(msg[chunk + i * 4]) << 24) | (uint32_t(msg[chunk + i * 4 + 1]) << 16) |
             (uint32_t(msg[chunk + i * 4 + 2]) << 8) | uint32_t(msg[chunk + i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
      uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rotl32(b, 30); b = a; a = tmp;
    }
    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
  }

  std::array<uint8_t, 20> out{};
  uint32_t hs[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; ++i) {
    out[i * 4 + 0] = uint8_t((hs[i] >> 24) & 0xFF);
    out[i * 4 + 1] = uint8_t((hs[i] >> 16) & 0xFF);
    out[i * 4 + 2] = uint8_t((hs[i] >> 8) & 0xFF);
    out[i * 4 + 3] = uint8_t(hs[i] & 0xFF);
  }
  return out;
}

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

std::string wsBase64(const std::vector<uint8_t>& in) {
  std::string out;
  size_t i = 0;
  while (i + 2 < in.size()) {
    uint32_t n = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) | uint32_t(in[i + 2]);
    out.push_back(kB64[(n >> 18) & 63]);
    out.push_back(kB64[(n >> 12) & 63]);
    out.push_back(kB64[(n >> 6) & 63]);
    out.push_back(kB64[n & 63]);
    i += 3;
  }
  const size_t rem = in.size() - i;
  if (rem == 1) {
    uint32_t n = uint32_t(in[i]) << 16;
    out.push_back(kB64[(n >> 18) & 63]);
    out.push_back(kB64[(n >> 12) & 63]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    uint32_t n = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8);
    out.push_back(kB64[(n >> 18) & 63]);
    out.push_back(kB64[(n >> 12) & 63]);
    out.push_back(kB64[(n >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

std::string wsSha1Hex(const std::string& in) {
  auto d = sha1Raw(in);
  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(40);
  for (uint8_t byte : d) {
    out.push_back(hex[byte >> 4]);
    out.push_back(hex[byte & 0xF]);
  }
  return out;
}

std::string wsComputeAcceptKey(const std::string& clientKey) {
  // RFC6455 §1.3: base64( SHA1( key + magic GUID ) ).
  static const char* kMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  auto digest = sha1Raw(clientKey + kMagic);
  return wsBase64(std::vector<uint8_t>(digest.begin(), digest.end()));
}

std::vector<uint8_t> wsEncodeFrame(WsOpcode opcode, const std::string& payload, bool mask,
                                   const uint8_t maskKey[4]) {
  std::vector<uint8_t> f;
  f.push_back(uint8_t(0x80 | (uint8_t(opcode) & 0x0F)));  // FIN=1 + opcode

  const uint64_t n = payload.size();
  const uint8_t maskBit = mask ? 0x80 : 0x00;
  if (n <= 125) {
    f.push_back(uint8_t(maskBit | uint8_t(n)));
  } else if (n <= 0xFFFF) {
    f.push_back(uint8_t(maskBit | 126));
    f.push_back(uint8_t((n >> 8) & 0xFF));
    f.push_back(uint8_t(n & 0xFF));
  } else {
    f.push_back(uint8_t(maskBit | 127));
    for (int i = 7; i >= 0; --i) f.push_back(uint8_t((n >> (i * 8)) & 0xFF));
  }

  if (mask) {
    for (int i = 0; i < 4; ++i) f.push_back(maskKey[i]);
    for (size_t i = 0; i < payload.size(); ++i)
      f.push_back(uint8_t(uint8_t(payload[i]) ^ maskKey[i % 4]));  // §5.3 masking
  } else {
    f.insert(f.end(), payload.begin(), payload.end());
  }
  return f;
}

WsFrame wsDecodeFrame(const uint8_t* data, size_t len) {
  WsFrame r;
  if (len < 2) return r;  // need at least FIN/opcode + len byte

  size_t pos = 0;
  const uint8_t b0 = data[pos++];
  const uint8_t b1 = data[pos++];
  r.fin = (b0 & 0x80) != 0;
  r.opcode = WsOpcode(b0 & 0x0F);
  r.masked = (b1 & 0x80) != 0;

  uint64_t payloadLen = b1 & 0x7F;
  if (payloadLen == 126) {
    if (len < pos + 2) return r;
    payloadLen = (uint64_t(data[pos]) << 8) | uint64_t(data[pos + 1]);
    pos += 2;
  } else if (payloadLen == 127) {
    if (len < pos + 8) return r;
    payloadLen = 0;
    for (int i = 0; i < 8; ++i) payloadLen = (payloadLen << 8) | uint64_t(data[pos + i]);
    pos += 8;
  }

  uint8_t key[4] = {0, 0, 0, 0};
  if (r.masked) {
    if (len < pos + 4) return r;
    for (int i = 0; i < 4; ++i) key[i] = data[pos + i];
    pos += 4;
  }

  if (len < pos + payloadLen) return r;  // full payload not yet buffered
  r.payload.resize(size_t(payloadLen));
  for (uint64_t i = 0; i < payloadLen; ++i) {
    uint8_t byte = data[pos + i];
    if (r.masked) byte ^= key[i % 4];
    r.payload[size_t(i)] = char(byte);
  }
  pos += size_t(payloadLen);

  r.consumed = pos;
  r.ok = true;
  return r;
}

}  // namespace sw
