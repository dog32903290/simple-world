// platform/websocket_frame — RFC6455 handshake key + frame codec (pure, closed-form).
//
// TiXL's WebSocketClient uses .NET ClientWebSocket and WebSocketServer uses HttpListener.AcceptWebSocket
// — both wrap a hidden BCL RFC6455 implementation (no portable source to transcribe). The PROTOCOL is
// a public closed-form spec (RFC6455), so the load-bearing pieces are self-rolled here as pure
// functions and verified against the RFC's own canonical test vectors — independent of any transport.
// The live send/receive reuse TcpClientLoopback / TcpServerLoopback (platform/net_loopback.h) for the
// byte stream; this file is only the protocol codec.
//
// Ground truth: RFC6455.
//   §1.3   Sec-WebSocket-Accept = base64( SHA1( key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" ) ).
//          Canonical example: key "dGhlIHNhbXBsZSBub25jZQ==" → accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=".
//   §5.2   Frame: FIN|RSV|opcode, MASK|payloadLen(7 / 7+16 / 7+64), masking-key(4B if MASK), payload.
//   §5.3   Client→server frames MUST be masked (payload XOR the 4-byte key, cycled).
//
// SCOPE NOTE: this file is the pure CODEC + handshake-accept derivation that every WS path depends on.
// The live state machine that rides it (HTTP-upgrade REQUEST parse + continuation / ping-pong / close-
// handshake state machine + long-connection mgmt over a real TCP stream) is built on top in
// platform/ws_live.{h,cpp} + ws_live_socket.cpp and proven end-to-end by selftests_io_ws_live.cpp.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sw {

// RFC6455 §1.3 accept-key derivation: base64(SHA1(clientKey + magic GUID)). This is the whole server
// handshake proof — a server that computes this correctly will complete the upgrade with any client.
std::string wsComputeAcceptKey(const std::string& clientKey);

// Opcodes we surface (RFC6455 §5.2). Text is the only data type TiXL uses (WebSocketMessageType.Text).
enum class WsOpcode : uint8_t {
  Continuation = 0x0,
  Text = 0x1,
  Binary = 0x2,
  Close = 0x8,
  Ping = 0x9,
  Pong = 0xA,
};

// Encode one complete (FIN=1) WebSocket frame. If `mask` is true, a masking key is generated from
// `maskKey` (4 bytes, client→server MUST mask per §5.3) and applied. Server→client frames are
// unmasked (mask=false). Returns the on-wire bytes.
std::vector<uint8_t> wsEncodeFrame(WsOpcode opcode, const std::string& payload, bool mask,
                                   const uint8_t maskKey[4]);

// Decode result for one frame off a buffer.
struct WsFrame {
  bool ok = false;         // false = incomplete / malformed (need more bytes)
  bool fin = false;
  WsOpcode opcode = WsOpcode::Text;
  bool masked = false;
  std::string payload;     // unmasked payload bytes
  size_t consumed = 0;     // bytes consumed from the input (to advance the stream)
};

// Decode the first frame at the front of `data` (len bytes). If the buffer doesn't yet hold a full
// frame, returns ok=false, consumed=0 (caller reads more). Unmasks the payload if MASK was set.
WsFrame wsDecodeFrame(const uint8_t* data, size_t len);

// Minimal SHA-1 (RFC3174) and base64 — exposed for the selftest to assert against known vectors.
// wsBase64 is standard base64 with '+' '/' and '=' padding (RFC4648).
std::string wsSha1Hex(const std::string& in);          // 40-char lowercase hex digest
std::vector<uint8_t> wsSha1Raw(const std::string& in); // the raw 20-byte digest (base64-of-digest use)
std::string wsBase64(const std::vector<uint8_t>& in);  // standard base64

}  // namespace sw
