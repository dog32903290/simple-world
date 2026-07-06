// selftests_net_nodes — io/tcp+udp+serial+dmx NODE cook golden (--selftest-net-nodes). deferred-hw-verify.
// (Top-level src/selftests*.cpp glob — self-registers, no CMake edit; drives the runtime cooks directly.)
//
// THE GATE (machine, loopback-injected — no real socket): the value a net arrival carries must reach a
// graph node's OUTPUT (extOut) through the REAL production cook (cookNetInputNodes / cookNetOutputNodes,
// the exact fns frame_cook drives every frame via cookIoDeviceNodes). We build a one-node composition,
// push a decoded net signal onto the runtime NetDeviceBus (the SAME sink the app's loopback callback
// appends to — the real-device half reuses this verbatim; only the arrival origin changes from loopback
// to hardware, so deferred-hw-verify for the physical device), cook, and read the node's extOut.
//   INPUT teeth  — a matching-KIND / matching-UNIVERSE arrival fires WasTrigger/Activity; a wrong one
//                  must NOT (injectBug mode 2 drops the DMX universe filter → wrong-universe leaks → RED).
//   OUTPUT teeth — TWO families (2026-07-06 拍板, 照 TiXL): EVENT senders (UDPOutput manual trigger)
//                  emit ONLY on the rising edge; REFRESH senders (Artnet/Sacn/DMX/WLED lighting) STREAM
//                  every frame the enable holds (ArtnetOutput.cs:123-128 sender thread; E1.31 silence =
//                  source loss). injectBug mode 1 SWAPS the semantics → both families' goldens redden.
//
// Ground truth mirrored (external/tixl, read-only): io/{udp/UDPInput,udp/UDPOutput,tcp/TcpClient,
//   tcp/TcpServer,serial/SerialInput,serial/SerialOutput,dmx/ArtnetInput,dmx/SacnInput,dmx/ArtnetOutput,
//   dmx/SacnOutput,dmx/DMXOutput}.cs (each cited at its assertion). The DMX packet BYTES are goldened
//   separately in selftests_io_dmx; this golden proves the value-RAIL cook (trigger/activity/echo).
#include <cstdio>
#include <map>
#include <string>

#include "runtime/graph.h"                // findSpec
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/net_node_cook.h"        // ingestNetSignal / cookNet*Nodes / setNetNodeBug / netDeviceBus
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS

namespace sw {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [net-nodes] FAIL %s\n", what); }
  else printf("  [net-nodes] ok   %s\n", what);
}

// Net-signal kind codes (must match net_node_cook.cpp's enum).
enum : int { kUdp = 0, kTcpClient = 1, kTcpServer = 2, kSerial = 3, kArtnet = 4, kSacn = 5 };

// Root compound "R" with one child of type `op` (id 1), given Float overrides.
SymbolLibrary makeLib(const char* op, const std::map<std::string, float>& floatOv) {
  SymbolLibrary lib;
  const NodeSpec* spec = findSpec(op);
  if (spec) lib.symbols[op] = atomicSymbolFromSpec(*spec);
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = op;
  for (const auto& kv : floatOv) c.overrides[kv.first] = kv.second;
  root.children = {c};
  root.nextChildId = 2;
  lib.symbols["R"] = root;
  lib.rootId = "R";
  return lib;
}

// ── UDPInput: a UDP arrival fires WasTrigger; no arrival leaves it 0 ──────────────────────────────────
bool goldenUdpInput(bool injectBug) {
  SymbolLibrary lib = makeLib("UDPInput", {{"Listen", 1.0f}, {"Port", 7000.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  const ResidentNode* n = g.node("1");

  // No arrival this frame → WasTrigger 0 (UDPInput.cs:99).
  endNetDeviceFrame();
  cookNetInputNodes(g);
  const float idle = n ? n->extOut[0] : -999.0f;
  endNetDeviceFrame();
  expect("UDPInput idle (no datagram) → WasTrigger 0", idle == 0.0f);

  // A UDP arrival → WasTrigger 1. injectBug is not the tell here (kind-match is unconditional); the
  // trigger edge is the fixed expectation, only the presence of the arrival moves it.
  ingestNetSignal(kUdp, 0);
  cookNetInputNodes(g);
  const float hit = n ? n->extOut[0] : -999.0f;
  endNetDeviceFrame();
  expect("UDPInput datagram arrived → WasTrigger 1", hit == 1.0f);
  (void)injectBug;
  return g_fail == 0;
}

// ── UDPInput kind rejection: a SERIAL arrival must NOT fire a UDPInput (wrong kind) ───────────────────
bool goldenUdpKindReject(bool injectBug) {
  SymbolLibrary lib = makeLib("UDPInput", {{"Listen", 1.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  const ResidentNode* n = g.node("1");

  // A serial-kind arrival only. Production: kind != kUdp → WasTrigger stays 0. (No bug seam on kind —
  // the kind switch is closed; this asserts the family isolation directly.)
  ingestNetSignal(kSerial, 0);
  cookNetInputNodes(g);
  const float v = n ? n->extOut[0] : -999.0f;
  endNetDeviceFrame();
  expect("UDPInput ignores a serial-kind arrival → WasTrigger 0", v == 0.0f);
  (void)injectBug;
  return g_fail == 0;
}

// ── ArtnetInput universe filter: an IN-range universe fires Activity; wrong-only universe does not ────
// injectBug mode 2 drops the universe match → the wrong-universe arrival leaks → Activity 1 → RED.
bool goldenArtnetInputUniverse(bool injectBug) {
  // StartUniverse 10, NumUniverses 2 → subscribes {10, 11}. Feed universe 20 (OUT of range).
  SymbolLibrary lib = makeLib("ArtnetInput",
      {{"Active", 1.0f}, {"StartUniverse", 10.0f}, {"NumUniverses", 2.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  const ResidentNode* n = g.node("1");

  ingestNetSignal(kArtnet, /*universe*/ 20);   // out of [10,12)
  setNetNodeBug(injectBug ? 2 : 0);
  cookNetInputNodes(g);
  setNetNodeBug(0);
  const float v = n ? n->extOut[0] : -999.0f;
  endNetDeviceFrame();
  // FIXED expect 0 (universe 20 ∉ subscribed). injectBug mode 2 drops the match → 20 accepted → 1 → RED.
  expect("ArtnetInput out-of-range universe rejected → Activity 0 (bug leaks 1 → RED)", v == 0.0f);

  // Sanity: an in-range universe (11) DOES fire (independent of the bug).
  ingestNetSignal(kArtnet, 11);
  cookNetInputNodes(g);
  const float hit = n ? n->extOut[0] : -999.0f;
  endNetDeviceFrame();
  expect("ArtnetInput in-range universe 11 → Activity 1", hit == 1.0f);
  return g_fail == 0;
}

// ── SacnInput universe filter (mirror of Artnet, distinct start) ──────────────────────────────────────
bool goldenSacnInputUniverse(bool injectBug) {
  SymbolLibrary lib = makeLib("SacnInput",
      {{"Active", 1.0f}, {"StartUniverse", 1.0f}, {"NumUniverses", 1.0f}});  // subscribes {1}
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  const ResidentNode* n = g.node("1");

  ingestNetSignal(kSacn, /*universe*/ 5);   // out of [1,2)
  setNetNodeBug(injectBug ? 2 : 0);
  cookNetInputNodes(g);
  setNetNodeBug(0);
  const float v = n ? n->extOut[0] : -999.0f;
  endNetDeviceFrame();
  expect("SacnInput out-of-range universe 5 rejected → Activity 0 (bug leaks 1 → RED)", v == 0.0f);
  return g_fail == 0;
}

// ── REFRESH family (Artnet/Sacn/DMX/WLED — lighting): held enable STREAMS every frame ─────────────────
// 2026-07-06 拍板 (照 TiXL): ArtnetOutput.cs:123-128 StartSenderThread streams continuously while
// SendTrigger is high; E1.31 receivers treat silence as source loss, and an edge-only marker could never
// animate lights while held. The old golden here pinned the OPPOSITE ("held 不重發") — killed.
// injectBug mode 1 SWAPS the gate semantics (refresh→edge) → frame 2 goes silent → RED.
bool goldenArtnetOutputStream(bool injectBug) {
  SymbolLibrary lib = makeLib("ArtnetOutput", {{"SendTrigger", 1.0f}});  // held true across both frames
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, NetOutputState> state;

  setNetNodeBug(injectBug ? 1 : 0);
  endNetDeviceFrame();
  cookNetOutputNodes(g, state);                    // frame 1: enabled → emit
  const size_t frame1 = netDeviceBus().out.size();
  endNetDeviceFrame();
  cookNetOutputNodes(g, state);                    // frame 2: still held → KEEPS streaming
  const size_t frame2 = netDeviceBus().out.size();
  endNetDeviceFrame();
  setNetNodeBug(0);

  // Gate closed → nothing emits (fresh graph/state, SendTrigger 0).
  SymbolLibrary offLib = makeLib("ArtnetOutput", {{"SendTrigger", 0.0f}});
  ResidentEvalGraph gOff = buildEvalGraph(offLib, offLib.rootId);
  initResidentCache(gOff);
  std::map<std::string, NetOutputState> stOff;
  endNetDeviceFrame();
  cookNetOutputNodes(gOff, stOff);
  const size_t offCount = netDeviceBus().out.size();
  endNetDeviceFrame();

  expect("ArtnetOutput emits while enabled (frame1 == 1)", frame1 == 1);
  expect("ArtnetOutput KEEPS streaming while held (frame2 == 1; bug edge-gates → RED)", frame2 == 1);
  expect("ArtnetOutput disabled → no emit", offCount == 0);
  return g_fail == 0;
}

// ── SacnOutput refresh stream (mirror) ────────────────────────────────────────────────────────────────
bool goldenSacnOutputStream(bool injectBug) {
  SymbolLibrary lib = makeLib("SacnOutput", {{"SendTrigger", 1.0f}, {"Priority", 100.0f}});
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, NetOutputState> state;

  setNetNodeBug(injectBug ? 1 : 0);
  endNetDeviceFrame();
  cookNetOutputNodes(g, state);
  const size_t frame1 = netDeviceBus().out.size();
  endNetDeviceFrame();
  cookNetOutputNodes(g, state);
  const size_t frame2 = netDeviceBus().out.size();
  endNetDeviceFrame();
  setNetNodeBug(0);
  expect("SacnOutput emits while enabled (frame1 == 1)", frame1 == 1);
  expect("SacnOutput KEEPS streaming while held (frame2 == 1; bug edge-gates → RED)", frame2 == 1);
  return g_fail == 0;
}

// ── EVENT family counter-golden (UDPOutput manual SendTrigger): rising edge ONLY ─────────────────────
// Discrete message senders must NOT machine-gun while the trigger is held. injectBug mode 1 SWAPS the
// gate (event→level) → frame 2 re-emits → RED. This is the other half of the swap tooth.
bool goldenUdpOutputEdge(bool injectBug) {
  SymbolLibrary lib = makeLib("UDPOutput", {{"SendTrigger", 1.0f}});  // held true across both frames
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  std::map<std::string, NetOutputState> state;

  setNetNodeBug(injectBug ? 1 : 0);
  endNetDeviceFrame();
  cookNetOutputNodes(g, state);                    // frame 1: rising edge → emit
  const size_t frame1 = netDeviceBus().out.size();
  endNetDeviceFrame();
  cookNetOutputNodes(g, state);                    // frame 2: held, no new edge → NO emit
  const size_t frame2 = netDeviceBus().out.size();
  endNetDeviceFrame();
  setNetNodeBug(0);
  expect("UDPOutput emits on the rising edge (frame1 == 1)", frame1 == 1);
  expect("UDPOutput does NOT re-emit while held (frame2 == 0; bug level-fires → RED)", frame2 == 0);
  return g_fail == 0;
}

// ── DMXOutput send: Connect held → send EVERY frame (continuous serial DMX — refresh family) ─────────
// The old leg only sampled the FIRST frame, so its own header claim ("streams every frame") was never
// asserted — the 2026-07-06 audit caught the code edge-gating DMX while this comment said otherwise.
// Now: Connect held across 2 frames → both emit; injectBug mode 1 (swap → edge) silences frame 2 → RED.
bool goldenDmxOutputConnect(bool injectBug) {
  // Connect false → no emit (the gate rail, bug-invariant).
  SymbolLibrary offLib = makeLib("DMXOutput", {{"Connect", 0.0f}});
  ResidentEvalGraph gOff = buildEvalGraph(offLib, offLib.rootId);
  initResidentCache(gOff);
  std::map<std::string, NetOutputState> st0;
  endNetDeviceFrame();
  cookNetOutputNodes(gOff, st0);
  const size_t offCount = netDeviceBus().out.size();
  endNetDeviceFrame();
  expect("DMXOutput NOT connected → no emit (Connect gate)", offCount == 0);

  // Connect held → streams both frames (refresh family).
  SymbolLibrary onLib = makeLib("DMXOutput", {{"Connect", 1.0f}});
  ResidentEvalGraph gOn = buildEvalGraph(onLib, onLib.rootId);
  initResidentCache(gOn);
  std::map<std::string, NetOutputState> st1;
  setNetNodeBug(injectBug ? 1 : 0);
  endNetDeviceFrame();
  cookNetOutputNodes(gOn, st1);
  const size_t frame1 = netDeviceBus().out.size();
  endNetDeviceFrame();
  cookNetOutputNodes(gOn, st1);
  const size_t frame2 = netDeviceBus().out.size();
  endNetDeviceFrame();
  setNetNodeBug(0);
  expect("DMXOutput connected → emits a frame", frame1 == 1);
  expect("DMXOutput KEEPS streaming while connected (frame2 == 1; bug edge-gates → RED)", frame2 == 1);
  return g_fail == 0;
}

}  // namespace

int runNetNodeSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] net-nodes (socket/DMX device nodes: trigger/activity/send-edge; deferred-hw-verify)\n");
  goldenUdpInput(injectBug);
  goldenUdpKindReject(injectBug);
  goldenArtnetInputUniverse(injectBug);
  goldenSacnInputUniverse(injectBug);
  goldenArtnetOutputStream(injectBug);
  goldenSacnOutputStream(injectBug);
  goldenUdpOutputEdge(injectBug);
  goldenDmxOutputConnect(injectBug);
  printf("[selftest] net-nodes %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

REGISTER_SELFTESTS(/*orderBase=*/718, {"net-nodes", runNetNodeSelfTest});

}  // namespace sw
