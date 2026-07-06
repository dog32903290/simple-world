// app/src/selftests_io_socket.cpp — L5 IO/硬體 socket-family loopback harness (--selftest area leaf).
//
// 機器驗半 for the network/serial io family (TcpClient/TcpServer/UDPInput/UDPOutput/SerialInput/
// SerialOutput/WLedSerialOutput/WebSocketClient/WebSocketServer). Every path is proven WITHOUT a real
// peer or device: localhost TCP/UDP loopback, a PTY-pair serial loopback, and pure closed-form codecs
// (WLED Adalight frame, RFC6455 handshake+frame) verified against ground-truth values. Real remote
// peers / physical /dev/tty devices are 柏為 residue — they reuse these exact paths, only the origin
// of the bytes changes.
//
// Shell-tier leaf (app/src/ root, like selftests_io.cpp): self-registers into selftestRegistry()
// during pre-main dynamic init. LEAF-LOCAL — includes its own platform headers directly.
//
// Ground truth mirrored: net_loopback.h / serial_loopback.h / wled_frame.h / websocket_frame.h
// (each cites the exact external/tixl .cs line or RFC section for its expected values).
#include "runtime/selftest_registry.h"
#include "platform/net_loopback.h"
#include "platform/serial_loopback.h"
#include "platform/wled_frame.h"
#include "platform/websocket_frame.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sw {
namespace {

// ---- shared capture / wait helpers --------------------------------------------------------------
struct MsgCapture {
  std::mutex mx;
  std::vector<std::string> msgs;
  std::vector<int> ports;
  std::atomic<int> count{0};
  void push(const std::string& m, int port) {
    std::lock_guard<std::mutex> lk(mx);
    msgs.push_back(m);
    ports.push_back(port);
    count.fetch_add(1);
  }
};

bool waitFor(MsgCapture& cap, int n, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (cap.count.load() < n) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));  // settle a trailing message
  return true;
}

void udpCb(void* user, const std::string& m, int port) {
  static_cast<MsgCapture*>(user)->push(m, port);
}
void tcpServerCb(void* user, const std::string& m) { static_cast<MsgCapture*>(user)->push(m, 0); }
void tcpClientCb(void* user, const std::string& m) { static_cast<MsgCapture*>(user)->push(m, 0); }
void serialCb(void* user, const std::string& line) { static_cast<MsgCapture*>(user)->push(line, 0); }

}  // namespace

// ===== UDP (UDPInput / UDPOutput) ================================================================
// --selftest-io-udp-loopback : two localhost UDP endpoints; A sends to B's bound port, B receives +
// decodes UTF-8 (UDPInput.cs:232) and captures A's sender port (LastSenderPort, UDPInput.cs:89).
// injectBug corrupts the expected payload so the decode tooth bites the REAL recvfrom path.
int runUdpLoopbackSelfTest(bool injectBug) {
  bool ok = true;

  MsgCapture capB;
  UdpLoopbackDevice a, b;
  if (!b.startListening(0, udpCb, &capB) || !a.startListening(0, udpCb, nullptr)) {
    std::printf("[selftest-io-udp-loopback] bind failed -> SKIP transport (env restricted; tooth cannot bite)\n");
    if (injectBug) return 0;  // did-not-trip -> 0, NO-BITE list surfaces the env-dead tooth
    return ok ? 0 : 1;
  }

  const int bPort = b.boundPort();
  const int aPort = a.boundPort();
  // injectBug corrupts the SENT payload (real transport input) — the want below stays FIXED, so the
  // assert diverges on genuine wire carriage, not on a flipped expectation (P3 fix, 2026-07-06 audit).
  ok = ok && a.sendTo(bPort, injectBug ? "CORRUPT" : "hello world");
  ok = ok && a.sendTo(bPort, "12.5 ok");

  bool arrived = waitFor(capB, 2, 1000);
  ok = ok && arrived;
  if (arrived) {
    std::lock_guard<std::mutex> lk(capB.mx);
    ok = ok && capB.msgs[0] == "hello world";  // FIXED want (send side carries the corruption)
    ok = ok && capB.msgs[1] == "12.5 ok";
    ok = ok && capB.ports[0] == aPort;  // LastSenderPort round-trips to A's bound port
  }

  a.stopListening();
  b.stopListening();
  std::printf("[selftest-io-udp-loopback] received=%d expected=2 -> %s\n", capB.count.load(),
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== TCP (TcpClient / TcpServer) ===============================================================
// --selftest-io-tcp-loopback : server listens on localhost, client connects (ConnectionCount→1),
// client→server send (TcpServer recv, cs:246), server→client broadcast (TcpServer.cs:278 → TcpClient
// recv cs:290). injectBug corrupts the expected client→server payload so the recv tooth bites.
int runTcpLoopbackSelfTest(bool injectBug) {
  bool ok = true;

  MsgCapture srvCap, cliCap;
  TcpServerLoopback server;
  if (!server.startListening(0, tcpServerCb, &srvCap)) {
    std::printf("[selftest-io-tcp-loopback] listen failed -> SKIP transport (env restricted; tooth cannot bite)\n");
    if (injectBug) return 0;  // did-not-trip -> 0, NO-BITE list surfaces the env-dead tooth
    return ok ? 0 : 1;
  }
  const int port = server.boundPort();

  TcpClientLoopback client;
  ok = ok && client.connectTo(port, tcpClientCb, &cliCap);

  // Wait for the server to register the accepted connection (ConnectionCount output).
  {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (server.connectionCount() < 1 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ok = ok && server.connectionCount() == 1;

  // client → server
  ok = ok && client.send(injectBug ? "CORRUPT" : "ping from client");  // bug corrupts the SENT payload
  bool srvGot = waitFor(srvCap, 1, 1000);
  ok = ok && srvGot;
  if (srvGot) {
    std::lock_guard<std::mutex> lk(srvCap.mx);
    ok = ok && srvCap.msgs[0] == "ping from client";  // FIXED want (P3 fix, 2026-07-06 audit)
  }

  // server → client (broadcast)
  int sent = server.broadcast("pong from server");
  ok = ok && sent == 1;
  bool cliGot = waitFor(cliCap, 1, 1000);
  ok = ok && cliGot;
  if (cliGot) {
    std::lock_guard<std::mutex> lk(cliCap.mx);
    ok = ok && cliCap.msgs[0] == "pong from server";
  }

  client.disconnect();
  server.stopListening();
  std::printf("[selftest-io-tcp-loopback] srv=%d cli=%d conns-seen=1 -> %s\n", srvCap.count.load(),
              cliCap.count.load(), ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== Serial (SerialInput / SerialOutput) =======================================================
// --selftest-io-serial-loopback : PTY-pair serial. App-side writes go to the master (SerialOutput
// Write / WriteLine, appends '\n'); device-side injection splits '\n'-lines to the callback
// (SerialInput ISerialReceiver.ReceiveLine). injectBug corrupts the expected received line so the
// line-split tooth bites the real read path.
int runSerialLoopbackSelfTest(bool injectBug) {
  bool ok = true;

  MsgCapture cap;
  SerialLoopbackDevice serial;
  if (!serial.openLoopback(9600, serialCb, &cap)) {
    std::printf("[selftest-io-serial-loopback] openpty failed -> SKIP transport (env restricted; tooth cannot bite)\n");
    if (injectBug) return 0;  // did-not-trip -> 0, NO-BITE list surfaces the env-dead tooth
    return ok ? 0 : 1;
  }

  // 1) device → app: inject two newline-terminated lines; assert the split lines (CRLF stripped).
  ok = ok && serial.injectFromDevice(injectBug ? "CORRUPT\r\npartial"
                                               : "sensor 42\r\npartial");  // bug corrupts the device bytes
  ok = ok && serial.injectFromDevice(" done\n");               // completes the second line
  bool arrived = waitFor(cap, 2, 1000);
  ok = ok && arrived;
  if (arrived) {
    std::lock_guard<std::mutex> lk(cap.mx);
    ok = ok && cap.msgs[0] == "sensor 42";  // FIXED want, '\r' stripped (P3 fix, 2026-07-06 audit)
    ok = ok && cap.msgs[1] == "partial done";
  }

  // 2) app → device: SerialOutput WriteLine appends '\n'; observe it on the master.
  ok = ok && serial.writeLine("cmd:on");
  std::string seen = serial.readFromMaster(64, 500);
  ok = ok && seen == "cmd:on\n";  // AddLineEnding=true

  serial.close();
  std::printf("[selftest-io-serial-loopback] lines=%d expected=2 -> %s\n", cap.count.load(),
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== WLED Adalight frame (WLedSerialOutput body) ===============================================
// --selftest-io-wled-frame : the closed-form frame builder. Expected bytes hand-derived from
// WLedSerialOutput.cs (header + color-order + map-mode + brightness + ToByte). injectBug corrupts a
// frame parameter so the byte assert diverges on the real builder.
int runWledFrameSelfTest(bool injectBug) {
  bool ok = true;

  // 1) ToByte quantization (WLED :264-271): clamp + round v*255+0.5.
  ok = ok && wledToByte(0.f) == 0;
  ok = ok && wledToByte(1.f) == 255;
  ok = ok && wledToByte(0.5f) == 128;       // 0.5*255+0.5 = 128.0 → 128
  ok = ok && wledToByte(2.f) == 255;        // clamp high
  ok = ok && wledToByte(-1.f) == 0;         // clamp low

  // 2) Full frame: 2 source colors, 4 LEDs, Stretch, GRB order, brightness 1.0.
  //    Source: red(1,0,0), blue(0,0,1). Stretch nearest: srcIdx = i*2/4 → [0,0,1,1].
  //    GRB order → wire triplet = [G,R,B]. red→(0,255,0), blue→(0,0,255).
  //    Header: 'A''d''a', (4-1)>>8=0, (4-1)&0xFF=3, checksum=0^3^0x55=0x56.
  std::vector<WledColor> colors = {{1, 0, 0}, {0, 0, 1}};
  auto frame = buildWledFrame(colors, 4, WledMapMode::Stretch, WledColorOrder::GRB,
                              injectBug ? 0.0f : 1.0f);  // bug: brightness 0 → all pixels 0

  // Expected header (independent of brightness).
  ok = ok && frame.size() == 6 + 4 * 3;
  ok = ok && frame[0] == 0x41 && frame[1] == 0x64 && frame[2] == 0x61;
  ok = ok && frame[3] == 0x00 && frame[4] == 0x03 && frame[5] == 0x56;

  // Expected pixels at brightness 1.0 (GRB wire order). LEDs 0,1 = red; 2,3 = blue.
  // red GRB = [0,255,0]; blue GRB = [0,0,255]. At the injected brightness 0.0 → all 0 → bites.
  const uint8_t expRedG = 0, expRedR = 255, expRedB = 0;
  const uint8_t expBluG = 0, expBluR = 0, expBluB = 255;
  auto px = [&](int i, int off) { return frame[6 + i * 3 + off]; };
  ok = ok && px(0, 0) == expRedG && px(0, 1) == expRedR && px(0, 2) == expRedB;
  ok = ok && px(1, 0) == expRedG && px(1, 1) == expRedR && px(1, 2) == expRedB;
  ok = ok && px(2, 0) == expBluG && px(2, 1) == expBluR && px(2, 2) == expBluB;
  ok = ok && px(3, 0) == expBluG && px(3, 1) == expBluR && px(3, 2) == expBluB;

  // 3) Lerp map-mode at a DIVERGING MIDDLE (probes the interpolation body, not identity ends).
  //    2 colors red(1,0,0)→blue(0,0,1), 3 LEDs, RGB order, brightness 1. lerpScale=(2-1)/(3-1)=0.5.
  //    LED0 t=0 → red = R255 G0 B0.  LED1 t=0.5 → lerp 0.5 → (0.5,0,0.5) → R128 G0 B128.
  //    LED2 t=1.0 → lo=1≥src-1 → blue = R0 G0 B255. (WLED :174-193.) The middle LED is the only
  //    probe that would go RED if the interp math were wrong (frac/lo/lerpScale) — the P2 anchor.
  std::vector<WledColor> two = {{1, 0, 0}, {0, 0, 1}};
  auto lf = buildWledFrame(two, 3, WledMapMode::Lerp, WledColorOrder::RGB, 1.0f);
  auto lp = [&](int i, int off) { return lf[6 + i * 3 + off]; };
  ok = ok && lp(0, 0) == 255 && lp(0, 1) == 0 && lp(0, 2) == 0;    // red
  ok = ok && lp(1, 0) == 128 && lp(1, 1) == 0 && lp(1, 2) == 128;  // MID: 0.5*255+0.5=128 both ends
  ok = ok && lp(2, 0) == 0 && lp(2, 1) == 0 && lp(2, 2) == 255;    // blue

  std::printf("[selftest-io-wled-frame] frame=%zuB -> %s\n", frame.size(), ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== WebSocket codec (RFC6455) =================================================================
// --selftest-io-websocket-frame : the handshake-accept derivation + frame encode/decode, verified
// against RFC6455's own canonical vectors. injectBug corrupts the input key so the accept-key assert
// diverges on the real SHA1+base64 path. This proves the pure protocol CODEC; the LIVE WS state machine
// (HTTP-upgrade parse + frame state machine + long-connection mgmt over a real TCP stream, and the
// WebSocketClient/Server/WebServer nodes) is now built in platform/ws_live and proven end-to-end by
// selftests_io_ws_live.cpp (--selftest-io-ws-handshake / -io-ws-live / -io-ws-unmask).
int runWebSocketFrameSelfTest(bool injectBug) {
  bool ok = true;

  // 1) SHA-1 known-answer (RFC3174 / FIPS 180-1): SHA1("abc").
  ok = ok && wsSha1Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d";
  ok = ok && wsSha1Hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709";  // empty string

  // 2) base64 known-answer (RFC4648): "Man" → "TWFu", "Ma" → "TWE=", "M" → "TQ==".
  ok = ok && wsBase64({'M', 'a', 'n'}) == "TWFu";
  ok = ok && wsBase64({'M', 'a'}) == "TWE=";
  ok = ok && wsBase64({'M'}) == "TQ==";

  // 3) RFC6455 §1.3 canonical accept-key: key "dGhlIHNhbXBsZSBub25jZQ==" → accept
  //    "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=". injectBug feeds a wrong key so the derived accept diverges.
  const std::string key = injectBug ? "wrongkey==" : "dGhlIHNhbXBsZSBub25jZQ==";
  ok = ok && wsComputeAcceptKey(key) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

  // 4) Frame round-trip: server→client (unmasked) text frame encode → decode.
  {
    auto enc = wsEncodeFrame(WsOpcode::Text, "hello", /*mask=*/false, nullptr);
    // Unmasked small text frame: 0x81, 0x05, 'h','e','l','l','o'.
    ok = ok && enc.size() == 7 && enc[0] == 0x81 && enc[1] == 0x05;
    auto dec = wsDecodeFrame(enc.data(), enc.size());
    ok = ok && dec.ok && dec.fin && dec.opcode == WsOpcode::Text && !dec.masked &&
         dec.payload == "hello" && dec.consumed == enc.size();
  }

  // 5) Client→server MUST mask (§5.3): encode masked, assert MASK bit + XOR unmask round-trips.
  {
    const uint8_t mkey[4] = {0x37, 0xFA, 0x21, 0x3D};
    auto enc = wsEncodeFrame(WsOpcode::Text, "Hello", /*mask=*/true, mkey);
    ok = ok && (enc[1] & 0x80) != 0;  // MASK bit set
    // RFC 6455 §5.7 EXAMPLE, exact wire bytes: masked "Hello" with this very key = 81 85 37 fa 21 3d
    // 7f 9f 4d 51 58. Spec-anchored — kills the encode→decode self-consistency window where a
    // double-XOR no-op stays green (P5 fix, 2026-07-06 audit).
    const uint8_t kRfc57[11] = {0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58};
    ok = ok && enc.size() == 11 && std::memcmp(enc.data(), kRfc57, sizeof kRfc57) == 0;
    auto dec = wsDecodeFrame(enc.data(), enc.size());
    ok = ok && dec.ok && dec.masked && dec.payload == "Hello";
  }

  // 6) Extended payload length (126 path, 16-bit len): 200-byte payload.
  {
    std::string big(200, 'x');
    auto enc = wsEncodeFrame(WsOpcode::Text, big, false, nullptr);
    ok = ok && enc[1] == 126;  // 7-bit len == 126 → next 2 bytes are the 16-bit length
    auto dec = wsDecodeFrame(enc.data(), enc.size());
    ok = ok && dec.ok && dec.payload.size() == 200;
  }

  std::printf("[selftest-io-websocket-frame] codec+handshake -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/700,
    {"io-udp-loopback",       runUdpLoopbackSelfTest},
    {"io-tcp-loopback",       runTcpLoopbackSelfTest},
    {"io-serial-loopback",    runSerialLoopbackSelfTest},
    {"io-wled-frame",         runWledFrameSelfTest},
    {"io-websocket-frame",    runWebSocketFrameSelfTest});

}  // namespace sw
