// platform/osc_loopback — localhost UDP OSC receive loop + self-rolled OSC 1.0 parse.
//
// L5 (IO/硬體 lane) machine-verifiable half: the OSC receive path proven by a localhost loopback
// (bind 127.0.0.1:port, sendto self, recvfrom, parse) — zero real network, zero physical device.
// The REAL OSC controller binding (a phone/console sending to the Mac) is 柏為 device residue: it
// reuses this exact receive+parse path, only the sender changes.
//
// platform leaf: owns a POSIX UDP socket + a background receive thread behind a C++ pimpl, so callers
// include no socket headers and no runtime headers. Each parsed message is handed to a registered
// callback (the value already coerced to float the way TiXL's OscConnectionManager does it). The
// value-→-graph-parameter mapping lives ABOVE platform (app layer hook, future L6 refinement) — this
// stays a pure platform leaf with ZERO runtime dependency, mirroring audio_capture's inversion.
//
// OSC parse is self-rolled (TiXL uses the Rug.Osc C# NuGet — not portable). We implement the minimal
// OSC 1.0 message/bundle binary decode + the float coercion of OscConnectionManager.TryGetFloat-
// FromMessagePart (float/int/bool/double/string→parse, else NaN). See osc_loopback.cpp for the
// ground-truth file:line.
#pragma once
#include <string>

namespace sw {

class OscLoopbackDevice {
 public:
  OscLoopbackDevice();
  ~OscLoopbackDevice();
  OscLoopbackDevice(const OscLoopbackDevice&) = delete;
  OscLoopbackDevice& operator=(const OscLoopbackDevice&) = delete;

  // Per-message callback, invoked on the RECEIVE THREAD for each decoded OSC argument. `address` is
  // the OSC address pattern (e.g. "/test/val"); `value` is the FIRST/each argument coerced to float
  // exactly like TiXL TryGetFloatFromMessagePart (int/bool/double/string→float, NaN on parse-fail).
  // `argIndex` is the 0-based argument slot within the message (so a multi-arg message fires the cb
  // once per arg). Set it BEFORE startListening(): the receive thread reads it once on start.
  using ValueCallback = void (*)(void* user, const std::string& address, float value, int argIndex);

  // Bind 127.0.0.1:port (UDP) and start the receive thread. Returns false on an immediate, known
  // failure (e.g. bind denied / port in use) — non-fatal, the caller can skip. Idempotent-ish:
  // calling again stops + rebinds.
  bool startListening(int port, ValueCallback cb, void* user);
  void stopListening();

  // For loopback testing: encode + sendto 127.0.0.1:listeningPort() an OSC message with a single
  // float arg. Returns false if not listening or the send failed. The decoded message round-trips
  // back through recvfrom → parse → callback.
  bool sendTestFloat(const std::string& address, float value);
  // Same, but encodes a single int32 arg (proves the int→float coercion branch).
  bool sendTestInt(const std::string& address, int value);
  // Same, but a single OSC string arg (proves the string→float.TryParse branch; "abc" → NaN).
  bool sendTestString(const std::string& address, const std::string& value);
  // Two-arg message [float, int] in one packet (proves per-arg dispatch / argIndex).
  bool sendTestFloatInt(const std::string& address, float a, int b);
  // An OSC bundle wrapping two messages (proves the bundle-iterate branch).
  bool sendTestBundle(const std::string& addrA, float a, const std::string& addrB, float b);

  bool isListening() const;
  int  listeningPort() const;

 private:
  struct Impl;
  Impl* impl_;
};

// Mirror of TiXL OscConnectionManager.TryGetFloatFromMessagePart (Core/IO/OscConnectionManager.cs:219).
// Coerces a decoded OSC arg to float: 'f'→float, 'i'→int, 'T'/'F'→1/0, 'd'→(float)double,
// 's'→strtof (NaN if no number parsed). Returns false (and value=NaN) when the value is unusable —
// the !IsNaN gate TiXL uses. Exposed for the selftest to assert the coercion table directly.
bool oscCoerceToFloat(char typeTag, const void* argBytes, int argLen, float& outValue);

}  // namespace sw
