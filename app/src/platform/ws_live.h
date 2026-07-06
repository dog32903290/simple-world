// platform/ws_live — the LIVE WebSocket state machine over a localhost TCP stream (RFC6455).
//
// The missing third of the WS clone. platform/websocket_frame.h already ships the closed-form CODEC
// (handshake-accept SHA1+base64, frame encode/decode) — verified against RFC6455 canonical vectors.
// What that file's SCOPE NOTE flags as BLOCKED is here: the HTTP-upgrade REQUEST parse, the client-side
// 101 RESPONSE validate, and the per-connection FRAME STATE MACHINE (fragment reassembly, ping→pong,
// close handshake) run over a real (loopback) TCP byte stream that does NOT respect message boundaries.
//
// Why not ride net_loopback's TcpServerLoopback/TcpClientLoopback directly? Those decode "one recv ==
// one message" (raw TCP, no framing) — correct for TiXL's TcpClient/TcpServer nodes, WRONG for WS: a WS
// frame can span multiple recvs and multiple frames can arrive in one recv. WS needs a per-connection
// byte BUFFER + the handshake-then-frame-loop over the raw fd. So this file owns its own POSIX sockets
// (mirroring net_loopback.cpp's shape: bind 127.0.0.1, accept/connect, a std::thread rx loop behind a
// pimpl) and layers the RFC6455 stream machine on top. This is essential complexity (a stream protocol
// over a byte stream), not accidental — it is packaged behind a message-callback seam so the app/nodes
// operate the same knobs as the raw-TCP family (connect/listen/send + a per-message callback).
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/websocket/WebSocketClient.cs — ClientWebSocket connect+send+recv; MessageType.Text
//     recv → UTF-8 (cs:428); MessageType.Close → StopAsync (cs:415); send is masked text (cs:472).
//   Operators/Lib/io/websocket/WebSocketServer.cs — HttpListener.AcceptWebSocket accept loop (cs:294);
//     per-client recv (cs:339); broadcast unmasked text (cs:400); close on Close frame (cs:342).
//   Operators/Lib/io/http/WebServer.cs — HTTP GET → serve a static HTML string; "/" → 200 text/html
//     (cs:223), any other path → 404 (cs:240).
// TiXL's transport is .NET BCL (ClientWebSocket / HttpListener) — not portable — so the byte plumbing is
// self-rolled on POSIX sockets + the pure RFC6455 codec. The PROTOCOL (upgrade handshake, masking rule,
// control-frame semantics) is a public closed-form spec; the request/response parse below is the load-
// bearing content and is goldened as pure functions (parseUpgradeRequest / buildUpgradeResponse), the
// stream machine is proven end-to-end by a loopback client↔server round-trip in selftests_io_ws_live.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sw {

// ── Pure, closed-form handshake pieces (goldenable without any socket) ──────────────────────────────

// Parsed client → server HTTP upgrade request (RFC6455 §4.2.1). `ok` is false when the byte prefix does
// not yet hold a complete header block (no "\r\n\r\n") or the required upgrade fields are missing.
struct WsUpgradeRequest {
  bool ok = false;             // a complete, valid WS upgrade request was parsed
  bool headersComplete = false;  // the "\r\n\r\n" terminator was seen (independent of validity)
  std::string method;          // "GET"
  std::string target;          // request-target, e.g. "/" or "/ws"
  std::string key;             // Sec-WebSocket-Key header value (base64 nonce)
  size_t headerBytes = 0;      // bytes consumed by the header block incl. the "\r\n\r\n"
};

// Parse the HTTP request at the front of `data` (len bytes). Returns headersComplete=false / ok=false if
// the "\r\n\r\n" boundary is not yet buffered (caller reads more). A valid WS upgrade needs: method GET,
// Upgrade: websocket, Connection contains "upgrade", and a non-empty Sec-WebSocket-Key. Header-name
// matching is ASCII-case-insensitive (RFC7230 §3.2). RFC6455 §4.2.1.
WsUpgradeRequest parseUpgradeRequest(const uint8_t* data, size_t len);

// Build the server → client 101 Switching Protocols response for a parsed key (RFC6455 §4.2.2). The
// Sec-WebSocket-Accept value is wsComputeAcceptKey(key) (websocket_frame.h). Returns the exact on-wire
// bytes (CRLF line endings, terminating blank line).
std::string buildUpgradeResponse(const std::string& clientKey);

// Build the client → server upgrade REQUEST for a target path + key (RFC6455 §4.1). The client picks a
// random 16-byte nonce base64'd into `key` (caller supplies it so the golden is deterministic). Returns
// the exact on-wire request bytes.
std::string buildUpgradeRequest(const std::string& target, const std::string& clientKey, int port);

// Validate a server → client 101 response against the key the client sent (RFC6455 §4.1 step 4): the
// status line must be "101" and Sec-WebSocket-Accept must equal wsComputeAcceptKey(clientKey). Returns
// false if the header block is incomplete or the accept mismatches. `headerBytes` (out) is the response
// header length once complete (to advance past it into the frame stream).
bool validateUpgradeResponse(const uint8_t* data, size_t len, const std::string& clientKey,
                             size_t* headerBytes);

// ── HTTP static response (WebServer node) ───────────────────────────────────────────────────────────

// Parse a plain HTTP GET request line (WebServer.cs — no upgrade). Returns the requested path, or ""
// if the header block is incomplete. Only the request target matters for the static server.
std::string parseHttpGetTarget(const uint8_t* data, size_t len, bool* headersComplete);

// Build the WebServer HTTP response: "/" (or empty) → 200 text/html serving `html` (WebServer.cs:223);
// any other target → 404 (WebServer.cs:240). Returns the exact on-wire response bytes.
std::string buildHttpResponse(const std::string& target, const std::string& html);

// ── Golden TEETH hook (--selftest-io-ws-live) ───────────────────────────────────────────────────────
// 0 = production. 1 = server computes the WRONG accept key in its 101 response (corrupts the real
// buildUpgradeResponse path the server sends on the wire → the client's validateUpgradeResponse rejects
// → the live handshake fails → the end-to-end golden goes RED). 2 = the frame state machine DROPS the
// mask-unmask (treats every client→server frame as unmasked → the reassembled text is garbage → the
// roundtrip assert diverges on the real recv path). Sticky module switch; the golden restores 0.
void setWsLiveBug(int mode);
int  wsLiveBug();

// ── Live WebSocket SERVER (WebSocketServer node) ────────────────────────────────────────────────────
// Listens on 127.0.0.1:port, completes the RFC6455 upgrade per connection, runs the per-connection
// frame state machine (text → callback, ping → auto-pong, close → close handshake + drop), and can
// broadcast an (unmasked) text frame to every open connection. Owns its POSIX sockets + rx threads.
class WsLiveServer {
 public:
  WsLiveServer();
  ~WsLiveServer();
  WsLiveServer(const WsLiveServer&) = delete;
  WsLiveServer& operator=(const WsLiveServer&) = delete;

  // Per complete TEXT message, on a client thread. Payload is the reassembled UTF-8 message (fragments
  // joined; WebSocketServer.cs:348 Encoding.UTF8.GetString of one logical message).
  using MessageCallback = void (*)(void* user, const std::string& message);

  // Listen on 127.0.0.1:port (port 0 → OS picks; read via boundPort()). false on bind/listen failure.
  bool startListening(int port, MessageCallback cb, void* user);
  void stopListening();

  // Broadcast a text frame to all open connections (WebSocketServer.cs:400, unmasked server→client).
  // Returns the number of connections written to.
  int broadcast(const std::string& message);

  bool isListening() const;
  int  boundPort() const;
  int  connectionCount() const;  // open post-handshake connections (ConnectionCount output)

 private:
  struct Impl;
  Impl* impl_;
};

// ── Live WebSocket CLIENT (WebSocketClient node) ────────────────────────────────────────────────────
// Connects to 127.0.0.1:port, sends the upgrade request, validates the 101 response, then runs the
// frame state machine (text → callback, ping → auto-pong, close → drop). Sends masked text frames
// (RFC6455 §5.3 client→server MUST mask; WebSocketClient.cs:472).
class WsLiveClient {
 public:
  WsLiveClient();
  ~WsLiveClient();
  WsLiveClient(const WsLiveClient&) = delete;
  WsLiveClient& operator=(const WsLiveClient&) = delete;

  using MessageCallback = void (*)(void* user, const std::string& message);

  // Connect to 127.0.0.1:port at `target` path, perform the upgrade. false on connect/handshake failure.
  bool connectTo(int port, const std::string& target, MessageCallback cb, void* user);
  void disconnect();

  // Send a masked text frame (WebSocketClient.cs:472). false if not open / send failed.
  bool send(const std::string& message);

  bool isConnected() const;  // true once the 101 handshake completed and the socket is open

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace sw
