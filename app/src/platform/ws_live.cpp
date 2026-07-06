// platform/ws_live.cpp — the PURE half of the live WS clone: HTTP-upgrade REQUEST/RESPONSE parsers +
// the WebServer HTTP static responder + the TEETH switch. Closed-form, no sockets. The socket/thread
// plumbing (WsLiveServer / WsLiveClient / the frame stream) lives in ws_live_socket.cpp — split to keep
// each file ≤400 lines (ARCHITECTURE rule 4). Ground-truth lines cited in ws_live.h.
//
// platform leaf: pure string parsing + the codec's accept derivation (websocket_frame.h). No sockets,
// no threads, no runtime / app / UI includes. Every function here is goldenable without a peer.
#include "platform/ws_live.h"

#include "platform/websocket_frame.h"

#include <cctype>

namespace sw {

namespace {
int g_wsLiveBug = 0;
}  // namespace
void setWsLiveBug(int mode) { g_wsLiveBug = mode; }
int  wsLiveBug() { return g_wsLiveBug; }

namespace {

// ---- text helpers (ASCII, RFC7230 §3.2 case-insensitive field names) ----------------------------
std::string asciiLower(std::string s) {
  for (char& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

// Split a completed HTTP header block into the request/status line + a name→value map (lower-cased
// names). Returns false if the "\r\n\r\n" terminator is not present in [data,data+len).
bool splitHeaders(const uint8_t* data, size_t len, std::string& firstLine,
                  std::vector<std::pair<std::string, std::string>>& headers, size_t& headerBytes) {
  // Find the end-of-headers marker.
  const std::string blk(reinterpret_cast<const char*>(data), len);
  size_t term = blk.find("\r\n\r\n");
  if (term == std::string::npos) return false;
  headerBytes = term + 4;

  size_t pos = 0;
  size_t eol = blk.find("\r\n", pos);
  firstLine = blk.substr(0, eol);
  pos = eol + 2;
  while (pos < term) {
    eol = blk.find("\r\n", pos);
    if (eol == std::string::npos || eol > term) eol = term;
    const std::string line = blk.substr(pos, eol - pos);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string name = asciiLower(trim(line.substr(0, colon)));
      std::string val = trim(line.substr(colon + 1));
      headers.emplace_back(name, val);
    }
    pos = eol + 2;
  }
  return true;
}

const std::string* findHeader(const std::vector<std::pair<std::string, std::string>>& h,
                              const std::string& name) {
  for (const auto& kv : h)
    if (kv.first == name) return &kv.second;
  return nullptr;
}

}  // namespace

// ===== pure handshake parsers ====================================================================

WsUpgradeRequest parseUpgradeRequest(const uint8_t* data, size_t len) {
  WsUpgradeRequest r;
  std::string firstLine;
  std::vector<std::pair<std::string, std::string>> headers;
  if (!splitHeaders(data, len, firstLine, headers, r.headerBytes)) return r;  // need more bytes
  r.headersComplete = true;

  // Request line: METHOD SP request-target SP HTTP/1.1
  {
    size_t sp1 = firstLine.find(' ');
    if (sp1 == std::string::npos) return r;
    size_t sp2 = firstLine.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return r;
    r.method = firstLine.substr(0, sp1);
    r.target = firstLine.substr(sp1 + 1, sp2 - sp1 - 1);
  }

  // RFC6455 §4.2.1: GET + Upgrade: websocket + Connection contains "upgrade" + Sec-WebSocket-Key.
  const std::string* up = findHeader(headers, "upgrade");
  const std::string* conn = findHeader(headers, "connection");
  const std::string* key = findHeader(headers, "sec-websocket-key");
  const bool methodOk = r.method == "GET";
  const bool upgradeOk = up && asciiLower(*up).find("websocket") != std::string::npos;
  const bool connOk = conn && asciiLower(*conn).find("upgrade") != std::string::npos;
  const bool keyOk = key && !key->empty();
  if (key) r.key = *key;
  r.ok = methodOk && upgradeOk && connOk && keyOk;
  return r;
}

std::string buildUpgradeResponse(const std::string& clientKey) {
  // RFC6455 §4.2.2 — 101 Switching Protocols with the derived accept key.
  std::string accept = wsComputeAcceptKey(clientKey);
  std::string r;
  r += "HTTP/1.1 101 Switching Protocols\r\n";
  r += "Upgrade: websocket\r\n";
  r += "Connection: Upgrade\r\n";
  r += "Sec-WebSocket-Accept: " + accept + "\r\n";
  r += "\r\n";
  return r;
}

std::string buildUpgradeRequest(const std::string& target, const std::string& clientKey, int port) {
  // RFC6455 §4.1 — client opening handshake.
  std::string r;
  r += "GET " + (target.empty() ? std::string("/") : target) + " HTTP/1.1\r\n";
  r += "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
  r += "Upgrade: websocket\r\n";
  r += "Connection: Upgrade\r\n";
  r += "Sec-WebSocket-Key: " + clientKey + "\r\n";
  r += "Sec-WebSocket-Version: 13\r\n";
  r += "\r\n";
  return r;
}

bool validateUpgradeResponse(const uint8_t* data, size_t len, const std::string& clientKey,
                             size_t* headerBytes) {
  std::string firstLine;
  std::vector<std::pair<std::string, std::string>> headers;
  size_t hb = 0;
  if (!splitHeaders(data, len, firstLine, headers, hb)) return false;  // incomplete
  if (headerBytes) *headerBytes = hb;

  // Status line must carry 101 (RFC6455 §4.1 step 2). firstLine == "HTTP/1.1 101 Switching Protocols".
  if (firstLine.find(" 101 ") == std::string::npos &&
      !(firstLine.size() >= 12 && firstLine.substr(9, 3) == "101"))
    return false;
  const std::string* accept = findHeader(headers, "sec-websocket-accept");
  if (!accept) return false;
  return *accept == wsComputeAcceptKey(clientKey);  // §4.1 step 4
}

// ===== HTTP static (WebServer) ===================================================================

std::string parseHttpGetTarget(const uint8_t* data, size_t len, bool* headersComplete) {
  std::string firstLine;
  std::vector<std::pair<std::string, std::string>> headers;
  size_t hb = 0;
  if (!splitHeaders(data, len, firstLine, headers, hb)) {
    if (headersComplete) *headersComplete = false;
    return "";
  }
  if (headersComplete) *headersComplete = true;
  size_t sp1 = firstLine.find(' ');
  if (sp1 == std::string::npos) return "";
  size_t sp2 = firstLine.find(' ', sp1 + 1);
  if (sp2 == std::string::npos) return "";
  return firstLine.substr(sp1 + 1, sp2 - sp1 - 1);
}

std::string buildHttpResponse(const std::string& target, const std::string& html) {
  // WebServer.cs:223 — "/" (or empty) serves the HTML body 200; :240 — any other path → 404.
  const bool root = target.empty() || target == "/";
  const std::string body =
      root ? html
           : std::string("<html><body><h1>404 Not Found</h1></body></html>");
  const int status = root ? 200 : 404;
  const char* reason = root ? "OK" : "Not Found";
  std::string r;
  r += "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
  r += "Content-Type: text/html\r\n";
  r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  r += "Connection: close\r\n";
  r += "\r\n";
  r += body;
  return r;
}

}  // namespace sw
