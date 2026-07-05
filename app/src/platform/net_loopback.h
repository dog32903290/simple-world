// platform/net_loopback — localhost TCP + UDP loopback for the io network family.
//
// L5 (IO/硬體 lane) machine-verifiable half: the TCP/UDP send+receive paths proven by a localhost
// loopback (bind 127.0.0.1:port, connect/sendto self, recv/recvfrom). Zero real network, zero remote
// peer. The REAL peer (柏為's phone / a Processing sketch / a hardware controller sending to the Mac)
// is device residue: it reuses these exact send/receive paths — only the peer's origin changes.
//
// platform leaf: pure POSIX sockets (AF_INET) + a std::thread receive loop behind a C++ pimpl, so
// callers include no socket headers and no runtime headers. Mirrors osc_loopback.h's shape exactly
// (that file is the localhost-UDP-OSC precedent; this generalises it to raw TCP + raw UDP text).
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/tcp/TcpClient.cs   — connect, send UTF-8, recv → Encoding.UTF8.GetString.
//   Operators/Lib/io/tcp/TcpServer.cs   — TcpListener accept loop, per-client stream, broadcast.
//   Operators/Lib/io/udp/UDPInput.cs    — bind, Receive(ref remoteEp) → sender addr/port + UTF-8.
//   Operators/Lib/io/udp/UDPOutput.cs   — UdpClient.Send(data, targetIp, targetPort).
// TiXL's TcpClient/TcpServer/UdpClient are .NET BCL types (not portable); the byte transport here is
// self-rolled on POSIX sockets. The MESSAGE SEMANTICS (raw UTF-8 bytes, no framing) match TiXL 1:1 —
// TiXL neither adds a length prefix nor delimits; one recv = one message (see TcpClient.cs:290).
#pragma once
#include <string>

namespace sw {

// ----- UDP -------------------------------------------------------------------------------------
// A localhost UDP endpoint: binds 127.0.0.1:port, receives datagrams on a background thread, and can
// send datagrams to a target port on 127.0.0.1. Mirrors UDPInput (bind+recv, capturing sender port)
// and UDPOutput (send to a target) in a single device for loopback testing.
class UdpLoopbackDevice {
 public:
  UdpLoopbackDevice();
  ~UdpLoopbackDevice();
  UdpLoopbackDevice(const UdpLoopbackDevice&) = delete;
  UdpLoopbackDevice& operator=(const UdpLoopbackDevice&) = delete;

  // Per-datagram callback on the RECEIVE THREAD. `message` is the datagram bytes decoded as UTF-8
  // (TiXL UDPInput.cs:232 Encoding.UTF8.GetString). `senderPort` is the source port on 127.0.0.1
  // (mirrors LastSenderPort; the address is always 127.0.0.1 on loopback → not surfaced).
  using MessageCallback = void (*)(void* user, const std::string& message, int senderPort);

  // Bind 127.0.0.1:port (UDP) and start the receive thread. port 0 = OS picks a free one (read back
  // via boundPort()). Returns false on immediate failure (bind denied / busy) — non-fatal.
  bool startListening(int port, MessageCallback cb, void* user);
  void stopListening();

  // Send a UTF-8 datagram to 127.0.0.1:targetPort. Returns false if the socket is closed or send
  // failed. Mirrors UDPOutput.Send (UDPOutput.cs:133). The sender port is this device's own bound
  // port, so a loopback pair can verify LastSenderPort round-trips.
  bool sendTo(int targetPort, const std::string& message);

  bool isListening() const;
  int  boundPort() const;

 private:
  struct Impl;
  Impl* impl_;
};

// ----- TCP -------------------------------------------------------------------------------------
// A localhost TCP server: listens on 127.0.0.1:port, accepts clients on a background thread, decodes
// each recv as a UTF-8 message to a callback, and can broadcast a UTF-8 message to all connected
// clients. Mirrors TcpServer (accept loop + per-client recv + broadcast).
class TcpServerLoopback {
 public:
  TcpServerLoopback();
  ~TcpServerLoopback();
  TcpServerLoopback(const TcpServerLoopback&) = delete;
  TcpServerLoopback& operator=(const TcpServerLoopback&) = delete;

  // Per-message callback on an ACCEPT/CLIENT THREAD. `message` = one recv decoded as UTF-8
  // (TcpServer.cs:246). No framing — one TCP recv = one callback, exactly as TiXL treats it.
  using MessageCallback = void (*)(void* user, const std::string& message);

  // Listen on 127.0.0.1:port. port 0 = OS picks a free one (read via boundPort()). false on failure.
  bool startListening(int port, MessageCallback cb, void* user);
  void stopListening();

  // Broadcast a UTF-8 message to every connected client (TcpServer.cs:278 BroadcastMessageAsync).
  // Returns the number of clients written to.
  int broadcast(const std::string& message);

  bool isListening() const;
  int  boundPort() const;
  int  connectionCount() const;  // mirrors ConnectionCount output.

 private:
  struct Impl;
  Impl* impl_;
};

// A localhost TCP client: connects to 127.0.0.1:port, decodes each recv as a UTF-8 message to a
// callback, and can send a UTF-8 message. Mirrors TcpClient (connect + recv loop + send).
class TcpClientLoopback {
 public:
  TcpClientLoopback();
  ~TcpClientLoopback();
  TcpClientLoopback(const TcpClientLoopback&) = delete;
  TcpClientLoopback& operator=(const TcpClientLoopback&) = delete;

  using MessageCallback = void (*)(void* user, const std::string& message);

  // Connect to 127.0.0.1:port and start the receive thread. false on immediate connect failure
  // (mirrors TcpClient.StartAsync → catch). Blocks until connected or refused.
  bool connectTo(int port, MessageCallback cb, void* user);
  void disconnect();

  // Send a UTF-8 message (TcpClient.cs:317 SendMessageAsync). false if not connected / send failed.
  bool send(const std::string& message);

  bool isConnected() const;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace sw
