// platform/serial_loopback — POSIX termios serial port + a PTY-pair loopback for testing.
//
// L5 (IO/硬體 lane) machine-verifiable half: the serial send/receive + line-splitting paths proven
// by an in-process PTY pair (openpty → master/slave). No physical /dev/tty.usbserial device, no TCC.
// The REAL device (柏為 plugs in an Arduino / WLED strip) is device residue: SerialInput/Output open
// a real /dev/tty.* by name and use the SAME read/write + line-split path — only the fd's origin
// changes from our PTY slave to a hardware tty.
//
// platform leaf: pure POSIX (openpty / termios / read / write) behind a C++ pimpl; no runtime / app
// includes. Line splitting (SerialInput surfaces one ReceivedString per newline-terminated line) is
// self-rolled here; TiXL's SerialConnectionManager lives in the Editor/Player half (not in Core, not
// portable), so the manager's read-buffer→line semantics are re-implemented 1:1.
//
// Ground truth mirrored (external/tixl, read-only):
//   Operators/Lib/io/serial/SerialInput.cs   — ISerialReceiver.ReceiveLine(line) per line; baud.
//   Operators/Lib/io/serial/SerialOutput.cs  — Write / WriteLine (AddLineEnding appends '\n').
//   Operators/Lib/io/serial/WLedSerialOutput.cs — Adalight frame (see wled_frame.h for the codec).
#pragma once
#include <string>

namespace sw {

// A serial endpoint backed by a PTY pair for loopback. `openLoopback` creates a master/slave pair:
// the slave is the "device side" (what a real tty would be), the master is the "test side" that
// injects bytes as if a device sent them. Writes go slave→master (the app writes to the device);
// injectFromDevice writes master→slave (the device sends to the app). Received lines (slave-side
// reads, split on '\n') fire the callback — mirroring SerialInput.ISerialReceiver.ReceiveLine.
class SerialLoopbackDevice {
 public:
  SerialLoopbackDevice();
  ~SerialLoopbackDevice();
  SerialLoopbackDevice(const SerialLoopbackDevice&) = delete;
  SerialLoopbackDevice& operator=(const SerialLoopbackDevice&) = delete;

  // One decoded line (newline stripped) on the READ THREAD, per SerialInput ReceiveLine semantics.
  using LineCallback = void (*)(void* user, const std::string& line);

  // Create the PTY pair, apply raw termios at `baud`, start the slave-side read thread. false on
  // failure (openpty denied). baud is applied via cfsetspeed (mirrors SerialPort.BaudRate).
  bool openLoopback(int baud, LineCallback cb, void* user);
  void close();

  // ---- device-side (app) writes ----
  // Write raw bytes to the device (slave fd), mirroring SerialOutput.Write / WLED WriteBytes.
  bool write(const std::string& bytes);
  // Write + append '\n' (SerialOutput WriteLine, AddLineEnding=true).
  bool writeLine(const std::string& line);
  // Read whatever the app-side wrote, as seen on the master (test observes device-bound bytes).
  // Blocks up to timeoutMs. Returns the bytes read (may be empty on timeout).
  std::string readFromMaster(int maxBytes, int timeoutMs);

  // ---- test-side injection ----
  // Inject bytes master→slave as if the physical device sent them; the read thread splits lines and
  // fires the callback. Used by the selftest to prove the receive+line-split path.
  bool injectFromDevice(const std::string& bytes);

  bool isOpen() const;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace sw
