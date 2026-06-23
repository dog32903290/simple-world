// app/src/selftests_io.cpp — L5 IO/硬體 loopback harness 驗證（area manifest leaf for --selftest）
//
// 機器驗半（localhost UDP OSC + 虛擬 CoreMIDI loopback）。實體裝置（真 MIDI 控制器 / OSC 手機 /
// audio-in）= 柏為殘留，重用同一條 receive+decode path，非本檔阻擋。
//
// Shell-tier (app/src/ root, like the other selftests_<area>.cpp): self-registers its rows into
// selftestRegistry() during pre-main dynamic init; selftests.cpp reads that sink. LEAF-LOCAL — it
// includes its own platform headers directly, so selftests_decls.h is NOT touched (the two IO fns
// have no other caller). Reached via main's --selftest-io-osc-loopback / --selftest-io-midi-loopback
// (and the auto -bug refuter variants).
//
// Ground truth mirrored: OscConnectionManager.cs:219 (float coercion), MidiInput.cs:296 (MIDI decode),
// MidiInput.cs:208 (range remap), MidiInput.cs:401 (channel/control match), OscInput.cs:241 (addr match).
#include "runtime/selftest_registry.h"
#include "platform/osc_loopback.h"
#include "platform/midi_loopback.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace sw {
namespace {

// One received OSC arg, captured by the loopback callback.
struct OscRx {
  std::string address;
  float value;
  int argIndex;
};
struct OscCapture {
  std::vector<OscRx> rx;
  std::atomic<int> count{0};
};

void oscCb(void* user, const std::string& addr, float value, int argIndex) {
  auto* cap = static_cast<OscCapture*>(user);
  cap->rx.push_back({addr, value, argIndex});
  cap->count.fetch_add(1);
}

// Block until the capture has at least `n` messages or `timeoutMs` elapses. Returns true if reached.
bool waitForOsc(OscCapture& cap, int n, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (cap.count.load() < n) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // small settle so a trailing message in the same wait window lands too
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return true;
}

bool feq(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

// --selftest-io-osc-loopback : send a known OSC message over localhost UDP to ourselves, receive it,
// decode + coerce to float, assert each value. injectBug corrupts the first expected value so the
// tooth bites.
int runOscLoopbackSelfTest(bool injectBug) {
  bool ok = true;

  // 1) Direct coercion-table assertions for the byte-order-INDEPENDENT type tags (bool/string). The
  //    big-endian 'f'/'i'/'d' branches are proven end-to-end by the socket path below (which encodes
  //    proper big-endian on the wire, then decodes). This is the exact TryGetFloatFromMessagePart
  //    table for the branches that need no wire encoding.
  {
    float v = 0.f;
    ok = ok && oscCoerceToFloat('T', nullptr, 0, v) && feq(v, 1.0f);   // bool true → 1
    ok = ok && oscCoerceToFloat('F', nullptr, 0, v) && feq(v, 0.0f);   // bool false → 0
    ok = ok && oscCoerceToFloat('s', "3.5", 3, v) && feq(v, 3.5f);     // string "3.5" → 3.5
    bool parsedBad = oscCoerceToFloat('s', "invalid", 7, v);           // string "invalid" → NaN/false
    ok = ok && !parsedBad && std::isnan(v);
    bool parsedTrailing = oscCoerceToFloat('s', "3.5x", 4, v);         // trailing garbage → reject
    ok = ok && !parsedTrailing;
  }

  // 2) Loopback over the wire: bind ephemeral localhost port, sendto self, recvfrom, decode.
  OscCapture cap;
  OscLoopbackDevice osc;
  if (!osc.startListening(0 /*ephemeral*/, oscCb, &cap)) {
    // bind denied (sandbox) — graceful skip, but a -bug run must still FAIL so the tooth bites.
    std::printf("[selftest-io-osc-loopback] bind failed -> SKIP transport (env restricted)\n");
    if (injectBug) return 1;
    return ok ? 0 : 1;
  }

  osc.sendTestFloat("/test/val", 0.75f);   // → 0.75
  osc.sendTestInt("/test/int", 42);        // int → 42.0
  osc.sendTestString("/test/str", "3.5");  // string → 3.5
  osc.sendTestFloatInt("/test/mixed", 0.5f, 10);  // two args → [0.5, 10.0]
  osc.sendTestBundle("/test/a", 1.0f, "/test/b", 2.0f);  // bundle → two msgs

  // expected total args: 1 + 1 + 1 + 2 + 2 = 7
  bool arrived = waitForOsc(cap, 7, 1000);
  ok = ok && arrived;

  // Tally by address (order across datagrams is not guaranteed; assert by address+argIndex).
  auto findVal = [&](const std::string& a, int idx, float& out) -> bool {
    for (auto& r : cap.rx) if (r.address == a && r.argIndex == idx) { out = r.value; return true; }
    return false;
  };
  float v = 0.f;
  ok = ok && findVal("/test/val", 0, v) && feq(v, injectBug ? 999.f : 0.75f);  // refuter corrupts this
  ok = ok && findVal("/test/int", 0, v) && feq(v, 42.0f);
  ok = ok && findVal("/test/str", 0, v) && feq(v, 3.5f);
  ok = ok && findVal("/test/mixed", 0, v) && feq(v, 0.5f);
  ok = ok && findVal("/test/mixed", 1, v) && feq(v, 10.0f);
  ok = ok && findVal("/test/a", 0, v) && feq(v, 1.0f);
  ok = ok && findVal("/test/b", 0, v) && feq(v, 2.0f);

  osc.stopListening();
  std::printf("[selftest-io-osc-loopback] received=%d expected=7 -> %s\n",
              cap.count.load(), ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

namespace {
struct MidiRx {
  MidiEventKind kind;
  int channel, controllerId, controllerValue;
};
struct MidiCapture {
  std::vector<MidiRx> rx;
  std::atomic<int> count{0};
};
void midiCb(void* user, MidiEventKind kind, int channel, int controllerId, int controllerValue) {
  auto* cap = static_cast<MidiCapture*>(user);
  cap->rx.push_back({kind, channel, controllerId, controllerValue});
  cap->count.fetch_add(1);
}
bool waitForMidi(MidiCapture& cap, int n, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (cap.count.load() < n) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return true;
}
}  // namespace

// --selftest-io-midi-loopback : inject raw MIDI events into a virtual CoreMIDI source, receive them
// through our own input port, decode like TiXL ProcessParsedMidiEvent, assert value. injectBug
// corrupts the expected NoteOn velocity so the tooth bites.
int runMidiLoopbackSelfTest(bool injectBug) {
  bool ok = true;

  // 1) Direct decode-table assertions (no CoreMIDI) — the exact ProcessParsedMidiEvent mapping.
  {
    MidiEventKind k; int ch, id, val;
    // NoteOn ch1 note60 vel64 → Note, id=60, val=64 (channel is 1-based: 0x90 low-nibble=0 → ch=1)
    ok = ok && midiDecodeRaw(0x90, 60, 64, k, ch, id, val) &&
         k == MidiEventKind::Note && ch == 1 && id == 60 && val == 64;
    // NoteOff ch0 note60 vel32 → val forced 0
    ok = ok && midiDecodeRaw(0x80, 60, 32, k, ch, id, val) &&
         k == MidiEventKind::Note && val == 0;
    // NoteOn vel0 (NAudio fork → NoteOff) → val 0
    ok = ok && midiDecodeRaw(0x90, 60, 0, k, ch, id, val) && val == 0;
    // CC ch0 ctrl7 val100 → ControllerChange, id=7, val=100
    ok = ok && midiDecodeRaw(0xB0, 7, 100, k, ch, id, val) &&
         k == MidiEventKind::ControllerChange && id == 7 && val == 100;
    // PitchWheel ch0 LSB=0x68 MSB=0x07 → 14-bit = 0x68 | (0x07<<7) = 0x3E8 = 1000
    ok = ok && midiDecodeRaw(0xE0, 0x68, 0x07, k, ch, id, val) &&
         k == MidiEventKind::Other && val == 1000;
    // NoteOn ch6 note40 vel127 → channel=6, val=127 (0x95 low-nibble=5 → ch=6, 1-based)
    ok = ok && midiDecodeRaw(0x95, 40, 127, k, ch, id, val) && ch == 6 && val == 127;
    // status byte < 0x80 → no signal
    ok = ok && !midiDecodeRaw(0x40, 1, 2, k, ch, id, val);
  }

  // 2) Loopback through CoreMIDI virtual source → input port.
  MidiCapture cap;
  MidiLoopbackDevice midi;
  if (!midi.startListening(midiCb, &cap)) {
    std::printf("[selftest-io-midi-loopback] CoreMIDI unavailable -> SKIP transport\n");
    if (injectBug) return 1;
    return ok ? 0 : 1;
  }

  midi.sendRawMidi(0x90, 60, 64);   // NoteOn ch1 note60 vel64  (low nibble 0 → channel=1)
  midi.sendRawMidi(0xB0, 7, 100);   // CC ch1 ctrl7 val100
  midi.sendRawMidi(0x95, 40, 127);  // NoteOn ch6 note40 vel127 (low nibble 5 → channel=6)

  bool arrived = waitForMidi(cap, 3, 1000);
  ok = ok && arrived;

  if (arrived) {
    // order is preserved within a single source's stream
    // injectBug: corrupt the expected channel (wrong 0-based value) so the channel tooth bites;
    // velocity expectation is kept at true value 64 so only the channel assertion drives RED.
    int expCh0 = injectBug ? 0 : 1;   // ch1 true; bug injects 0 (old 0-based wrong value)
    int expCh5 = injectBug ? 5 : 6;   // ch6 true; bug injects 5 (old 0-based wrong value)
    ok = ok && cap.rx[0].kind == MidiEventKind::Note &&
         cap.rx[0].channel == expCh0 &&
         cap.rx[0].controllerId == 60 && cap.rx[0].controllerValue == 64;
    ok = ok && cap.rx[1].kind == MidiEventKind::ControllerChange &&
         cap.rx[1].controllerId == 7 && cap.rx[1].controllerValue == 100;
    ok = ok && cap.rx[2].kind == MidiEventKind::Note &&
         cap.rx[2].channel == expCh5 && cap.rx[2].controllerValue == 127;
  }

  midi.stopListening();
  std::printf("[selftest-io-midi-loopback] received=%d expected=3 -> %s\n",
              cap.count.load(), ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/310,
    {"io-osc-loopback",   runOscLoopbackSelfTest},
    {"io-midi-loopback",  runMidiLoopbackSelfTest});

}  // namespace sw
