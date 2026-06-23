// platform/osc_loopback.cpp — localhost UDP socket + self-rolled OSC 1.0 decode/encode.
//
// Ground truth (external/tixl, read-only):
//   Core/IO/OscConnectionManager.cs:140-160  receive loop: OscBundle → iterate messages;
//                                             OscMessage → ForwardMessage to consumers.
//   Core/IO/OscConnectionManager.cs:219-231  TryGetFloatFromMessagePart: float/int/bool/double/
//                                             string→float.TryParse else NaN; return !IsNaN.
// TiXL uses the Rug.Osc NuGet for the binary decode (not portable to C++), so the wire-format parse
// here is self-rolled per the OSC 1.0 spec (4-byte aligned address + ",tags" + big-endian args).
// The float COERCION mirrors TryGetFloatFromMessagePart byte-for-byte.
//
// platform leaf: pure POSIX sockets + a std::thread receive loop; no runtime / app includes.
#include "platform/osc_loopback.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace sw {
namespace {

// OSC pads every string / blob up to a 4-byte boundary. Returns the padded length of `n` bytes.
int oscPad4(int n) { return (n + 3) & ~3; }

// Read a 4-byte-aligned, NUL-terminated OSC string starting at buf[pos]. Advances pos past padding.
// Returns false if it would run off the end.
bool readOscString(const uint8_t* buf, int len, int& pos, std::string& out) {
  int start = pos;
  while (pos < len && buf[pos] != 0) pos++;
  if (pos >= len) return false;  // no terminator within bounds
  out.assign(reinterpret_cast<const char*>(buf + start), pos - start);
  // advance past the NUL, then up to the 4-byte boundary of the WHOLE string (incl. terminator)
  int strLenWithNul = (pos - start) + 1;
  pos = start + oscPad4(strLenWithNul);
  return pos <= len;
}

float beToFloat(const uint8_t* p) {
  uint32_t u = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
  float f;
  std::memcpy(&f, &u, sizeof(f));
  return f;
}
int32_t beToInt(const uint8_t* p) {
  return int32_t((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]));
}
double beToDouble(const uint8_t* p) {
  uint64_t u = 0;
  for (int i = 0; i < 8; i++) u = (u << 8) | uint64_t(p[i]);
  double d;
  std::memcpy(&d, &u, sizeof(d));
  return d;
}

void appendBeFloat(std::vector<uint8_t>& b, float f) {
  uint32_t u;
  std::memcpy(&u, &f, sizeof(u));
  b.push_back(uint8_t(u >> 24)); b.push_back(uint8_t(u >> 16));
  b.push_back(uint8_t(u >> 8));  b.push_back(uint8_t(u));
}
void appendBeInt(std::vector<uint8_t>& b, int32_t i) {
  uint32_t u = uint32_t(i);
  b.push_back(uint8_t(u >> 24)); b.push_back(uint8_t(u >> 16));
  b.push_back(uint8_t(u >> 8));  b.push_back(uint8_t(u));
}
void appendOscString(std::vector<uint8_t>& b, const std::string& s) {
  b.insert(b.end(), s.begin(), s.end());
  b.push_back(0);
  while (b.size() % 4 != 0) b.push_back(0);
}

}  // namespace

// Coercion table — the exact ground truth of OscConnectionManager.cs:219-231. The decoder calls this
// once it has located one arg's type tag + raw big-endian bytes.
bool oscCoerceToFloat(char typeTag, const void* argBytes, int argLen, float& outValue) {
  const uint8_t* p = static_cast<const uint8_t*>(argBytes);
  switch (typeTag) {
    case 'f':  // float
      if (argLen < 4) { outValue = std::nanf(""); return false; }
      outValue = beToFloat(p);
      break;
    case 'i':  // int → (float)int
      if (argLen < 4) { outValue = std::nanf(""); return false; }
      outValue = float(beToInt(p));
      break;
    case 'T':  // bool true → 1
      outValue = 1.0f;
      break;
    case 'F':  // bool false → 0
      outValue = 0.0f;
      break;
    case 'd':  // double → (float)double
      if (argLen < 8) { outValue = std::nanf(""); return false; }
      outValue = float(beToDouble(p));
      break;
    case 's': {  // string → float.TryParse(s) else NaN
      // argBytes points at the (already-extracted) C string. strtof parses leading number; require it
      // to consume at least one char AND for the whole token to be numeric (TiXL float.TryParse rejects
      // trailing garbage like "3.5x"). We treat the arg as a NUL-terminated string of argLen bytes.
      std::string s(reinterpret_cast<const char*>(p), argLen);
      char* end = nullptr;
      float v = std::strtof(s.c_str(), &end);
      if (end == s.c_str() || *end != '\0') { outValue = std::nanf(""); return false; }
      outValue = v;
      break;
    }
    default:  // unsupported type → NaN (TiXL's `_ => float.NaN`)
      outValue = std::nanf("");
      return false;
  }
  return !std::isnan(outValue);
}

struct OscLoopbackDevice::Impl {
  int sock = -1;
  int port = 0;
  std::atomic<bool> running{false};
  std::thread rxThread;
  ValueCallback cb = nullptr;
  void* user = nullptr;

  // Decode one OSC message (after the leading '/' address) and fire cb per arg. `buf`/`len` cover the
  // whole datagram (or the inner message inside a bundle).
  void dispatchMessage(const uint8_t* buf, int len) {
    int pos = 0;
    std::string address;
    if (!readOscString(buf, len, pos, address)) return;
    if (address.empty() || address[0] != '/') return;  // not a message
    std::string typeTags;
    if (!readOscString(buf, len, pos, typeTags)) return;
    if (typeTags.empty() || typeTags[0] != ',') return;  // malformed type-tag string

    int argIndex = 0;
    for (size_t t = 1; t < typeTags.size(); ++t) {
      char tag = typeTags[t];
      float value = 0.f;
      if (tag == 'f' || tag == 'i') {
        if (pos + 4 > len) return;
        oscCoerceToFloat(tag, buf + pos, 4, value);
        pos += 4;
      } else if (tag == 'd') {
        if (pos + 8 > len) return;
        oscCoerceToFloat(tag, buf + pos, 8, value);
        pos += 8;
      } else if (tag == 'T' || tag == 'F') {
        oscCoerceToFloat(tag, nullptr, 0, value);  // no payload bytes
      } else if (tag == 's') {
        std::string s;
        int before = pos;
        if (!readOscString(buf, len, pos, s)) return;
        (void)before;
        oscCoerceToFloat('s', s.data(), int(s.size()), value);
      } else {
        return;  // unknown tag → stop (can't know its width)
      }
      if (cb) cb(user, address, value, argIndex);
      argIndex++;
    }
  }

  void dispatchPacket(const uint8_t* buf, int len) {
    // OSC bundle? begins with "#bundle\0" (8 bytes).
    if (len >= 8 && std::memcmp(buf, "#bundle", 7) == 0 && buf[7] == 0) {
      int pos = 16;  // skip "#bundle\0" (8) + timetag (8)
      while (pos + 4 <= len) {
        int32_t elemLen = beToInt(buf + pos);
        pos += 4;
        if (elemLen < 0 || pos + elemLen > len) break;
        dispatchPacket(buf + pos, elemLen);  // recursive: bundles may nest
        pos += elemLen;
      }
      return;
    }
    dispatchMessage(buf, len);
  }

  void rxLoop() {
    uint8_t buf[2048];
    while (running.load()) {
      ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
      if (n <= 0) {
        if (!running.load()) break;
        continue;
      }
      dispatchPacket(buf, int(n));
    }
  }

  bool sendDatagram(const std::vector<uint8_t>& bytes) {
    if (sock < 0) return false;
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(uint16_t(port));
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1
    ssize_t s = sendto(sock, bytes.data(), bytes.size(), 0,
                       reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    return s == ssize_t(bytes.size());
  }
};

OscLoopbackDevice::OscLoopbackDevice() : impl_(new Impl()) {}
OscLoopbackDevice::~OscLoopbackDevice() {
  stopListening();
  delete impl_;
}

bool OscLoopbackDevice::startListening(int port, ValueCallback cb, void* user) {
  stopListening();
  impl_->cb = cb;
  impl_->user = user;

  int s = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(uint16_t(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // bind 127.0.0.1 only (no prompt, no external)
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(s);
    return false;  // port busy / denied — caller may skip gracefully
  }
  // Recover the actual port (port 0 = OS picks a free one).
  socklen_t alen = sizeof(addr);
  if (::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &alen) == 0) {
    impl_->port = ntohs(addr.sin_port);
  } else {
    impl_->port = port;
  }
  impl_->sock = s;
  impl_->running.store(true);
  impl_->rxThread = std::thread([this] { impl_->rxLoop(); });
  return true;
}

void OscLoopbackDevice::stopListening() {
  if (!impl_->running.load() && impl_->sock < 0) return;
  impl_->running.store(false);
  if (impl_->sock >= 0) {
    ::shutdown(impl_->sock, SHUT_RDWR);
    ::close(impl_->sock);  // unblocks recvfrom
    impl_->sock = -1;
  }
  if (impl_->rxThread.joinable()) impl_->rxThread.join();
  impl_->port = 0;
}

bool OscLoopbackDevice::sendTestFloat(const std::string& address, float value) {
  if (!isListening()) return false;
  std::vector<uint8_t> b;
  appendOscString(b, address);
  appendOscString(b, ",f");
  appendBeFloat(b, value);
  return impl_->sendDatagram(b);
}

bool OscLoopbackDevice::sendTestInt(const std::string& address, int value) {
  if (!isListening()) return false;
  std::vector<uint8_t> b;
  appendOscString(b, address);
  appendOscString(b, ",i");
  appendBeInt(b, value);
  return impl_->sendDatagram(b);
}

bool OscLoopbackDevice::sendTestString(const std::string& address, const std::string& value) {
  if (!isListening()) return false;
  std::vector<uint8_t> b;
  appendOscString(b, address);
  appendOscString(b, ",s");
  appendOscString(b, value);
  return impl_->sendDatagram(b);
}

bool OscLoopbackDevice::sendTestFloatInt(const std::string& address, float a, int b) {
  if (!isListening()) return false;
  std::vector<uint8_t> bytes;
  appendOscString(bytes, address);
  appendOscString(bytes, ",fi");
  appendBeFloat(bytes, a);
  appendBeInt(bytes, b);
  return impl_->sendDatagram(bytes);
}

bool OscLoopbackDevice::sendTestBundle(const std::string& addrA, float a,
                                       const std::string& addrB, float b) {
  if (!isListening()) return false;
  auto encodeMsg = [](const std::string& addr, float v) {
    std::vector<uint8_t> m;
    appendOscString(m, addr);
    appendOscString(m, ",f");
    appendBeFloat(m, v);
    return m;
  };
  std::vector<uint8_t> mA = encodeMsg(addrA, a);
  std::vector<uint8_t> mB = encodeMsg(addrB, b);

  std::vector<uint8_t> bundle;
  appendOscString(bundle, "#bundle");  // 8 bytes incl NUL+pad
  for (int i = 0; i < 8; i++) bundle.push_back(0);  // OSC time tag (immediate = 0,0... here just 0s)
  appendBeInt(bundle, int32_t(mA.size()));
  bundle.insert(bundle.end(), mA.begin(), mA.end());
  appendBeInt(bundle, int32_t(mB.size()));
  bundle.insert(bundle.end(), mB.begin(), mB.end());
  return impl_->sendDatagram(bundle);
}

bool OscLoopbackDevice::isListening() const { return impl_->running.load() && impl_->sock >= 0; }
int  OscLoopbackDevice::listeningPort() const { return impl_->port; }

}  // namespace sw
