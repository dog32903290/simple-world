// app/src/selftests_io_live_binding.cpp — L5 IO binding layer (LiveBindingTable) loopback harness.
//
// Split out of selftests_io.cpp (rule 4 line-count ratchet) when the Damping + DefaultOutputValue
// golden pushed the combined file over 400 lines. Same shell tier: self-registers its row into
// selftestRegistry() during pre-main dynamic init (orderBase 312, the slot the live-binding row held
// in the combined file). LEAF-LOCAL — includes its own platform headers directly.
//
// What it verifies: the binding layer that turns a decoded OSC-address / MIDI-CC event into a remapped,
// damped, named target value. Drives REAL loopback transports (localhost UDP OSC + virtual CoreMIDI)
// for the end-to-end half, plus pure-table closed-form assertions for the arithmetic.
//
// Ground truth mirrored (external/tixl, read-only):
//   MidiInput.cs:208-210  remap controllerValue over 0..127 into OutputRange (MathUtils.Remap).
//   MidiInput.cs:218      damping:  _dampedOutputValue = Lerp(currentValue, _dampedOutputValue, damping).
//   MidiInput.cs:201      default:  Result.Value = defaultOutputValue while _isDefaultValue (pre-event).
//   MidiInput.cs:171/459  _isDefaultValue starts true, clears on the first matching event.
//   MidiInput.cs:401-414  channel/control/eventType match (<0 / Any = match anything).
//   MathUtils.cs:305-308  Lerp(a,b,t) = a + (b - a) * t.
//   OscInput.cs:241-242   OSC address match = msg.Address.StartsWith(address); value flows raw.
#include "runtime/selftest_registry.h"
#include "platform/osc_loopback.h"
#include "platform/midi_loopback.h"
#include "platform/live_binding.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

namespace sw {
namespace {

bool feq(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// MIDI loopback callback feeds a shared table on the read thread. kindToMatch converts the transport's
// decoded MidiEventKind into the binding-filter MidiMatchKind (1:1).
MidiMatchKind kindToMatch(MidiEventKind k) {
  switch (k) {
    case MidiEventKind::Note:             return MidiMatchKind::Note;
    case MidiEventKind::ControllerChange: return MidiMatchKind::ControllerChange;
    case MidiEventKind::Other:            return MidiMatchKind::Other;
  }
  return MidiMatchKind::Any;
}

struct BindingMidiCtx {
  LiveBindingTable* table;
  std::atomic<int>  count{0};
};
void bindingMidiCb(void* user, MidiEventKind kind, int channel, int controllerId, int controllerValue) {
  auto* ctx = static_cast<BindingMidiCtx*>(user);
  ctx->table->ingestMidi(kindToMatch(kind), channel, controllerId, controllerValue);
  ctx->count.fetch_add(1);
}

struct BindingOscCtx {
  LiveBindingTable* table;
  std::atomic<int>  count{0};
};
void bindingOscCb(void* user, const std::string& address, float value, int /*argIndex*/) {
  auto* ctx = static_cast<BindingOscCtx*>(user);
  ctx->table->ingestOsc(address, value);
  ctx->count.fetch_add(1);
}

template <typename Ctx>
bool waitForCount(Ctx& ctx, int n, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (ctx.count.load() < n) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  return true;
}
}  // namespace

int runLiveBindingSelfTest(bool injectBug) {
  bool ok = true;

  // 1) Pure remap-table assertions (no transport) — the exact MathUtils.Remap closed-form. These prove
  //    the arithmetic byte-faithfully independent of the socket/CoreMIDI path.
  {
    // CC 64 over [0,127] → [0,1] = 64/127 = 0.50393701...  (the headline closed-form from the spec)
    float v = remapLinear(64.f, 0.f, 127.f, 0.f, 1.f);
    ok = ok && feq(v, 0.503937f);
    // CC 127 over [0,127] → [0,1] = 1.0 (max)
    ok = ok && feq(remapLinear(127.f, 0.f, 127.f, 0.f, 1.f), 1.0f);
    // CC 0 → 0.0 (min)
    ok = ok && feq(remapLinear(0.f, 0.f, 127.f, 0.f, 1.f), 0.0f);
    // Non-default out-range proves the affine term: CC 64 over [0,127] → [10,20] = 10 + 0.503937*10 = 15.03937
    ok = ok && feq(remapLinear(64.f, 0.f, 127.f, 10.f, 20.f), 15.03937f);
    // OSC identity (TiXL OscInput has no remap): value 0.75 over [0,1]→[0,1] = 0.75 unchanged
    ok = ok && feq(remapLinear(0.75f, 0.f, 1.f, 0.f, 1.f), 0.75f);
  }

  // 1b) Damping closed-form (TiXL MidiInput.cs:218 = Lerp(new, dampedPrev, damping)) + the
  //     DefaultOutputValue-before-any-event path (MidiInput.cs:201). Pure table, no transport, so the
  //     damping sequence is fully deterministic.
  {
    // lerpLinear faithfulness (MathUtils.cs:305-308): Lerp(1,0,0.5)=0.5; damping=0 returns the raw new.
    ok = ok && feq(lerpLinear(1.0f, 0.0f, 0.5f), 0.5f);
    ok = ok && feq(lerpLinear(1.0f, 0.0f, 0.0f), 1.0f);   // damping=0 ⇒ raw new value (no smoothing)
    ok = ok && feq(lerpLinear(1.0f, 0.5f, 1.0f), 0.5f);   // damping=1 ⇒ frozen at previous

    // (i) Damping=0 regression guard: with damping 0 the target equals the plain remap, event 1 and 2
    //     unchanged — proving the new path does not perturb the previously-shipped behavior.
    {
      LiveBindingTable t;
      LiveBinding b; b.source = LiveSourceKind::Midi; b.midiKind = MidiMatchKind::ControllerChange;
      b.midiChannel = 1; b.midiControl = 7; b.target = "raw"; b.damping = 0.0f;
      t.addBinding(b);
      t.ingestMidi(MidiMatchKind::ControllerChange, 1, 7, 64);
      float v = -1.f; ok = ok && t.valueForTarget("raw", v) && feq(v, 0.503937f);  // 64/127
      t.ingestMidi(MidiMatchKind::ControllerChange, 1, 7, 127);
      v = -1.f; ok = ok && t.valueForTarget("raw", v) && feq(v, 1.0f);             // straight to 127/127
    }

    // (ii) Damping=0.5 closed-form Lerp sequence: dampedPrev starts at 0 (TiXL does NOT seed to first
    //      event), [0,127]→[0,1]. Feed CC=127 twice. 1st: Lerp(1.0,0,0.5)=0.5. 2nd: Lerp(1.0,0.5,0.5)
    //      =0.75. Assert the 2nd output == lerpLinear(target, firstOutput, 0.5) exactly.
    {
      LiveBindingTable t;
      LiveBinding b; b.source = LiveSourceKind::Midi; b.midiKind = MidiMatchKind::ControllerChange;
      b.midiChannel = 1; b.midiControl = 7; b.target = "smoothed"; b.damping = 0.5f;
      t.addBinding(b);
      t.ingestMidi(MidiMatchKind::ControllerChange, 1, 7, 127);
      float first = -1.f; ok = ok && t.valueForTarget("smoothed", first) && feq(first, 0.5f);
      t.ingestMidi(MidiMatchKind::ControllerChange, 1, 7, 127);
      float second = -1.f; ok = ok && t.valueForTarget("smoothed", second);
      // bug injects the wrong damping arg-order (Lerp(prev,new) instead of Lerp(new,prev)) → 0.5 not 0.75.
      float expected = injectBug ? lerpLinear(first, 1.0f, 0.5f) : lerpLinear(1.0f, first, 0.5f);
      ok = ok && feq(second, expected);  // 0.75 (faithful) vs 0.5 (bug)
    }

    // (iii) DefaultOutputValue BEFORE any event (TiXL MidiInput.cs:201). A freshly-bound target reads
    //       its default, NOT "no value"; an unbound name is still absent.
    {
      LiveBindingTable t;
      LiveBinding b; b.source = LiveSourceKind::Midi; b.midiKind = MidiMatchKind::ControllerChange;
      b.midiChannel = 1; b.midiControl = 7; b.target = "withDefault"; b.defaultOutputValue = 0.42f;
      t.addBinding(b);
      float v = -1.f;
      // bug: expect the pre-event path to be absent (the old "no value" behavior) → mismatch RED.
      bool found = t.valueForTarget("withDefault", v);
      ok = ok && (found == !injectBug);
      if (!injectBug) ok = ok && feq(v, 0.42f);
      float dummy = 0.f;
      ok = ok && !t.valueForTarget("neverBound", dummy);  // truly unbound name stays absent
    }
  }

  // 2) Match-predicate assertions (TiXL MidiInput.cs:401-414 / OscInput.cs:241) — static, no transport.
  {
    LiveBinding mb; mb.source = LiveSourceKind::Midi; mb.midiKind = MidiMatchKind::ControllerChange;
    mb.midiChannel = 1; mb.midiControl = 7;
    ok = ok &&  LiveBindingTable::midiMatches(mb, MidiMatchKind::ControllerChange, 1, 7);   // exact
    ok = ok && !LiveBindingTable::midiMatches(mb, MidiMatchKind::ControllerChange, 2, 7);   // wrong channel
    ok = ok && !LiveBindingTable::midiMatches(mb, MidiMatchKind::ControllerChange, 1, 8);   // wrong control
    ok = ok && !LiveBindingTable::midiMatches(mb, MidiMatchKind::Note, 1, 7);               // wrong kind
    LiveBinding anyb; anyb.source = LiveSourceKind::Midi;  // all-any defaults
    ok = ok &&  LiveBindingTable::midiMatches(anyb, MidiMatchKind::Note, 9, 40);            // <0=any matches all

    LiveBinding ob; ob.source = LiveSourceKind::Osc; ob.oscAddress = "/fader";
    ok = ok &&  LiveBindingTable::oscMatches(ob, "/fader");        // exact
    ok = ok &&  LiveBindingTable::oscMatches(ob, "/fader/1");      // StartsWith prefix
    ok = ok && !LiveBindingTable::oscMatches(ob, "/knob");         // no match
  }

  // 3) End-to-end MIDI: register a CC binding (ch1 ctrl7 → "filterCutoff", [0,127]→[0,1]); drive the
  //    virtual CoreMIDI loopback; assert the target value remaps; a wrong-channel CC must NOT update it.
  {
    LiveBindingTable table;
    LiveBinding cc;
    cc.source = LiveSourceKind::Midi; cc.midiKind = MidiMatchKind::ControllerChange;
    cc.midiChannel = 1; cc.midiControl = 7; cc.target = "filterCutoff";
    cc.inMin = 0.f; cc.inMax = 127.f; cc.outMin = 0.f; cc.outMax = 1.f;
    table.addBinding(cc);

    BindingMidiCtx ctx; ctx.table = &table;
    MidiLoopbackDevice midi;
    if (midi.startListening(bindingMidiCb, &ctx)) {
      midi.sendRawMidi(0xB0, 7, 64);   // CC ch1 ctrl7 val64 → matches → remap 64/127
      midi.sendRawMidi(0xB5, 7, 100);  // CC ch6 ctrl7 val100 → WRONG channel → must NOT touch target
      bool arrived = waitForCount(ctx, 2, 1000);
      ok = ok && arrived;
      float v = -1.f;
      bool found = table.valueForTarget("filterCutoff", v);
      ok = ok && found;
      // bug injects a wrong expected value so the tooth bites; true value = 64/127 (the wrong-channel
      // CC100 must NOT have overwritten it to 100/127≈0.787).
      ok = ok && feq(v, injectBug ? 0.787f : 0.503937f);
      midi.stopListening();
    } else {
      std::printf("[selftest-io-live-binding] CoreMIDI unavailable -> SKIP MIDI transport\n");
      if (injectBug) return 1;
    }
  }

  // 4) End-to-end OSC: register an address binding ("/fader" → "opacity", identity [0,1]→[0,1]); drive
  //    the localhost UDP loopback; assert the value passes through (TiXL OscInput = raw); a non-matching
  //    address must NOT update the target.
  {
    LiveBindingTable table;
    LiveBinding fader;
    fader.source = LiveSourceKind::Osc; fader.oscAddress = "/fader"; fader.target = "opacity";
    fader.inMin = 0.f; fader.inMax = 1.f; fader.outMin = 0.f; fader.outMax = 1.f;  // identity
    table.addBinding(fader);

    BindingOscCtx ctx; ctx.table = &table;
    OscLoopbackDevice osc;
    if (osc.startListening(0 /*ephemeral*/, bindingOscCb, &ctx)) {
      osc.sendTestFloat("/fader", 0.75f);  // matches → identity → 0.75
      osc.sendTestFloat("/knob", 0.20f);   // WRONG address → must NOT touch "opacity"
      bool arrived = waitForCount(ctx, 2, 1000);
      ok = ok && arrived;
      float v = -1.f;
      bool found = table.valueForTarget("opacity", v);
      ok = ok && found;
      ok = ok && feq(v, 0.75f);  // not overwritten by the /knob message
      // negative: "opacity" must be the only updated target — /knob created no target.
      float dummy = 0.f;
      ok = ok && !table.valueForTarget("knobTarget", dummy);
    } else {
      std::printf("[selftest-io-live-binding] bind failed -> SKIP OSC transport (env restricted)\n");
      if (injectBug) return 1;
    }
  }

  std::printf("[selftest-io-live-binding] -> %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/312,
    {"io-live-binding",   runLiveBindingSelfTest});

}  // namespace sw
