// platform/net_loopback.cpp — localhost TCP + UDP over POSIX sockets (self-rolled transport).
//
// Ground truth (external/tixl, read-only): see net_loopback.h header. TiXL's transport is .NET BCL
// (System.Net.Sockets.TcpClient / TcpListener / UdpClient) — not portable — so the byte plumbing is
// self-rolled on POSIX sockets. The observable SEMANTICS match TiXL: raw UTF-8 payload, no framing,
// one recv == one message; UDP recvfrom captures the sender port (UDPInput LastSenderPort).
//
// platform leaf: pure POSIX sockets + std::thread; no runtime / app includes.
#include "platform/net_loopback.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace sw {
namespace {

// Fill a sockaddr_in for 127.0.0.1:port.
sockaddr_in loopbackAddr(int port) {
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(uint16_t(port));
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return a;
}

// Read back the actual port a socket bound to (recovers the OS-picked port when bind(port=0)).
int actualPort(int sock, int fallback) {
  sockaddr_in a{};
  socklen_t alen = sizeof(a);
  if (::getsockname(sock, reinterpret_cast<sockaddr*>(&a), &alen) == 0) return ntohs(a.sin_port);
  return fallback;
}

}  // namespace

// ===== UDP =====================================================================================

struct UdpLoopbackDevice::Impl {
  int sock = -1;
  int port = 0;
  std::atomic<bool> running{false};
  std::thread rxThread;
  MessageCallback cb = nullptr;
  void* user = nullptr;

  void rxLoop() {
    uint8_t buf[65536];
    while (running.load()) {
      sockaddr_in src{};
      socklen_t slen = sizeof(src);
      ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&src), &slen);
      if (n <= 0) {
        if (!running.load()) break;
        continue;
      }
      int senderPort = ntohs(src.sin_port);
      std::string msg(reinterpret_cast<const char*>(buf), size_t(n));  // UTF-8 bytes, as TiXL
      if (cb) cb(user, msg, senderPort);
    }
  }
};

UdpLoopbackDevice::UdpLoopbackDevice() : impl_(new Impl()) {}
UdpLoopbackDevice::~UdpLoopbackDevice() {
  stopListening();
  delete impl_;
}

bool UdpLoopbackDevice::startListening(int port, MessageCallback cb, void* user) {
  stopListening();
  impl_->cb = cb;
  impl_->user = user;

  int s = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return false;
  int yes = 1;
  ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));  // UDPInput.cs:148 ReuseAddress
  sockaddr_in addr = loopbackAddr(port);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(s);
    return false;  // port busy / denied — caller may skip
  }
  impl_->port = actualPort(s, port);
  impl_->sock = s;
  impl_->running.store(true);
  impl_->rxThread = std::thread([this] { impl_->rxLoop(); });
  return true;
}

void UdpLoopbackDevice::stopListening() {
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

bool UdpLoopbackDevice::sendTo(int targetPort, const std::string& message) {
  if (impl_->sock < 0) return false;
  sockaddr_in dst = loopbackAddr(targetPort);
  ssize_t s = ::sendto(impl_->sock, message.data(), message.size(), 0,
                       reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
  return s == ssize_t(message.size());
}

bool UdpLoopbackDevice::isListening() const { return impl_->running.load() && impl_->sock >= 0; }
int  UdpLoopbackDevice::boundPort() const { return impl_->port; }

// ===== TCP server ==============================================================================

struct TcpServerLoopback::Impl {
  int listenSock = -1;
  int port = 0;
  std::atomic<bool> running{false};
  std::thread acceptThread;
  MessageCallback cb = nullptr;
  void* user = nullptr;

  std::mutex clientsMx;
  std::vector<int> clients;             // connected client fds
  std::vector<std::thread> clientThreads;

  void removeClient(int fd) {
    std::lock_guard<std::mutex> lk(clientsMx);
    for (size_t i = 0; i < clients.size(); ++i) {
      if (clients[i] == fd) { clients.erase(clients.begin() + i); break; }
    }
  }

  // Per-client recv loop → cb per message. One recv == one message (no framing), as TcpServer.cs.
  void clientLoop(int fd) {
    uint8_t buf[8192];
    while (running.load()) {
      ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) break;  // 0 = graceful close, <0 = error/shutdown
      std::string msg(reinterpret_cast<const char*>(buf), size_t(n));
      if (cb) cb(user, msg);
    }
    ::close(fd);
    removeClient(fd);
  }

  void acceptLoop() {
    while (running.load()) {
      int fd = ::accept(listenSock, nullptr, nullptr);
      if (fd < 0) {
        if (!running.load()) break;
        continue;
      }
      {
        std::lock_guard<std::mutex> lk(clientsMx);
        clients.push_back(fd);
        clientThreads.emplace_back([this, fd] { clientLoop(fd); });
      }
    }
  }
};

TcpServerLoopback::TcpServerLoopback() : impl_(new Impl()) {}
TcpServerLoopback::~TcpServerLoopback() {
  stopListening();
  delete impl_;
}

bool TcpServerLoopback::startListening(int port, MessageCallback cb, void* user) {
  stopListening();
  impl_->cb = cb;
  impl_->user = user;

  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return false;
  int yes = 1;
  ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr = loopbackAddr(port);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(s);
    return false;
  }
  if (::listen(s, 8) != 0) {
    ::close(s);
    return false;
  }
  impl_->port = actualPort(s, port);
  impl_->listenSock = s;
  impl_->running.store(true);
  impl_->acceptThread = std::thread([this] { impl_->acceptLoop(); });
  return true;
}

void TcpServerLoopback::stopListening() {
  if (!impl_->running.load() && impl_->listenSock < 0) return;
  impl_->running.store(false);
  if (impl_->listenSock >= 0) {
    ::shutdown(impl_->listenSock, SHUT_RDWR);
    ::close(impl_->listenSock);  // unblocks accept
    impl_->listenSock = -1;
  }
  // Close all client fds to unblock their recv loops, then join.
  {
    std::lock_guard<std::mutex> lk(impl_->clientsMx);
    for (int fd : impl_->clients) ::shutdown(fd, SHUT_RDWR);
  }
  if (impl_->acceptThread.joinable()) impl_->acceptThread.join();
  for (auto& t : impl_->clientThreads) {
    if (t.joinable()) t.join();
  }
  impl_->clientThreads.clear();
  {
    std::lock_guard<std::mutex> lk(impl_->clientsMx);
    impl_->clients.clear();
  }
  impl_->port = 0;
}

int TcpServerLoopback::broadcast(const std::string& message) {
  std::lock_guard<std::mutex> lk(impl_->clientsMx);
  int sent = 0;
  for (int fd : impl_->clients) {
    ssize_t n = ::send(fd, message.data(), message.size(), 0);
    if (n == ssize_t(message.size())) sent++;
  }
  return sent;
}

bool TcpServerLoopback::isListening() const {
  return impl_->running.load() && impl_->listenSock >= 0;
}
int TcpServerLoopback::boundPort() const { return impl_->port; }
int TcpServerLoopback::connectionCount() const {
  std::lock_guard<std::mutex> lk(impl_->clientsMx);
  return int(impl_->clients.size());
}

// ===== TCP client ==============================================================================

struct TcpClientLoopback::Impl {
  int sock = -1;
  std::atomic<bool> running{false};
  std::thread rxThread;
  MessageCallback cb = nullptr;
  void* user = nullptr;

  void rxLoop() {
    uint8_t buf[8192];
    while (running.load()) {
      ssize_t n = ::recv(sock, buf, sizeof(buf), 0);
      if (n <= 0) break;
      std::string msg(reinterpret_cast<const char*>(buf), size_t(n));
      if (cb) cb(user, msg);
    }
  }
};

TcpClientLoopback::TcpClientLoopback() : impl_(new Impl()) {}
TcpClientLoopback::~TcpClientLoopback() {
  disconnect();
  delete impl_;
}

bool TcpClientLoopback::connectTo(int port, MessageCallback cb, void* user) {
  disconnect();
  impl_->cb = cb;
  impl_->user = user;

  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return false;
  sockaddr_in addr = loopbackAddr(port);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(s);
    return false;  // connection refused — mirrors TcpClient connect catch
  }
  int yes = 1;
  ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));  // send small messages immediately
  impl_->sock = s;
  impl_->running.store(true);
  impl_->rxThread = std::thread([this] { impl_->rxLoop(); });
  return true;
}

void TcpClientLoopback::disconnect() {
  if (!impl_->running.load() && impl_->sock < 0) return;
  impl_->running.store(false);
  if (impl_->sock >= 0) {
    ::shutdown(impl_->sock, SHUT_RDWR);
    ::close(impl_->sock);
    impl_->sock = -1;
  }
  if (impl_->rxThread.joinable()) impl_->rxThread.join();
}

bool TcpClientLoopback::send(const std::string& message) {
  if (impl_->sock < 0) return false;
  ssize_t n = ::send(impl_->sock, message.data(), message.size(), 0);
  return n == ssize_t(message.size());
}

bool TcpClientLoopback::isConnected() const { return impl_->running.load() && impl_->sock >= 0; }

}  // namespace sw
