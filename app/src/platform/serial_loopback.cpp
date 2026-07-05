// platform/serial_loopback.cpp — POSIX termios serial over a PTY pair (self-rolled loopback).
//
// Ground truth (external/tixl, read-only): see serial_loopback.h header. TiXL's SerialConnectionManager
// is Editor/Player-side (not in Core) and Windows-flavoured (System.IO.Ports.SerialPort) — not
// portable — so the termios transport + line-split are self-rolled. The observable semantics match
// SerialInput/SerialOutput: raw byte write, WriteLine appends '\n', reads split into '\n'-lines.
//
// platform leaf: pure POSIX (openpty / termios / read / write) + std::thread; no runtime / app includes.
#include "platform/serial_loopback.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>  // openpty (macOS)

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace sw {
namespace {

// Map an integer baud (e.g. 9600, 115200) to the termios speed_t constant. macOS termios accepts the
// numeric value directly for cfsetspeed on standard rates; we pass it through (B9600 == 9600, etc.).
speed_t baudToSpeed(int baud) { return static_cast<speed_t>(baud); }

}  // namespace

struct SerialLoopbackDevice::Impl {
  int master = -1;  // test side (injects device→app, observes app→device)
  int slave = -1;   // device side (the app reads/writes here, as if it were a real tty)
  std::atomic<bool> running{false};
  std::thread rxThread;
  LineCallback cb = nullptr;
  void* user = nullptr;
  std::string lineBuf;  // accumulates slave-side reads until a '\n' completes a line

  // Slave-side read loop: read bytes the "device" sent, split on '\n', fire cb per completed line.
  // Mirrors SerialConnectionManager's read-buffer → ISerialReceiver.ReceiveLine(line).
  void rxLoop() {
    uint8_t buf[1024];
    while (running.load()) {
      ssize_t n = ::read(slave, buf, sizeof(buf));
      if (n <= 0) {
        if (!running.load()) break;
        continue;
      }
      for (ssize_t i = 0; i < n; ++i) {
        char c = char(buf[i]);
        if (c == '\n') {
          // strip a trailing '\r' (CRLF devices) before surfacing the line
          if (!lineBuf.empty() && lineBuf.back() == '\r') lineBuf.pop_back();
          if (cb) cb(user, lineBuf);
          lineBuf.clear();
        } else {
          lineBuf.push_back(c);
        }
      }
    }
  }
};

SerialLoopbackDevice::SerialLoopbackDevice() : impl_(new Impl()) {}
SerialLoopbackDevice::~SerialLoopbackDevice() {
  close();
  delete impl_;
}

bool SerialLoopbackDevice::openLoopback(int baud, LineCallback cb, void* user) {
  close();
  impl_->cb = cb;
  impl_->user = user;

  int master = -1, slave = -1;
  if (::openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) return false;

  // Apply raw termios at the requested baud on the slave (device side) — no echo, no line canon, so
  // bytes pass through 1:1 like a real serial link. Mirrors SerialPort defaults + BaudRate.
  termios tio{};
  if (::tcgetattr(slave, &tio) == 0) {
    cfmakeraw(&tio);
    speed_t sp = baudToSpeed(baud);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    ::tcsetattr(slave, TCSANOW, &tio);
  }

  impl_->master = master;
  impl_->slave = slave;
  impl_->running.store(true);
  impl_->rxThread = std::thread([this] { impl_->rxLoop(); });
  return true;
}

void SerialLoopbackDevice::close() {
  if (!impl_->running.load() && impl_->master < 0 && impl_->slave < 0) return;
  impl_->running.store(false);
  // Closing the master unblocks the slave read (EOF).
  if (impl_->master >= 0) { ::close(impl_->master); impl_->master = -1; }
  if (impl_->slave >= 0)  { ::close(impl_->slave);  impl_->slave = -1; }
  if (impl_->rxThread.joinable()) impl_->rxThread.join();
  impl_->lineBuf.clear();
}

bool SerialLoopbackDevice::write(const std::string& bytes) {
  if (impl_->slave < 0) return false;
  ssize_t n = ::write(impl_->slave, bytes.data(), bytes.size());
  return n == ssize_t(bytes.size());
}

bool SerialLoopbackDevice::writeLine(const std::string& line) {
  return write(line + "\n");  // SerialOutput WriteLine (AddLineEnding=true)
}

std::string SerialLoopbackDevice::readFromMaster(int maxBytes, int timeoutMs) {
  if (impl_->master < 0) return {};
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  std::string out;
  std::vector<char> tmp(size_t(maxBytes > 0 ? maxBytes : 1));
  // Set the master non-blocking so we can poll until deadline without a helper thread.
  int flags = ::fcntl(impl_->master, F_GETFL, 0);
  ::fcntl(impl_->master, F_SETFL, flags | O_NONBLOCK);
  while (int(out.size()) < maxBytes) {
    ssize_t n = ::read(impl_->master, tmp.data(), tmp.size());
    if (n > 0) {
      out.append(tmp.data(), size_t(n));
      continue;
    }
    if (std::chrono::steady_clock::now() > deadline) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ::fcntl(impl_->master, F_SETFL, flags);  // restore
  return out;
}

bool SerialLoopbackDevice::injectFromDevice(const std::string& bytes) {
  if (impl_->master < 0) return false;
  ssize_t n = ::write(impl_->master, bytes.data(), bytes.size());
  return n == ssize_t(bytes.size());
}

bool SerialLoopbackDevice::isOpen() const {
  return impl_->running.load() && impl_->slave >= 0;
}

}  // namespace sw
