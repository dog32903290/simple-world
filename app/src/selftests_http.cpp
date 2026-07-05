// selftests_http — io/json RequestUrl + image LoadImageFromUrl golden (--selftest-http). Two rails:
//   (A) TRANSPORT parity (machine, localhost loopback — no real network): a real platform httpGet()
//       against a TcpServerLoopback speaking a fixed HTTP/1.1 response. Proves the request-line the GET
//       emits + the status/body the parse recovers, closed-form against a hand-derived response. The
//       REAL remote server (an API / an image host) is 柏為 residue: it reuses this exact NSURLSession
//       path — only the peer's origin changes → deferred-hw-verify.
//   (B) COOK parity (machine, bus-injected): the trigger rising-edge gate in cookHttpNodes — a GET is
//       kicked ONCE on the rising edge (TiXL MathUtils.WasTriggered, RequestUrl.cs:20), NOT every frame
//       a held trigger persists. injectBug mode 1 drops the edge → a held trigger re-fires frame 2 →
//       the frame-2 request count diverges from 0 → RED (mirror of selftests_net_nodes' send-edge tooth).
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/json/RequestUrl.cs                    — HttpClient GET, Result = body string,
//                                                            status NOT checked (cs:28-45); trigger
//                                                            rising edge OR url change (cs:18-23).
//   Operators/Lib/image/generate/load/LoadImageFromUrl.cs  — GetAsync, IsSuccessStatusCode gate (cs:76),
//                                                            trigger OR url→non-empty change (cs:25-28).
//
// Shell-tier leaf (app/src/ root, like selftests_io_socket.cpp): self-registers, includes its own
// platform header (http_request.h) directly + drives the runtime http cook directly.
#include "platform/http_request.h"        // httpGet (transport rail)
#include "platform/net_loopback.h"        // TcpServerLoopback (localhost HTTP server)
#include "runtime/graph.h"                // findSpec
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/net_node_cook.h"        // cookHttpNodes / httpBus / endHttpFrame / setHttpNodeBug
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS

#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace sw {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [http] FAIL %s\n", what); }
  else printf("  [http] ok   %s\n", what);
}

// ── (A) TRANSPORT: httpGet against a localhost HTTP/1.1 loopback ──────────────────────────────────────
// The loopback server answers EVERY inbound request with the same fixed HTTP/1.1 response. The request
// bytes the GET sends are captured so we can assert the request LINE (method + version) too.
struct HttpServerState {
  TcpServerLoopback* server = nullptr;
  std::mutex mx;
  std::string lastRequest;           // the raw request bytes the client sent (captured for assertion)
  std::atomic<int> requests{0};
  std::string response;              // the fixed response to broadcast back
};

HttpServerState* g_srv = nullptr;
void httpServerCb(void* user, const std::string& request) {
  auto* st = static_cast<HttpServerState*>(user);
  {
    std::lock_guard<std::mutex> lk(st->mx);
    st->lastRequest = request;
  }
  st->requests.fetch_add(1);
  // Answer the request: broadcast the fixed response to the connected client (one recv = one message,
  // net_loopback raw UTF-8, no framing — the HTTP client reads it as the response body stream).
  if (st->server) st->server->broadcast(st->response);
}

bool goldenHttpTransport(bool injectBug) {
  HttpServerState st;
  TcpServerLoopback server;
  st.server = &server;
  // Fixed HTTP/1.1 200 response: status line + Content-Length + a JSON body. Hand-derived expected
  // values below (body == the 13 chars, status == 200).
  const std::string body = "{\"ok\":true}\r\n";  // 13 bytes
  st.response =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: 13\r\n"
      "Connection: close\r\n"
      "\r\n" +
      body;

  if (!server.startListening(0, httpServerCb, &st)) {
    printf("  [http] listen failed -> SKIP transport (env restricted)\n");
    // A restricted env can't bind loopback; do NOT count a false green nor a false red for the tooth.
    if (injectBug) return true;  // nothing to bite here; the cook tooth carries the bite duty.
    return g_fail == 0;
  }
  const int port = server.boundPort();
  char url[64];
  std::snprintf(url, sizeof(url), "http://127.0.0.1:%d/data", port);

  // injectBug corrupts the URL PATH so the request the server captures diverges from the expected path
  // → the request-line assertion bites the REAL bytes the GET emitted (not a flipped expected value).
  if (injectBug) std::snprintf(url, sizeof(url), "http://127.0.0.1:%d/WRONGPATH", port);

  HttpResponse resp = httpGet(url, /*timeoutSeconds=*/5.0);
  server.stopListening();

  expect("httpGet reached the server (ok)", resp.ok);
  expect("httpGet status == 200 (parsed from HTTP/1.1 200 OK)", resp.status == 200);
  const std::string got(resp.body.begin(), resp.body.end());
  expect("httpGet body == the fixed 13-byte JSON (Content-Length honored)", got == body);

  // Request-line parity: the GET must emit `GET /data HTTP/1.1`. injectBug sent /WRONGPATH → the
  // captured request line diverges → RED. (The path is the ONLY thing the bug moves; method+version
  // are fixed by httpGet — asserting all three pins the request the platform seam actually wrote.)
  std::string req;
  { std::lock_guard<std::mutex> lk(st.mx); req = st.lastRequest; }
  const bool methodOk  = req.rfind("GET ", 0) == 0;                 // starts with "GET "
  const bool pathOk    = req.find("GET /data ") == 0;               // exact path /data
  const bool versionOk = req.find("HTTP/1.1") != std::string::npos; // HTTP/1.1
  expect("request line method == GET", methodOk);
  expect("request line path == /data (bug sends /WRONGPATH → RED)", pathOk);
  expect("request line version == HTTP/1.1", versionOk);
  return g_fail == 0;
}

// ── (A2) TRANSPORT closed-form: a non-2xx status is surfaced (ok=true, status=404) ────────────────────
// RequestUrl reads the body regardless of status (RequestUrl.cs:44) — httpGet must NOT swallow a 404;
// it reports ok=true + status=404 + the error body. (No bug seam: this is a closed-form status-parse
// assertion — the value is TiXL-anchored, the parse either recovers 404 or it doesn't.)
bool goldenHttpNon2xx(bool /*injectBug*/) {
  HttpServerState st;
  TcpServerLoopback server;
  st.server = &server;
  const std::string body = "not found";  // 9 bytes
  st.response =
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Length: 9\r\n"
      "Connection: close\r\n"
      "\r\n" +
      body;
  if (!server.startListening(0, httpServerCb, &st)) {
    printf("  [http] listen failed -> SKIP non-2xx (env restricted)\n");
    return g_fail == 0;
  }
  const int port = server.boundPort();
  char url[64];
  std::snprintf(url, sizeof(url), "http://127.0.0.1:%d/missing", port);
  HttpResponse resp = httpGet(url, 5.0);
  server.stopListening();

  expect("httpGet 404 still ok (transport reached, status reported not swallowed)", resp.ok);
  expect("httpGet status == 404 (RequestUrl reads body regardless of status)", resp.status == 404);
  const std::string got(resp.body.begin(), resp.body.end());
  expect("httpGet 404 body recovered", got == body);
  return g_fail == 0;
}

// ── (A3) TRANSPORT error: a malformed / unreachable URL returns ok=false, status=0, no crash ──────────
bool goldenHttpBadUrl(bool /*injectBug*/) {
  HttpResponse bad = httpGet("not a url", 2.0);
  expect("httpGet malformed url → ok=false", !bad.ok);
  expect("httpGet malformed url → status=0", bad.status == 0);
  expect("httpGet malformed url → error set (no crash)", !bad.error.empty());
  return g_fail == 0;
}

// ── (B) COOK: the trigger rising-edge gate in cookHttpNodes ───────────────────────────────────────────
// Root "R" with one child of type `op`, given float + string overrides.
SymbolLibrary makeLib(const char* op, const std::map<std::string, float>& floatOv,
                      const std::map<std::string, std::string>& strOv) {
  SymbolLibrary lib;
  const NodeSpec* spec = findSpec(op);
  if (spec) lib.symbols[op] = atomicSymbolFromSpec(*spec);
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = op;
  for (const auto& kv : floatOv) c.overrides[kv.first] = kv.second;
  for (const auto& kv : strOv)   c.strOverrides[kv.first] = kv.second;
  root.children = {c};
  root.nextChildId = 2;
  lib.symbols["R"] = root;
  lib.rootId = "R";
  return lib;
}

// A HELD trigger fires a GET on the RISING edge only (frame 1), not on frame 2. injectBug mode 1 drops
// the edge → frame 2 re-fires → the frame-2 request count diverges from 0 → RED.
bool goldenHttpTriggerEdge(bool injectBug, const char* op, const char* trigPort) {
  SymbolLibrary lib = makeLib(op, {{trigPort, 1.0f}},  // trigger held true across both frames
                              {{"Url", "http://example.com/x"}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  const ResidentNode* n = g.node("1");
  std::map<std::string, HttpNodeState> state;

  setHttpNodeBug(injectBug ? 1 : 0);
  endHttpFrame();
  cookHttpNodes(g, state);                         // frame 1: rising edge → 1 request
  const size_t frame1 = httpBus().requests.size();
  const float fired1 = n ? n->extOut[0] : -1.0f;
  endHttpFrame();
  cookHttpNodes(g, state);                         // frame 2: still held, no new edge → 0 requests
  const size_t frame2 = httpBus().requests.size();
  const float fired2 = n ? n->extOut[0] : -1.0f;
  endHttpFrame();
  setHttpNodeBug(0);

  char msg[128];
  std::snprintf(msg, sizeof(msg), "%s kicks a GET on the rising edge (frame1 request count == 1)", op);
  expect(msg, frame1 == 1);
  std::snprintf(msg, sizeof(msg), "%s RequestFired echo == 1 on frame 1", op);
  expect(msg, fired1 == 1.0f);
  std::snprintf(msg, sizeof(msg),
                "%s does NOT re-fire while held (frame2 count == 0; bug level-fires → RED)", op);
  expect(msg, frame2 == 0);
  std::snprintf(msg, sizeof(msg), "%s RequestFired echo == 0 on frame 2 (held, no new edge)", op);
  expect(msg, fired2 == 0.0f);
  return g_fail == 0;
}

// ── (B2) COOK: a blank Url never fires (nothing to GET) ───────────────────────────────────────────────
bool goldenHttpBlankUrl(bool /*injectBug*/) {
  SymbolLibrary lib = makeLib("RequestUrl", {{"TriggerRequest", 1.0f}}, {{"Url", ""}});  // blank Url
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  const ResidentNode* n = g.node("1");
  std::map<std::string, HttpNodeState> state;

  endHttpFrame();
  cookHttpNodes(g, state);   // trigger held true, BUT Url blank → no GET (non-empty gate, cs:27)
  const size_t count = httpBus().requests.size();
  const float fired = n ? n->extOut[0] : -1.0f;
  endHttpFrame();
  expect("blank Url + trigger → NO GET fired (non-empty gate, LoadImageFromUrl.cs:27)", count == 0);
  expect("blank Url → RequestFired echo 0", fired == 0.0f);
  return g_fail == 0;
}

}  // namespace

int runHttpSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] http (RequestUrl + LoadImageFromUrl: transport parity + trigger-edge cook)\n");
  goldenHttpTransport(injectBug);
  goldenHttpNon2xx(injectBug);
  goldenHttpBadUrl(injectBug);
  goldenHttpTriggerEdge(injectBug, "RequestUrl", "TriggerRequest");
  goldenHttpTriggerEdge(injectBug, "LoadImageFromUrl", "TriggerUpdate");
  goldenHttpBlankUrl(injectBug);
  printf("[selftest] http %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

REGISTER_SELFTESTS(/*orderBase=*/730, {"http", runHttpSelfTest});

}  // namespace sw
