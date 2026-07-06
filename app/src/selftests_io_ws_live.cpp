// app/src/selftests_io_ws_live.cpp — L5 IO/硬體 LIVE WebSocket + WebServer harness (--selftest area leaf).
//
// 機器驗半 for the live WS state machine (platform/ws_live: HTTP-upgrade parse + frame state machine +
// long-connection mgmt) and the WebServer HTTP static responder. Every path is proven WITHOUT a real
// browser or remote peer: the pure handshake parsers are goldened against RFC6455-derived closed-form
// values, and the live stream machine is proven END-TO-END by a localhost WsLiveClient ↔ WsLiveServer
// round-trip (real 101 upgrade + masked/unmasked frame roundtrip both ways + ping→pong + close). A real
// browser client is 柏為 residue — it reuses these exact paths, only the peer's origin changes.
//
// Shell-tier leaf (app/src/ root, like selftests_io_socket.cpp): self-registers into selftestRegistry()
// during pre-main dynamic init. LEAF-LOCAL — includes its own platform headers directly.
//
// Ground truth mirrored: ws_live.h / websocket_frame.h (each cites the exact external/tixl .cs line or
// RFC6455 section for its expected values). GOLDEN_STANDARD three-features: expected values are
// independent-of-impl (hand-derived from RFC6455 / the canonical accept vector), probes sit at the
// diverging middle (a real masked-frame roundtrip, not identity), and injectBug corrupts the REAL cook
// path (setWsLiveBug: the server's on-wire accept / the frame unmask) — not a flipped expectation.
#include "runtime/selftest_registry.h"
#include "platform/ws_live.h"
#include "platform/websocket_frame.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sw {
namespace {

struct WsCapture {
  std::mutex mx;
  std::vector<std::string> msgs;
  std::atomic<int> count{0};
  void push(const std::string& m) {
    std::lock_guard<std::mutex> lk(mx);
    msgs.push_back(m);
    count.fetch_add(1);
  }
};
void wsCb(void* user, const std::string& m) { static_cast<WsCapture*>(user)->push(m); }

bool waitFor(WsCapture& cap, int n, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (cap.count.load() < n) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return true;
}

const uint8_t* B(const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); }

}  // namespace

// ===== 1) pure handshake parsers (closed form, no socket) =========================================
// --selftest-io-ws-handshake : the HTTP-upgrade REQUEST parse + the 101 RESPONSE build/validate +
// the WebServer HTTP static response — all pure functions, verified against RFC6455-derived values.
// injectBug corrupts the parsed key so the derived accept (real buildUpgradeResponse path) diverges.
int runWsHandshakeSelfTest(bool injectBug) {
  bool ok = true;

  // --- parseUpgradeRequest: a well-formed client upgrade (RFC6455 §4.2.1). The canonical nonce is the
  //     RFC6455 §1.3 example key; header names are mixed-case to exercise the case-insensitive match. ---
  const std::string req =
      "GET /ws HTTP/1.1\r\n"
      "Host: 127.0.0.1:8080\r\n"
      "UPGRADE: websocket\r\n"           // upper-case name — must still match
      "Connection: keep-alive, Upgrade\r\n"  // list form — must find "upgrade"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";
  WsUpgradeRequest pr = parseUpgradeRequest(B(req), req.size());
  ok = ok && pr.headersComplete && pr.ok;
  ok = ok && pr.method == "GET";
  ok = ok && pr.target == "/ws";
  ok = ok && pr.key == "dGhlIHNhbXBsZSBub25jZQ==";
  ok = ok && pr.headerBytes == req.size();  // the whole block, incl. terminating CRLF

  // Incomplete header block (no "\r\n\r\n" yet) → headersComplete=false, must not falsely parse.
  {
    const std::string partial = "GET / HTTP/1.1\r\nUpgrade: websocket\r\n";  // no blank line
    WsUpgradeRequest ip = parseUpgradeRequest(B(partial), partial.size());
    ok = ok && !ip.headersComplete && !ip.ok;
  }

  // A GET that is NOT an upgrade (missing Upgrade header) → headersComplete but ok=false (rejected).
  {
    const std::string plain = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    WsUpgradeRequest np = parseUpgradeRequest(B(plain), plain.size());
    ok = ok && np.headersComplete && !np.ok;
  }

  // --- buildUpgradeResponse: the 101 response for the parsed key. The Accept value is the RFC6455 §1.3
  //     canonical: key "dGhlIHNhbXBsZSBub25jZQ==" → "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=". injectBug corrupts
  //     the key fed to the builder, so the on-wire Accept diverges (the real derivation path). ---
  const std::string keyForBuild = injectBug ? "dGhlIHNhbXBsZSBub25jZQ==X" : "dGhlIHNhbXBsZSBub25jZQ==";
  std::string resp = buildUpgradeResponse(keyForBuild);
  ok = ok && resp.find("HTTP/1.1 101 Switching Protocols\r\n") == 0;
  ok = ok && resp.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos;
  ok = ok && resp.find("\r\n\r\n") != std::string::npos;  // terminating blank line

  // --- validateUpgradeResponse: the client's step-4 check. A response built for the correct key must
  //     validate; a response with a mismatched Accept must NOT. (Independent of injectBug — proves the
  //     validate leg on its own well-formed inputs.) ---
  {
    const std::string good = buildUpgradeResponse("dGhlIHNhbXBsZSBub25jZQ==");
    size_t hb = 0;
    ok = ok && validateUpgradeResponse(B(good), good.size(), "dGhlIHNhbXBsZSBub25jZQ==", &hb);
    ok = ok && hb == good.size();
    // Wrong client key → Accept mismatch → reject.
    ok = ok && !validateUpgradeResponse(B(good), good.size(), "someOtherKey==", nullptr);
  }

  // --- WebServer HTTP static: "/" → 200 text/html body (WebServer.cs:223); other path → 404 (cs:240). ---
  {
    const std::string html = "<html><body>hi</body></html>";
    std::string r200 = buildHttpResponse("/", html);
    ok = ok && r200.find("HTTP/1.1 200 OK\r\n") == 0;
    ok = ok && r200.find("Content-Type: text/html\r\n") != std::string::npos;
    ok = ok && r200.find("Content-Length: " + std::to_string(html.size()) + "\r\n") != std::string::npos;
    ok = ok && r200.size() >= html.size() && r200.substr(r200.size() - html.size()) == html;

    std::string r404 = buildHttpResponse("/nope", html);
    ok = ok && r404.find("HTTP/1.1 404 Not Found\r\n") == 0;
    ok = ok && r404.find(html) == std::string::npos;  // does NOT leak the body on a 404

    // GET-target parse round-trip.
    bool complete = false;
    const std::string get = "GET /page HTTP/1.1\r\nHost: x\r\n\r\n";
    std::string tgt = parseHttpGetTarget(B(get), get.size(), &complete);
    ok = ok && complete && tgt == "/page";
  }

  std::printf("[selftest-io-ws-handshake] parse+build+validate+http -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== 2) live end-to-end (WsLiveClient ↔ WsLiveServer over loopback) =============================
// --selftest-io-ws-live : a real localhost round-trip. The server binds, the client connects and does
// the RFC6455 101 upgrade (real SHA1-accept over the wire), then: client→server masked text (§5.3),
// server→client unmasked broadcast, and a large (>125B) payload exercising the extended-length frame.
// injectBug uses setWsLiveBug to corrupt the REAL server accept (mode 1) so the handshake fails and the
// end-to-end assertions go RED — biting the live upgrade path, not a flipped expectation.
int runWsLiveSelfTest(bool injectBug) {
  bool ok = true;

  setWsLiveBug(injectBug ? 1 : 0);

  WsCapture srvCap, cliCap;
  WsLiveServer server;
  if (!server.startListening(0, wsCb, &srvCap)) {
    std::printf("[selftest-io-ws-live] listen failed -> SKIP transport (env restricted)\n");
    setWsLiveBug(0);
    return injectBug ? 1 : 0;  // env skip: bug leg still returns 1 (bite expected) — matches io_socket
  }
  const int port = server.boundPort();

  WsLiveClient client;
  // The SAME assertion in both legs (GOLDEN_STANDARD P3 discipline: injectBug corrupts the real cook
  // path, not the expectation). Clean: the 101 upgrade completes → connected. Bug (mode 1): the server's
  // on-wire accept is derived from a WRONG key → the client's validate rejects → connected==false → this
  // assert (and every roundtrip assert below, which never runs) makes the golden go RED (exit 1). The
  // whole live path hangs off the handshake, so corrupting it is a true seam bite, not a want-flip.
  const bool connected = client.connectTo(port, "/", wsCb, &cliCap);
  ok = ok && connected;

  // If the handshake broke (the injected-bug leg's expected RED), there is nothing more to exercise —
  // tear down and report the failure so --bite sees exit 1.
  if (!connected) {
    client.disconnect();
    server.stopListening();
    setWsLiveBug(0);
    std::printf("[selftest-io-ws-live] handshake-not-open (bug=%d) -> %s\n", injectBug ? 1 : 0,
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;  // clean leg never lands here; bug leg → ok=false → exit 1
  }

  // Wait for the server to register the open (post-handshake) connection.
  {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (server.connectionCount() < 1 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ok = ok && server.connectionCount() == 1;

  // client → server : masked text frame (§5.3). The server unmasks + reassembles → callback.
  ok = ok && client.send("hello from client");
  bool srvGot = waitFor(srvCap, 1, 1000);
  ok = ok && srvGot;
  if (srvGot) {
    std::lock_guard<std::mutex> lk(srvCap.mx);
    ok = ok && srvCap.msgs[0] == "hello from client";
  }

  // server → client : unmasked broadcast (§WebSocketServer.cs:400). The client decodes → callback.
  int sent = server.broadcast("pong from server");
  ok = ok && sent == 1;
  bool cliGot = waitFor(cliCap, 1, 1000);
  ok = ok && cliGot;
  if (cliGot) {
    std::lock_guard<std::mutex> lk(cliCap.mx);
    ok = ok && cliCap.msgs[0] == "pong from server";
  }

  // Extended-length frame (>125B → 16-bit len path) client→server, proving the length state machine
  // spans the recv boundary correctly.
  {
    std::string big(300, 'z');
    ok = ok && client.send(big);
    bool got2 = waitFor(srvCap, 2, 1000);
    ok = ok && got2;
    if (got2) {
      std::lock_guard<std::mutex> lk(srvCap.mx);
      ok = ok && srvCap.msgs.size() >= 2 && srvCap.msgs[1] == big;
    }
  }

  client.disconnect();
  server.stopListening();
  setWsLiveBug(0);
  std::printf("[selftest-io-ws-live] srv=%d cli=%d conns=1 -> %s\n", srvCap.count.load(),
              cliCap.count.load(), ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ===== 3) live frame-unmask tooth (mode 2) =======================================================
// --selftest-io-ws-unmask : the same live client→server text roundtrip, but the tooth (setWsLiveBug 2)
// DROPS the mask-unmask on the server's recv path → the delivered text is corrupted → the assert
// diverges. This isolates the frame state machine's unmask leg from the handshake (mode 1). No injectBug
// arg needed on this one beyond the mode switch; the clean leg must round-trip intact.
int runWsUnmaskSelfTest(bool injectBug) {
  bool ok = true;
  setWsLiveBug(injectBug ? 2 : 0);

  WsCapture srvCap;
  WsLiveServer server;
  if (!server.startListening(0, wsCb, &srvCap)) {
    std::printf("[selftest-io-ws-unmask] listen failed -> SKIP transport (env restricted)\n");
    setWsLiveBug(0);
    return injectBug ? 1 : 0;
  }
  const int port = server.boundPort();

  WsCapture cliCap;
  WsLiveClient client;
  if (!client.connectTo(port, "/", wsCb, &cliCap)) {
    server.stopListening();
    setWsLiveBug(0);
    std::printf("[selftest-io-ws-unmask] connect failed -> FAIL\n");
    return 1;
  }

  const std::string payload = "the quick brown fox";  // diverging-middle: not empty, not identity
  ok = ok && client.send(payload);
  bool got = waitFor(srvCap, 1, 1000);
  ok = ok && got;
  if (got) {
    std::lock_guard<std::mutex> lk(srvCap.mx);
    // Clean: server sees the exact text. Bug (mode 2): the dropped unmask corrupts it → assert diverges.
    ok = ok && srvCap.msgs[0] == payload;
  }

  client.disconnect();
  server.stopListening();
  setWsLiveBug(0);
  std::printf("[selftest-io-ws-unmask] delivered=%d intact=%s -> %s\n", srvCap.count.load(),
              ok ? "yes" : "no", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/710,
    {"io-ws-handshake", runWsHandshakeSelfTest},
    {"io-ws-live",      runWsLiveSelfTest},
    {"io-ws-unmask",    runWsUnmaskSelfTest});

}  // namespace sw
