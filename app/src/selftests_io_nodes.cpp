// selftests_io_nodes — io/midi + io/osc NODE cook golden (--selftest-io-nodes). deferred-hw-verify.
// (Lives in the top-level src/selftests*.cpp glob — self-registers its row, no CMake edit; it drives
// the runtime cook fns directly so it needs no app-layer deps.)
//
// THE GATE (machine, loopback-injected — no physical controller): the value a MIDI CC / OSC message
// carries must reach a graph node's OUTPUT (extOut) through the REAL production cook
// (cookMidiInputNodes / cookOscInputNodes, the exact fns frame_cook::run drives every frame). We build
// a one-node composition, push a decoded event onto the runtime IoDeviceBus (the SAME sink the app's
// loopback callback appends to — the real-device half reuses this verbatim; only the event origin
// changes from the loopback to hardware, so this golden is marked deferred-hw-verify for the physical
// controller leg), cook, and read the node's extOut. injectBug走真 decode/map seam: mode 1 drops the
// channel filter (a wrong-channel CC then leaks in → the rejection assertion goes RED), mode 2 drops
// the Remap (Result becomes the raw controller value → the closed-form Remap assertion goes RED).
//
// Ground truth mirrored (external/tixl, read-only):
//   MidiInput.cs:208      Result = Remap(controllerValue, 0,127, OutputRange.X, OutputRange.Y).
//   MidiInput.cs:401-414  match predicate: trained channel/controller/eventType (<0 = any).
//   MidiInput.cs:199-206  _isDefaultValue path: DefaultOutputValue before the first matching event.
//   OscInput.cs:241       address match = msg.Address.StartsWith(_address); WasTrigger on a match.
#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/graph.h"                // findSpec (MidiInput/OscInput NodeSpec)
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/io_device_bus.h"        // ingestMidiSignal / ingestOscArg / endIoDeviceFrame
#include "runtime/io_node_cook.h"         // cookMidiInputNodes / cookOscInputNodes / setIoNodeBug
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS

namespace sw {
namespace {

constexpr float kTol = 1e-4f;
bool feq(float a, float b) { return std::fabs(a - b) < kTol; }

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [io-nodes] FAIL %s\n", what); }
  else printf("  [io-nodes] ok   %s\n", what);
}

// Root compound "R" with one child of type `op` (id 1), given Float + String overrides.
SymbolLibrary makeLib(const char* op,
                      const std::map<std::string, float>& floatOv,
                      const std::map<std::string, std::string>& strOv = {}) {
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

// ── MidiInput: CC 64 over ch1 ctrl48 → Remap(64,0..127,[0,1]) = 0.503937, damping OFF ────────────────
bool goldenMidiInput(bool injectBug) {
  // Damping=0 so the closed-form Remap lands directly on Result (no cross-frame smoothing to unwind).
  SymbolLibrary lib = makeLib("MidiInput", {
      {"OutputRange.x", 0.0f}, {"OutputRange.y", 1.0f},
      {"DefaultOutputValue", 0.0f}, {"Damping", 0.0f},
      {"Channel", 1.0f}, {"Control", 48.0f}, {"EventType", 2.0f /*ControllerChanges*/}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, MidiInputState> state;

  // Before any event: _isDefaultValue → Result = DefaultOutputValue (0).
  endIoDeviceFrame();                 // ensure the bus is empty
  cookMidiInputNodes(g, state);
  const ResidentNode* n = g.node("1");
  const float before = n ? n->extOut[0] : -999.0f;
  expect("default output before any event == 0", feq(before, 0.0f));

  // A matching CC (bus kind 1 = ControllerChange, ch1, ctrl48, val64). injectBug mode 1 would also let
  // a WRONG-channel CC through — so we feed a wrong-channel CC FIRST; in production it must be ignored,
  // under the bug it captures ch6/val127 and Result flips to 127/127 = 1.0.
  ingestMidiSignal(/*kind ControllerChange*/ 1, /*channel*/ 6, /*controllerId*/ 48, /*value*/ 127);
  ingestMidiSignal(/*kind ControllerChange*/ 1, /*channel*/ 1, /*controllerId*/ 48, /*value*/ 64);
  setIoNodeBug(injectBug ? 1 : 0);
  cookMidiInputNodes(g, state);
  setIoNodeBug(0);
  endIoDeviceFrame();
  const float after = n ? n->extOut[0] : -999.0f;
  // 64/127 = 0.5039370; under the filter-drop bug the LAST signal processed is ch1/64 too, but the
  // wrong-channel ch6/127 is also accepted (processed first), leaving currentControllerValue=64 → same
  // 0.5039 — so mode 1 alone wouldn't split here. Assert the REJECTION directly instead: a wrong-only
  // channel must NOT move Result. (mode-2 remap-drop is covered by goldenMidiRemap below.)
  const float want = 0.503937f;
  expect("CC 64/127 over [0,1] == 0.503937 (Remap closed form)", feq(after, want));
  return g_fail == 0;
}

// ── MidiInput filter rejection: a wrong-channel-ONLY CC must leave Result at default ─────────────────
bool goldenMidiReject(bool injectBug) {
  SymbolLibrary lib = makeLib("MidiInput", {
      {"OutputRange.x", 0.0f}, {"OutputRange.y", 1.0f}, {"Damping", 0.0f},
      {"Channel", 1.0f}, {"Control", 48.0f}, {"EventType", 2.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, MidiInputState> state;
  const ResidentNode* n = g.node("1");

  // ONLY a wrong-channel CC arrives. Production: rejected → _isDefaultValue stays true → Result=default 0.
  // injectBug mode 1 CORRUPTS the real cook (midiSignalMatches drops the filter) → ch6/127 accepted →
  // Result = 127/127 = 1.0, DIVERGING from the FIXED expected 0 → RED (no want-flip: the expectation is
  // bug-independent, only the cooked value moves under the bug — GOLDEN_STANDARD P3-safe polarity).
  ingestMidiSignal(1, /*channel*/ 6, 48, 127);
  setIoNodeBug(injectBug ? 1 : 0);
  cookMidiInputNodes(g, state);
  setIoNodeBug(0);
  endIoDeviceFrame();
  const float after = n ? n->extOut[0] : -999.0f;
  expect("wrong-channel CC rejected → Result stays default 0 (bug leaks 1.0 → RED)", feq(after, 0.0f));
  return g_fail == 0;
}

// ── MidiInput remap seam: probe a MID value with a NON-identity range ────────────────────────────────
// OutputRange [10,20], CC 64 → Remap(64,0..127,10,20) = 10 + 64/127*10 = 15.039370. Non-identity range +
// mid controller value = P2-safe (не端点). injectBug mode 2 drops the remap → Result = 64 → RED.
bool goldenMidiRemap(bool injectBug) {
  SymbolLibrary lib = makeLib("MidiInput", {
      {"OutputRange.x", 10.0f}, {"OutputRange.y", 20.0f}, {"Damping", 0.0f},
      {"Channel", 1.0f}, {"Control", 48.0f}, {"EventType", 2.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, MidiInputState> state;
  const ResidentNode* n = g.node("1");

  ingestMidiSignal(1, 1, 48, 64);
  setIoNodeBug(injectBug ? 2 : 0);
  cookMidiInputNodes(g, state);
  setIoNodeBug(0);
  endIoDeviceFrame();
  const float after = n ? n->extOut[0] : -999.0f;
  // FIXED expectation = the TiXL Remap closed form. injectBug mode 2 CORRUPTS the cook (drops the
  // Remap → Result = raw 64), DIVERGING from 15.03937 → RED. Bug-independent expectation (P3-safe).
  expect("CC 64 over [10,20] == 15.03937 (bug drops Remap → raw 64 → RED)", feq(after, 15.039370f));
  return g_fail == 0;
}

// ── OscInput WasTrigger: a matching-address message fires the trigger; wrong prefix does not ──────────
bool goldenOscTrigger(bool injectBug) {
  SymbolLibrary lib = makeLib("OscInput", {}, {{"Address", "/fader"}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, OscInputState> state;
  const ResidentNode* n = g.node("1");

  // Wrong-prefix message only. Production: no StartsWith match → WasTrigger 0. injectBug mode 1
  // CORRUPTS the cook (drops the address filter) → WasTrigger 1, DIVERGING from the FIXED 0 → RED.
  ingestOscArg("/knob", 0.5f, 0);
  setIoNodeBug(injectBug ? 1 : 0);
  cookOscInputNodes(g, state);
  setIoNodeBug(0);
  endIoDeviceFrame();
  const float wrong = n ? n->extOut[0] : -999.0f;
  expect("wrong-prefix OSC does not trigger → WasTrigger 0 (bug leaks 1 → RED)", feq(wrong, 0.0f));

  // Matching prefix "/fader/x" StartsWith "/fader" → WasTrigger 1 (independent of the bug).
  ingestOscArg("/fader/x", 0.75f, 0);
  cookOscInputNodes(g, state);
  endIoDeviceFrame();
  const float hit = n ? n->extOut[0] : -999.0f;
  expect("matching-prefix OSC fires WasTrigger == 1", feq(hit, 1.0f));
  return g_fail == 0;
}

// ── MidiControlOutput: SendContinuously emits the exact CC short message; edge-gating under Triggered ─
bool goldenMidiControlOutput(bool injectBug) {
  // SendContinuously (0), CC (0), channel 3, controller 74, value 100 → CC short message
  // status = 0xB0 | (3-1) = 0xB2, data1 = 74, data2 = 100.
  SymbolLibrary lib = makeLib("MidiControlOutput", {
      {"SendMode", 0.0f}, {"TriggerSend", 0.0f}, {"CCorPressure", 0.0f},
      {"ChannelNumber", 3.0f}, {"ControllerNumber", 74.0f}, {"Value", 100.0f}, {"UseValueFloat", 0.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, MidiOutputState> state;

  endIoDeviceFrame();
  cookMidiControlOutputNodes(g, state);
  const IoDeviceBus& bus = ioDeviceBus();
  bool okCount = bus.midiOut.size() == 1;
  int status = okCount ? bus.midiOut[0].status : -1;
  int data1  = okCount ? bus.midiOut[0].data1  : -1;
  int data2  = okCount ? bus.midiOut[0].data2  : -1;
  endIoDeviceFrame();
  // FIXED expectation = the TiXL short message. (No bug affects the SendContinuously CC bytes; this is
  // the path-independent production probe. The teeth live in the edge-gate + value-scale goldens below.)
  expect("SendContinuously emits CC status 0xB2", okCount && status == 0xB2);
  expect("SendContinuously CC controller == 74", data1 == 74);
  expect("SendContinuously CC value == 100", data2 == 100);

  // Edge-gate tooth: SendWhenTriggered (1) with TriggerSend HELD true across two cooks must emit ONLY on
  // the rising edge (first cook), NOT the second. injectBug mode 1 drops the edge → emits BOTH → the
  // second-frame count diverges from the FIXED 0 → RED.
  SymbolLibrary lib2 = makeLib("MidiControlOutput", {
      {"SendMode", 1.0f /*SendWhenTriggered*/}, {"TriggerSend", 1.0f}, {"CCorPressure", 0.0f},
      {"ChannelNumber", 1.0f}, {"ControllerNumber", 10.0f}, {"Value", 64.0f}, {"UseValueFloat", 0.0f}});
  ResidentEvalGraph g2 = buildEvalGraph(lib2, lib2.rootId);
  initResidentCache(g2);
  std::map<std::string, MidiOutputState> state2;
  setIoNodeBug(injectBug ? 1 : 0);
  endIoDeviceFrame();
  cookMidiControlOutputNodes(g2, state2);      // frame 1: rising edge → emits
  size_t frame1 = ioDeviceBus().midiOut.size();
  endIoDeviceFrame();
  cookMidiControlOutputNodes(g2, state2);      // frame 2: trigger still held, no new edge → NO emit
  size_t frame2 = ioDeviceBus().midiOut.size();
  endIoDeviceFrame();
  setIoNodeBug(0);
  expect("SendWhenTriggered emits on the rising edge (frame1 == 1)", frame1 == 1);
  expect("SendWhenTriggered does NOT re-emit while held (frame2 == 0; bug re-emits → RED)", frame2 == 0);
  return g_fail == 0;
}

// ── MidiControlOutput value scale tooth: UseValueFloat scales 0..1 → 0..127 ───────────────────────────
bool goldenMidiOutputValueScale(bool injectBug) {
  // UseValueFloat, ValueFloat 0.5 → (int)(0.5*127) = 63 (TiXL cs:46). injectBug mode 2 drops the
  // clamp+scale → (int)0.5 = 0, diverging from the FIXED 63 → RED.
  SymbolLibrary lib = makeLib("MidiControlOutput", {
      {"SendMode", 0.0f}, {"CCorPressure", 0.0f}, {"ChannelNumber", 1.0f},
      {"ControllerNumber", 20.0f}, {"UseValueFloat", 1.0f}, {"ValueFloat", 0.5f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, MidiOutputState> state;
  endIoDeviceFrame();
  setIoNodeBug(injectBug ? 2 : 0);
  cookMidiControlOutputNodes(g, state);
  setIoNodeBug(0);
  int data2 = ioDeviceBus().midiOut.size() == 1 ? ioDeviceBus().midiOut[0].data2 : -1;
  endIoDeviceFrame();
  expect("ValueFloat 0.5 scales to CC value 63 (bug drops scale → 0 → RED)", data2 == 63);
  return g_fail == 0;
}

// Helper: cook one output node once (freshly built graph) and return its first emitted MIDI message.
// clk drives the duration modes. Returns {status,data1,data2,count}. Bug is set around the cook.
struct EmitProbe { int status = -1, data1 = -1, data2 = -1; size_t count = 0; };
template <typename CookFn>
EmitProbe cookOnceMidi(const char* op, const std::map<std::string, float>& fo,
                       const std::map<std::string, std::string>& so, CookFn cook, int bug) {
  SymbolLibrary lib = makeLib(op, fo, so);
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  endIoDeviceFrame();
  setIoNodeBug(bug);
  cook(g);
  setIoNodeBug(0);
  const IoDeviceBus& bus = ioDeviceBus();
  EmitProbe p; p.count = bus.midiOut.size();
  if (p.count) { p.status = bus.midiOut[0].status; p.data1 = bus.midiOut[0].data1; p.data2 = bus.midiOut[0].data2; }
  endIoDeviceFrame();
  return p;
}

// ── MidiOutput ControllerChange (mode 2): 0xB0|(ch-1), NoteOrController, velocity*127 ────────────────
bool goldenMidiOutput(bool /*injectBug*/) {
  std::map<std::string, MidiNoteOutputState> st; IoOutClock clk;
  // SendMode=2 ControllerChange, ch2, ctrl 30, Velocity 1.0 → 127. status 0xB1, data1 30, data2 127.
  EmitProbe p = cookOnceMidi("MidiOutput",
      {{"SendMode", 2.0f}, {"ChannelNumber", 2.0f}, {"NoteOrController", 30.0f}, {"Velocity", 1.0f}, {"Velocity127", -1.0f}},
      {}, [&](ResidentEvalGraph& g) { cookMidiOutputNodes(g, st, clk); }, 0);
  expect("MidiOutput CC → 0xB1/30/127", p.count == 1 && p.status == 0xB1 && p.data1 == 30 && p.data2 == 127);
  return g_fail == 0;
}

// ── MidiNoteOutput Note_WhileTriggered: edge-on NoteOn, edge-off NoteOff (vel 0) ─────────────────────
bool goldenMidiNoteOutput(bool injectBug) {
  SymbolLibrary lib = makeLib("MidiNoteOutput",
      {{"SendMode", 1.0f}, {"ChannelNumber", 1.0f}, {"NoteNumber", 60.0f}, {"Velocity", 100.0f},
       {"UseVelocityFloat", 0.0f}, {"TriggerSend", 1.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId); initResidentCache(g);
  std::map<std::string, MidiNoteOutputState> st; IoOutClock clk;
  setIoNodeBug(0);
  endIoDeviceFrame(); cookMidiNoteOutputNodes(g, st, clk);   // frame1: rising → NoteOn 0x90/60/100
  EmitProbe p1; p1.count = ioDeviceBus().midiOut.size();
  if (p1.count) { p1.status = ioDeviceBus().midiOut[0].status; p1.data1 = ioDeviceBus().midiOut[0].data1; p1.data2 = ioDeviceBus().midiOut[0].data2; }
  endIoDeviceFrame();
  expect("MidiNoteOutput NoteOn on edge → 0x90/60/100", p1.count == 1 && p1.status == 0x90 && p1.data1 == 60 && p1.data2 == 100);
  // ★ HONEST NO-TOOTH (2026-07-06 audit): this golden has NO injectBug wiring — the old comment here
  // described a "fresh graph held-across-two-frames" bite that was never written, and the NoteOff leg
  // (release → 0x80/vel0, MidiNoteOutput.cs:93-98) is untested. Debt: add a release-edge leg + a
  // level-fire tooth. Until then the -bug variant exercises nothing on this golden.
  (void)injectBug;
  return g_fail == 0;
}

// ── MidiPitchbendOutput: PitchFloat 0.5 → 14-bit 12288 → LSB/MSB. bug1 level-fire, bug2 no clamp ─────
bool goldenMidiPitchbend(bool injectBug) {
  std::map<std::string, MidiOutputState> st;
  // PitchFloat 0.5 → (int)(0.5*8192)=4096 +8192 = 12288. LSB=12288&0x7F=0, MSB=(12288>>7)&0x7F=96.
  EmitProbe p = cookOnceMidi("MidiPitchbendOutput",
      {{"SendMode", 0.0f}, {"ChannelNumber", 1.0f}, {"UsePitchFloat", 1.0f}, {"PitchFloat", 0.5f}},
      {}, [&](ResidentEvalGraph& g) { cookMidiPitchbendOutputNodes(g, st); }, 0);
  expect("Pitchbend 0.5 → 0xE0 LSB 0 MSB 96 (14-bit 12288)", p.count == 1 && p.status == 0xE0 && p.data1 == 0 && p.data2 == 96);
  // bug2 drops the *8192 scale → (int)0.5=0 +8192 = 8192 (center) → MSB 64, diverges from 96 → RED.
  std::map<std::string, MidiOutputState> st2;
  EmitProbe pb = cookOnceMidi("MidiPitchbendOutput",
      {{"SendMode", 0.0f}, {"ChannelNumber", 1.0f}, {"UsePitchFloat", 1.0f}, {"PitchFloat", 0.5f}},
      {}, [&](ResidentEvalGraph& g) { cookMidiPitchbendOutputNodes(g, st2); }, injectBug ? 2 : 0);
  expect("Pitchbend MSB == 96 (bug drops scale → center 64 → RED)", pb.data2 == 96);
  return g_fail == 0;
}

// ── MidiTriggerOutput: ProgramChange rising edge → 0xC0|(ch-1), program ──────────────────────────────
bool goldenMidiTriggerOutput(bool injectBug) {
  SymbolLibrary lib = makeLib("MidiTriggerOutput",
      {{"ChannelNumber", 1.0f}, {"TriggerProgramChange", 1.0f}, {"ProgramChangeNumber", 42.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId); initResidentCache(g);
  std::map<std::string, MidiTriggerOutputState> st; IoOutClock clk;
  setIoNodeBug(injectBug ? 1 : 0);
  endIoDeviceFrame(); cookMidiTriggerOutputNodes(g, st, clk);  // frame1: rising → PC 0xC0/42
  size_t f1 = ioDeviceBus().midiOut.size(); int s1 = f1 ? ioDeviceBus().midiOut[0].status : -1; int d1 = f1 ? ioDeviceBus().midiOut[0].data1 : -1;
  endIoDeviceFrame(); cookMidiTriggerOutputNodes(g, st, clk);  // frame2: still held, no edge → no emit
  size_t f2 = ioDeviceBus().midiOut.size();
  endIoDeviceFrame(); setIoNodeBug(0);
  expect("MidiTriggerOutput PC rising → 0xC0/42", f1 == 1 && s1 == 0xC0 && d1 == 42);
  expect("MidiTriggerOutput no re-fire while held (bug level-fires → RED)", f2 == 0);
  return g_fail == 0;
}

// ── MidiSysexOutput: parse "F0 43 12 F7" hex → 4 SysEx bytes on the rising edge ──────────────────────
// ★ NAMED FORK (fork-midisysex-edge-gate-not-toggle, 2026-07-06 audit): TiXL MidiSysexOutput.cs:44-52
// gates the send on `_initialized = !_initialized` — an ALTERNATING toggle (held trigger: frame1 silent,
// frame2 sends, then every other frame), almost certainly an upstream bug. sw deliberately uses the
// sanitized rising-edge gate the sibling Midi*Output ops use (send once on the rise, silent while held).
// The hex parse / F0 framing below carries the TiXL anchor; the CADENCE is a declared sw divergence.
bool goldenMidiSysexOutput(bool injectBug) {
  SymbolLibrary lib = makeLib("MidiSysexOutput", {{"TriggerSend", 1.0f}}, {{"SysexString", "F0 43 12 F7"}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId); initResidentCache(g);
  std::map<std::string, MidiOutputState> st;
  setIoNodeBug(injectBug ? 1 : 0);
  endIoDeviceFrame(); cookMidiSysexOutputNodes(g, st);   // frame1: rising → parse+emit
  size_t f1 = ioDeviceBus().sysexOut.size(); size_t nb = f1 ? ioDeviceBus().sysexOut[0].size() : 0;
  int b0 = (f1 && nb) ? ioDeviceBus().sysexOut[0][0] : -1;
  endIoDeviceFrame(); cookMidiSysexOutputNodes(g, st);   // frame2: still held, no edge → no emit
  size_t f2 = ioDeviceBus().sysexOut.size();
  endIoDeviceFrame(); setIoNodeBug(0);
  expect("MidiSysexOutput parses 'F0 43 12 F7' → 4 bytes, first 0xF0", f1 == 1 && nb == 4 && b0 == 0xF0);
  expect("MidiSysexOutput no re-fire while held (bug level-fires → RED)", f2 == 0);
  return g_fail == 0;
}

// ── OscOutput (PARTIAL): single-value send on trigger → OscMessage(address, [value]) ─────────────────
bool goldenOscOutput(bool /*injectBug*/) {
  SymbolLibrary lib = makeLib("OscOutput", {{"Value", 0.42f}, {"SendTrigger", 1.0f}, {"OnlySendChanges", 0.0f}},
                             {{"Address", "/vol"}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId); initResidentCache(g);
  std::map<std::string, MidiOutputState> st;
  endIoDeviceFrame(); cookOscOutputNodes(g, st);
  const IoDeviceBus& bus = ioDeviceBus();
  bool ok = bus.oscOut.size() == 1 && bus.oscOut[0].address == "/vol" && bus.oscOut[0].args.size() == 1
            && feq(bus.oscOut[0].args[0], 0.42f);
  endIoDeviceFrame();
  expect("OscOutput sends OscMessage(/vol,[0.42]) on trigger (single-value PARTIAL)", ok);
  return g_fail == 0;
}

}  // namespace

int runIoNodeSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] io-nodes (MidiInput/OscInput + MIDI/OSC output family; deferred-hw-verify)\n");
  goldenMidiInput(injectBug);
  goldenMidiReject(injectBug);
  goldenMidiRemap(injectBug);
  goldenOscTrigger(injectBug);
  goldenMidiControlOutput(injectBug);
  goldenMidiOutputValueScale(injectBug);
  goldenMidiOutput(injectBug);
  goldenMidiNoteOutput(injectBug);
  goldenMidiPitchbend(injectBug);
  goldenMidiTriggerOutput(injectBug);
  goldenMidiSysexOutput(injectBug);
  goldenOscOutput(injectBug);
  printf("[selftest] io-nodes %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

REGISTER_SELFTESTS(/*orderBase=*/316, {"io-nodes", runIoNodeSelfTest});

}  // namespace sw
