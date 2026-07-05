// runtime/net_node_cook — per-frame cook for the io/tcp + io/udp + io/serial + io/dmx operator NODES
// (the socket & DMX-over-network device nodes registered in node_registry_math_net_io.cpp). The io-family
// sibling of io_node_cook.cpp (midi/osc): these nodes have NO pure evaluate() — their input value comes
// from a per-frame net device bus, their output effect is a send side-effect + per-instance memory
// (the send trigger-edge latch, the last-message-changed cache) — so frame_cook cooks them once per
// frame and writes the value rail onto ResidentNode::extOut; evalResidentFloat returns extOut[idx] via
// the generic no-evaluate path, exactly like MidiInput/OscInput/AudioReaction.
//
// TiXL authority (per-node file:line cited at each cook fn in net_node_cook.cpp).
//
// The DMX packet ASSEMBLY value is goldened byte-推 in platform/dmx_packet (selftests_io_dmx); these
// cooks are the value-RAIL side: input nodes surface WasTrigger/Activity when a matching net signal
// arrived this frame; output nodes surface an echo (packets-sent / IsConnected) when the send condition
// fires. The String/List message currency is deferred (fork-net-message-currency-deferred).
//
// runtime leaf: pure computation (reads the runtime net bus), no sockets / no UI / no upward dep. The
// app owns the real transport (net_loopback) and PUSHES decoded signals into the net bus; the golden
// pushes them directly (no transport) — same leaf-seam inversion as io_device_bus.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sw {

struct ResidentEvalGraph;   // resident_eval_graph.h

// ── The per-frame NET device bus (mirror of IoDeviceBus for the socket/DMX family) ─────────────────

// One decoded inbound net signal (a UDP/TCP/serial message arrival, or an Art-Net/sACN universe frame).
// `kind` selects which input-node family it feeds: 0=UDP, 1=TCP-client, 2=TCP-server, 3=Serial,
// 4=Artnet, 5=Sacn. `universe` is set for the DMX kinds (Artnet/Sacn); it is ignored for the byte-stream
// kinds. The app's loopback callback appends one per arrival; the input cooks drain them per frame.
struct NetBusSignal {
  int kind = 0;
  int universe = 0;   // DMX kinds only (Artnet/Sacn); 0 for byte-stream kinds.
};

// One outbound net packet an output node EMITTED this frame (the wire bytes from platform/dmx_packet or
// the UTF-8 message). The app forwards it to the real socket (deferred-hw-verify); the golden asserts
// the byte count / echo. `kind` matches NetBusSignal.kind for symmetry.
struct NetOutPacket {
  int kind = 0;
  int universe = 0;
  std::vector<uint8_t> bytes;
};

struct NetDeviceBus {
  std::vector<NetBusSignal> in;    // INPUT: this frame's decoded net arrivals (all kinds)
  std::vector<NetOutPacket> out;   // OUTPUT: packets the output cooks emitted this frame
};

NetDeviceBus& netDeviceBus();

// App-side ingest hook (called from the loopback callbacks the app installs; the golden calls it
// directly). Thin append — the runtime never calls this, the app/golden does (leaf-seam: single writer).
void ingestNetSignal(int kind, int universe);

// Output-side emit helper (called by the output cooks; the app drains `out` each frame to the real
// senders, then the frame clear wipes it).
void emitNetPacket(int kind, int universe, const std::vector<uint8_t>& bytes);

// Clear both the input arrivals and the output packets — called once per frame AFTER the input cooks
// read + the app drained the output packets.
void endNetDeviceFrame();

// ── Per-instance state + cooks ─────────────────────────────────────────────────────────────────────

// Per-instance memory for a send-side output node — the trigger-edge latch (TiXL private `_wasSending`
// / manual SendTrigger rising edge). SendContinuously fires every frame the enable is held;
// SendWhenTriggered / manual-trigger fires only on the rising edge.
struct NetOutputState {
  bool triggered = false;   // previous frame's trigger/enable value (for rising-edge detection)
};

// Cook every socket/DMX INPUT node once this frame: for each net bus signal whose kind (and universe,
// for DMX) matches the node's family/config, set WasTrigger/Activity → extOut[0] (and IsConnected →
// extOut[1] for the two-output socket inputs). Per-node state is not needed (edge is per-frame).
void cookNetInputNodes(ResidentEvalGraph& g);

// Cook every socket/DMX OUTPUT node once this frame: on the send condition (SendContinuously = every
// frame; SendTrigger rising edge otherwise), build the wire packet (via platform/dmx_packet for the DMX
// senders) into the net out bus and echo a probe onto extOut[0]. Per-node state keyed by resident path.
void cookNetOutputNodes(ResidentEvalGraph& g, std::map<std::string, NetOutputState>& state);

// The single frame_cook entry point: cook the net input + output families, then clear the net bus.
// Owns the per-instance state map (function-local static keyed by resident path). The golden drives
// cookNetInputNodes / cookNetOutputNodes directly (its own state map), not this wrapper.
void cookNetDeviceNodes(ResidentEvalGraph& g);

// ── Golden TEETH hook (--selftest-net-nodes). 0 = production; 1 = DROP the send-edge gate (an output's
// SendWhenTriggered fires every frame the trigger is held → the edge-gated golden goes RED); 2 = DROP
// the DMX universe match on an input (accepts ANY universe → the wrong-universe-rejected assertion goes
// RED). Sticky module switch; the golden restores 0. Mirrors setIoNodeBug's convention.
void setNetNodeBug(int mode);
int  netNodeBug();

}  // namespace sw
