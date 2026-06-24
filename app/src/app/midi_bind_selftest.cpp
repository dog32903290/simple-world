// app/midi_bind_selftest.cpp — P3 live-control pipe loopback golden (--selftest-midi-bind).
//
// Split out of midi_bind.cpp (rule 4 line-count): the seam's own harness, self-registers its row.
//
// THE GATE (machine, harness-gap-immune): the value a live MIDI CC / OSC arg carries must reach a
// GRAPH PARAMETER through the cook-side wire. We build a tiny composition (one child with a Float
// "amount" input), enter learn on that param, inject a CC over the REAL virtual-CoreMIDI loopback (and
// a fallback pure-injection path when CoreMIDI is unavailable), then tick() and read the param back via
// effectiveInput — the SAME readback the resident cook walks. The closed-form CC 64/127 → 0.503937 now
// lands on the child's override. A wrong-channel CC must NOT move it; -bug asserts the wrong value /
// the unbound state → RED.
//
// Ground truth mirrored (external/tixl, read-only):
//   MidiInput.cs:131-149  learn capture: first event during _teachingActive trains channel/control/kind.
//   MidiInput.cs:208      remap controllerValue over 0..127 into OutputRange (here [0,1]).
//   MidiInput.cs:401-414  match: trained channel/controller must equal (the learned concrete values).
#include "app/midi_bind.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "platform/midi_loopback.h"      // MidiLoopbackDevice (drive the real virtual-CoreMIDI loopback)
#include "runtime/compound_graph.h"      // SymbolLibrary / Symbol / SymbolChild / effectiveInput
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS

namespace sw::midibind {
namespace {

constexpr float kTol = 1e-4f;
bool feq(float a, float b) { return std::fabs(a - b) < kTol; }

// One composition "comp" with a single child (id 1) of an atomic Op exposing Float input "amount"
// (default 0). Mirrors variation_live's buildLib — the narrowed single-param target.
SymbolLibrary buildLib() {
  SymbolLibrary lib;
  Symbol op;
  op.id = "Op"; op.name = "Op"; op.atomic = true;
  op.inputDefs = {{"amount", "Amount", "Float", 0.0f, "", 0, 0}};
  lib.symbols["Op"] = op;

  Symbol comp;
  comp.id = "comp"; comp.name = "comp"; comp.atomic = false;
  SymbolChild child; child.id = 1; child.symbolId = "Op";
  comp.children.push_back(child);
  comp.nextChildId = 2;
  lib.symbols["comp"] = comp;
  lib.rootId = "comp";
  return lib;
}

// Read the live override value on (comp, child 1, amount) through the GRAPH (effectiveInput) — the same
// path the resident cook walks. -999 sentinel never collides with a real remapped value in [0,1].
float paramValue(const SymbolLibrary& lib) {
  const Symbol* comp = lib.find("comp");
  if (!comp) return -999.f;
  const SymbolChild* c = childById(*comp, 1);
  if (!c) return -999.f;
  return effectiveInput(lib, *c, "amount", -999.f);
}

// GOLDEN — learn + cook-side wire over the REAL virtual-CoreMIDI loopback. Returns true on PASS. When
// CoreMIDI is unavailable, falls back to the pure injection path (injectMidiForTest) so the cook-side
// correctness is still proven in restricted envs (the transport leg is separately proven by
// --selftest-io-midi-loopback).
bool goldenMidiLearnToGraph(bool injectBug) {
  SymbolLibrary lib = buildLib();
  reset();

  // 1) Enter learn on (comp, child 1, amount). Nothing bound yet, param at its default 0.
  const float before = paramValue(lib);
  beginLearn("comp", 1, "amount");
  bool learnWasActive = learnActive();

  bool usedTransport = false;
  MidiLoopbackDevice midi;
  // The live ingest callback the app hook installs reads the module's own table; to drive the REAL
  // transport here we register the app's live sources (which install the learn-aware callbacks) and
  // send through a SEPARATE virtual device that round-trips into the SAME CoreMIDI loopback the app
  // listens on. Simpler + deterministic: register the app sources, then push raw MIDI through a local
  // device — but the app's source is the one with the learn callback. So we drive the app's transport
  // directly: registerIoLiveSources() owns the listening device; we send through a peer device that
  // shares the virtual-source loopback. If CoreMIDI is up, the app callback captures + ingests.
  if (registerIoLiveSources() && ioLiveSourcesRunning() && midi.startListening(
          [](void*, MidiEventKind, int, int, int) {}, nullptr)) {
    // A CC ch1 ctrl7 val64 round-trips into BOTH listeners' virtual source; the app's learn callback
    // captures (ch1 ctrl7) and ingests 64 → remap 64/127. Send + small settle handled by the device.
    midi.sendRawMidi(0xB0, 7, 64);   // CC ch1 ctrl7 val64
    // Give the read thread a beat (the loopback selftest uses a 5ms post-arrival settle; mirror it).
    for (volatile int spin = 0; spin < 2000000; ++spin) {}
    usedTransport = true;
    midi.stopListening();
    shutdownIoLiveSources();
  }
  // Fallback (or belt-and-braces): drive the learn+ingest deterministically through the injector. If
  // the transport already captured, learn is no longer active, so this is a plain matching CC update to
  // the SAME ch1/ctrl7 binding (idempotent: 64 → 64/127 again). If the transport path was skipped, this
  // performs the learn capture itself.
  injectMidiForTest(/*kind=*/1, /*channel=*/1, /*controllerId=*/7, /*controllerValue=*/64);

  // 2) The binding must be recorded (the learn side-effect) and routed to one graph param.
  bool bound = isParamBound(1, "amount");
  bool oneBound = (boundParamCount() == 1);

  // 3) A WRONG-channel CC must NOT move the param (the trained ch1 filter rejects ch6).
  injectMidiForTest(/*kind=*/1, /*channel=*/6, /*controllerId=*/7, /*controllerValue=*/127);

  // 4) Cook-side wire: tick writes the bound value onto the graph param; read it back via the GRAPH.
  tick(lib);
  const float after = paramValue(lib);

  // The closed-form: CC 64 over [0,127]→[0,1] = 64/127 = 0.503937. -bug expects the wrong value (the
  // wrong-channel 127/127≈0.787 leaking in) → mismatch RED.
  const float want = injectBug ? 0.787402f : 0.503937f;
  bool ok = learnWasActive && bound && oneBound && feq(after, want);
  // before is the default 0 (proves the value actually MOVED, not a pre-seeded constant).
  ok = ok && feq(before, 0.0f);

  std::printf("[selftest-midi-bind] LEARN+COOK (%s) before=%.4f after=%.6f want=%.6f "
              "bound=%s -> %s\n",
              usedTransport ? "transport+inject" : "inject-only", before, after, want,
              bound ? "y" : "n", ok ? "PASS" : "FAIL");
  reset();
  return ok;
}

// GOLDEN — OSC learn + cook-side wire (pure injection; the OSC transport leg is proven by
// --selftest-io-live-binding). Learn binds the next OSC address; a 0.75 arg flows raw (identity) onto
// the graph param. -bug expects a non-matching address to have moved it → RED.
bool goldenOscLearnToGraph(bool injectBug) {
  SymbolLibrary lib = buildLib();
  reset();

  beginLearn("comp", 1, "opacityParam");  // slot name is "amount" in the test op; use it:
  // (the test op only exposes "amount"; re-target onto it so effectiveInput reads a real slot)
  cancelLearn();
  beginLearn("comp", 1, "amount");

  injectOscForTest("/fader", 0.75f);   // learn captures "/fader"; identity → 0.75
  bool bound = isParamBound(1, "amount");

  injectOscForTest("/knob", 0.20f);    // WRONG address (not a /fader prefix) → must NOT move the param

  tick(lib);
  const float after = paramValue(lib);
  const float want = injectBug ? 0.20f : 0.75f;  // bug: the /knob value leaked in
  bool ok = bound && feq(after, want);

  std::printf("[selftest-midi-bind] OSC-LEARN+COOK after=%.4f want=%.4f bound=%s -> %s\n",
              after, want, bound ? "y" : "n", ok ? "PASS" : "FAIL");
  reset();
  return ok;
}

// GOLDEN — unbind clears the route (the cook-side wire stops writing). After unbind a new event on the
// old filter must NOT move the param. -bug: assert the param still moved (route not really cleared) → RED.
bool goldenUnbind(bool injectBug) {
  SymbolLibrary lib = buildLib();
  reset();
  beginLearn("comp", 1, "amount");
  injectMidiForTest(1, 1, 7, 127);       // bind + value 1.0
  tick(lib);
  const float boundVal = paramValue(lib);  // ~1.0

  bool removed = unbindParam(1, "amount");
  // Manually drop the override the bound tick wrote (unbind stops FUTURE writes; the last value stays
  // until the param is reset by the user — we reset it here to isolate "no new writes").
  if (Symbol* comp = lib.find("comp"))
    if (SymbolChild* c = childById(*comp, 1)) c->overrides.erase("amount");

  injectMidiForTest(1, 1, 7, 64);   // an event on the OLD filter — but the route is gone
  tick(lib);
  const float after = paramValue(lib);   // should stay at the default 0 (no route writes it)

  bool ok = removed && feq(boundVal, 1.0f) && (boundParamCount() == 0);
  ok = ok && (injectBug ? !feq(after, 0.0f) : feq(after, 0.0f));
  std::printf("[selftest-midi-bind] UNBIND boundVal=%.4f after-unbind=%.4f removed=%s -> %s\n",
              boundVal, after, removed ? "y" : "n", ok ? "PASS" : "FAIL");
  reset();
  return ok;
}

}  // namespace

int runMidiBindSelfTest(bool injectBug) {
  bool ok = true;
  ok = goldenMidiLearnToGraph(injectBug) && ok;
  ok = goldenOscLearnToGraph(injectBug) && ok;
  ok = goldenUnbind(injectBug) && ok;
  std::printf("[selftest-midi-bind] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/314,
    {"midi-bind", runMidiBindSelfTest});

}  // namespace sw::midibind
