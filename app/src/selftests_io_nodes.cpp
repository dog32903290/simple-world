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

}  // namespace

int runIoNodeSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] io-nodes (MidiInput/OscInput device-node cook; deferred-hw-verify for physical MIDI)\n");
  goldenMidiInput(injectBug);
  goldenMidiReject(injectBug);
  goldenMidiRemap(injectBug);
  goldenOscTrigger(injectBug);
  printf("[selftest] io-nodes %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

REGISTER_SELFTESTS(/*orderBase=*/316, {"io-nodes", runIoNodeSelfTest});

}  // namespace sw
