// platform/ws_live_socket.cpp — the socket/thread plumbing for the live WS state machine (WsLiveServer /
// WsLiveClient + the per-connection FrameStream). Split from ws_live.cpp (pure parsers + HTTP static +
// TEETH) to keep each file ≤400 lines (ARCHITECTURE rule 4). Same platform leaf, same header (ws_live.h);
// this half owns the POSIX sockets + rx threads, the parser half owns the closed-form derivations.
//
// Ground-truth lines cited in ws_live.h. The TEETH switch (setWsLiveBug/wsLiveBug) lives with the parser
// half; this file reads it via wsLiveBug() (mode 1 = server accept corrupted at the send site below;
// mode 2 = frame unmask dropped in FrameStream).
#include "platform/ws_live.h"

#include "platform/websocket_frame.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace sw {

// ===== frame state machine (shared by client + server) ==========================================
namespace {

// Send all bytes of `buf` on fd (loops over partial writes). false on error.
bool sendAll(int fd, const uint8_t* buf, size_t n) {
  size_t off = 0;
  while (off < n) {
    ssize_t w = ::send(fd, buf + off, n - off, 0);
    if (w <= 0) return false;
    off += size_t(w);
  }
  return true;
}
bool sendAll(int fd, const std::vector<uint8_t>& v) { return sendAll(fd, v.data(), v.size()); }

// Per-connection RFC6455 stream reader. Accumulates raw recv bytes into `buf`, drains complete frames,
// reassembles fragmented data messages, auto-replies ping→pong, and reports a clean CLOSE. Returns
// false from feed() once the connection should be torn down (close received / protocol shutdown).
struct FrameStream {
  int fd = -1;
  bool serverSide = false;   // server sends unmasked, client sends masked (§5.3)
  std::vector<uint8_t> buf;  // raw byte accumulator
  std::string fragBuf;       // reassembly of a fragmented data message
  WsOpcode fragOpcode = WsOpcode::Text;
  bool inFragment = false;

  // Feed `n` freshly-recv'd bytes; drain all complete frames. Calls `onText(msg)` per complete text
  // message. Returns false if a Close was seen (caller tears down after replying).
  template <class OnText>
  bool feed(const uint8_t* data, size_t n, OnText&& onText) {
    buf.insert(buf.end(), data, data + n);
    size_t off = 0;
    while (true) {
      WsFrame f = wsDecodeFrame(buf.data() + off, buf.size() - off);
      if (!f.ok) break;  // need more bytes for the next frame
      off += f.consumed;

      switch (f.opcode) {
        case WsOpcode::Ping: {
          // §5.5.2 — reply Pong with the same application data, our own send-side mask discipline.
          uint8_t mk[4] = {0x12, 0x34, 0x56, 0x78};
          auto pong = wsEncodeFrame(WsOpcode::Pong, f.payload, /*mask=*/!serverSide, mk);
          sendAll(fd, pong);
          break;
        }
        case WsOpcode::Pong:
          break;  // unsolicited pong — ignore (§5.5.3)
        case WsOpcode::Close: {
          // §5.5.1 — echo a Close then signal teardown.
          uint8_t mk[4] = {0x12, 0x34, 0x56, 0x78};
          auto cl = wsEncodeFrame(WsOpcode::Close, f.payload, /*mask=*/!serverSide, mk);
          sendAll(fd, cl);
          buf.erase(buf.begin(), buf.begin() + off);
          return false;
        }
        case WsOpcode::Continuation:
          if (inFragment) {
            fragBuf += f.payload;
            if (f.fin) {
              if (fragOpcode == WsOpcode::Text) onText(fragBuf);
              fragBuf.clear();
              inFragment = false;
            }
          }
          break;
        case WsOpcode::Text:
        case WsOpcode::Binary:
          // TEETH mode 2: on a MASKED (client→server) frame, corrupt the unmasked payload — simulates a
          // dropped unmask on the real recv path, so the delivered text diverges from what was sent.
          if (wsLiveBug() == 2 && f.masked && !f.payload.empty()) f.payload[0] ^= 0x40;
          if (f.fin) {
            if (f.opcode == WsOpcode::Text) onText(f.payload);  // TiXL surfaces Text only (cs:426)
          } else {
            // Start of a fragmented data message (§5.4).
            inFragment = true;
            fragOpcode = f.opcode;
            fragBuf = f.payload;
          }
          break;
      }
    }
    if (off > 0) buf.erase(buf.begin(), buf.begin() + off);
    return true;
  }
};

}  // namespace

// ===== live SERVER ===============================================================================

struct WsLiveServer::Impl {
  int listenSock = -1;
  int port = 0;
  std::atomic<bool> running{false};
  std::thread acceptThread;
  MessageCallback cb = nullptr;
  void* user = nullptr;

  std::mutex clientsMx;
  std::vector<int> openFds;              // post-handshake, open connections (for broadcast + count)
  std::vector<std::thread> clientThreads;

  void addOpen(int fd) {
    std::lock_guard<std::mutex> lk(clientsMx);
    openFds.push_back(fd);
  }
  void removeOpen(int fd) {
    std::lock_guard<std::mutex> lk(clientsMx);
    for (size_t i = 0; i < openFds.size(); ++i)
      if (openFds[i] == fd) { openFds.erase(openFds.begin() + i); break; }
  }

  // One accepted connection: do the upgrade handshake, then run the frame stream until close/error.
  void clientLoop(int fd) {
    // 1) Read until the HTTP upgrade header block is complete, then respond 101.
    std::vector<uint8_t> hs;
    WsUpgradeRequest req;
    {
      uint8_t rbuf[4096];
      while (running.load()) {
        ssize_t rn = ::recv(fd, rbuf, sizeof(rbuf), 0);
        if (rn <= 0) { ::close(fd); return; }
        hs.insert(hs.end(), rbuf, rbuf + rn);
        req = parseUpgradeRequest(hs.data(), hs.size());
        if (req.headersComplete) break;
        if (hs.size() > 65536) { ::close(fd); return; }  // runaway header
      }
    }
    if (!req.ok) {
      // Not a valid WS upgrade → reject (mirrors WebSocketServer.cs:310, StatusCode 400).
      const char* bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
      sendAll(fd, reinterpret_cast<const uint8_t*>(bad), std::strlen(bad));
      ::close(fd);
      return;
    }
    // TEETH mode 1: derive the accept from a WRONG key → the client's validate rejects the 101 (the real
    // on-wire handshake breaks). This corrupts the exact response the server sends, not the expectation.
    std::string resp = buildUpgradeResponse(wsLiveBug() == 1 ? (req.key + "X") : req.key);
    if (!sendAll(fd, reinterpret_cast<const uint8_t*>(resp.data()), resp.size())) {
      ::close(fd);
      return;
    }

    // 2) Handshake done → connection is OPEN. Any bytes past the header block are the first frames.
    addOpen(fd);
    FrameStream fs;
    fs.fd = fd;
    fs.serverSide = true;
    bool live = true;
    if (hs.size() > req.headerBytes) {
      live = fs.feed(hs.data() + req.headerBytes, hs.size() - req.headerBytes,
                     [&](const std::string& m) { if (cb) cb(user, m); });
    }

    // 3) Frame loop.
    uint8_t rbuf[8192];
    while (live && running.load()) {
      ssize_t rn = ::recv(fd, rbuf, sizeof(rbuf), 0);
      if (rn <= 0) break;
      live = fs.feed(rbuf, size_t(rn), [&](const std::string& m) { if (cb) cb(user, m); });
    }
    removeOpen(fd);
    ::close(fd);
  }

  void acceptLoop() {
    while (running.load()) {
      int fd = ::accept(listenSock, nullptr, nullptr);
      if (fd < 0) {
        if (!running.load()) break;
        continue;
      }
      int yes = 1;
      ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
      std::lock_guard<std::mutex> lk(clientsMx);
      clientThreads.emplace_back([this, fd] { clientLoop(fd); });
    }
  }
};

WsLiveServer::WsLiveServer() : impl_(new Impl()) {}
WsLiveServer::~WsLiveServer() {
  stopListening();
  delete impl_;
}

bool WsLiveServer::startListening(int port, MessageCallback cb, void* user) {
  stopListening();
  impl_->cb = cb;
  impl_->user = user;

  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return false;
  int yes = 1;
  ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(uint16_t(port));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { ::close(s); return false; }
  if (::listen(s, 8) != 0) { ::close(s); return false; }
  sockaddr_in got{};
  socklen_t glen = sizeof(got);
  impl_->port = (::getsockname(s, reinterpret_cast<sockaddr*>(&got), &glen) == 0)
                    ? ntohs(got.sin_port)
                    : port;
  impl_->listenSock = s;
  impl_->running.store(true);
  impl_->acceptThread = std::thread([this] { impl_->acceptLoop(); });
  return true;
}

void WsLiveServer::stopListening() {
  if (!impl_->running.load() && impl_->listenSock < 0) return;
  impl_->running.store(false);
  if (impl_->listenSock >= 0) {
    ::shutdown(impl_->listenSock, SHUT_RDWR);
    ::close(impl_->listenSock);
    impl_->listenSock = -1;
  }
  {
    std::lock_guard<std::mutex> lk(impl_->clientsMx);
    for (int fd : impl_->openFds) ::shutdown(fd, SHUT_RDWR);
  }
  if (impl_->acceptThread.joinable()) impl_->acceptThread.join();
  for (auto& t : impl_->clientThreads)
    if (t.joinable()) t.join();
  impl_->clientThreads.clear();
  {
    std::lock_guard<std::mutex> lk(impl_->clientsMx);
    impl_->openFds.clear();
  }
  impl_->port = 0;
}

int WsLiveServer::broadcast(const std::string& message) {
  auto frame = wsEncodeFrame(WsOpcode::Text, message, /*mask=*/false, nullptr);  // server→client unmasked
  std::lock_guard<std::mutex> lk(impl_->clientsMx);
  int sent = 0;
  for (int fd : impl_->openFds)
    if (sendAll(fd, frame)) sent++;
  return sent;
}

bool WsLiveServer::isListening() const {
  return impl_->running.load() && impl_->listenSock >= 0;
}
int WsLiveServer::boundPort() const { return impl_->port; }
int WsLiveServer::connectionCount() const {
  std::lock_guard<std::mutex> lk(impl_->clientsMx);
  return int(impl_->openFds.size());
}

// ===== live CLIENT ===============================================================================

struct WsLiveClient::Impl {
  int sock = -1;
  std::atomic<bool> open{false};
  std::atomic<bool> running{false};
  std::thread rxThread;
  MessageCallback cb = nullptr;
  void* user = nullptr;

  void rxLoop(std::vector<uint8_t> leftover) {
    FrameStream fs;
    fs.fd = sock;
    fs.serverSide = false;
    bool live = true;
    if (!leftover.empty())
      live = fs.feed(leftover.data(), leftover.size(),
                     [&](const std::string& m) { if (cb) cb(user, m); });
    uint8_t rbuf[8192];
    while (live && running.load()) {
      ssize_t rn = ::recv(sock, rbuf, sizeof(rbuf), 0);
      if (rn <= 0) break;
      live = fs.feed(rbuf, size_t(rn), [&](const std::string& m) { if (cb) cb(user, m); });
    }
    open.store(false);
  }
};

WsLiveClient::WsLiveClient() : impl_(new Impl()) {}
WsLiveClient::~WsLiveClient() {
  disconnect();
  delete impl_;
}

bool WsLiveClient::connectTo(int port, const std::string& target, MessageCallback cb, void* user) {
  disconnect();
  impl_->cb = cb;
  impl_->user = user;

  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return false;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(uint16_t(port));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { ::close(s); return false; }
  int yes = 1;
  ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

  // Opening handshake: send the upgrade request, read + validate the 101 response.
  // A fixed base64 nonce keeps it deterministic (a real nonce would be random; loopback parity only
  // needs the accept to derive from THIS key — the server echoes SHA1(key+GUID)).
  const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";  // RFC6455 §1.3 canonical nonce
  std::string req = buildUpgradeRequest(target, key, port);
  if (!sendAll(s, reinterpret_cast<const uint8_t*>(req.data()), req.size())) { ::close(s); return false; }

  std::vector<uint8_t> hs;
  size_t headerBytes = 0;
  bool validated = false;
  {
    uint8_t rbuf[4096];
    while (true) {
      ssize_t rn = ::recv(s, rbuf, sizeof(rbuf), 0);
      if (rn <= 0) { ::close(s); return false; }
      hs.insert(hs.end(), rbuf, rbuf + rn);
      bool complete = false;
      // validateUpgradeResponse succeeds only on a complete + matching 101; else check completeness.
      if (validateUpgradeResponse(hs.data(), hs.size(), key, &headerBytes)) { validated = true; break; }
      // If the header block is complete but validate returned false → mismatch → fail.
      parseHttpGetTarget(hs.data(), hs.size(), &complete);
      if (complete) { ::close(s); return false; }
      if (hs.size() > 65536) { ::close(s); return false; }
    }
  }
  if (!validated) { ::close(s); return false; }

  impl_->sock = s;
  impl_->open.store(true);
  impl_->running.store(true);
  // Any bytes past the response header block are already-arrived frames — hand them to the rx loop.
  std::vector<uint8_t> leftover(hs.begin() + headerBytes, hs.end());
  impl_->rxThread = std::thread([this, leftover] { impl_->rxLoop(leftover); });
  return true;
}

void WsLiveClient::disconnect() {
  if (!impl_->running.load() && impl_->sock < 0) return;
  impl_->running.store(false);
  impl_->open.store(false);
  if (impl_->sock >= 0) {
    ::shutdown(impl_->sock, SHUT_RDWR);
    ::close(impl_->sock);
    impl_->sock = -1;
  }
  if (impl_->rxThread.joinable()) impl_->rxThread.join();
}

bool WsLiveClient::send(const std::string& message) {
  if (impl_->sock < 0 || !impl_->open.load()) return false;
  uint8_t mk[4] = {0x37, 0xFA, 0x21, 0x3D};  // client→server MUST mask (§5.3)
  auto frame = wsEncodeFrame(WsOpcode::Text, message, /*mask=*/true, mk);
  return sendAll(impl_->sock, frame);
}

bool WsLiveClient::isConnected() const { return impl_->open.load() && impl_->sock >= 0; }

}  // namespace sw
