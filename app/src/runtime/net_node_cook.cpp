// runtime/net_node_cook — per-frame cook for the socket/DMX operator nodes. See net_node_cook.h.
//
// TiXL authority: Operators/Lib/io/{tcp,udp,serial,dmx}/*.cs (read-only). The DMX packet ASSEMBLY is
// goldened byte-推 in platform/dmx_packet; these cooks are the value-RAIL echo + the send-edge gate.
#include "runtime/net_node_cook.h"

#include <cmath>

#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / ResidentNode / resolveResidentFloatInputs

namespace sw {

namespace {
int g_netNodeBug = 0;

// Net-signal kind codes (must match net_node_cook.h NetBusSignal.kind / the input-node families).
enum : int { kUdp = 0, kTcpClient = 1, kTcpServer = 2, kSerial = 3, kArtnet = 4, kSacn = 5 };

// Does a DMX input node accept this universe? Artnet/Sacn inputs subscribe [StartUniverse,
// StartUniverse+NumUniverses). TEETH mode 2 drops the universe match (accept any).
bool universeInRange(int universe, int start, int num) {
  if (g_netNodeBug == 2) return true;
  return universe >= start && universe < start + num;
}
}  // namespace

NetDeviceBus& netDeviceBus() {
  static NetDeviceBus bus;
  return bus;
}
void ingestNetSignal(int kind, int universe) { netDeviceBus().in.push_back({kind, universe}); }
void emitNetPacket(int kind, int universe, const std::vector<uint8_t>& bytes) {
  netDeviceBus().out.push_back({kind, universe, bytes});
}
void endNetDeviceFrame() {
  NetDeviceBus& bus = netDeviceBus();
  bus.in.clear();
  bus.out.clear();
}
void setNetNodeBug(int mode) { g_netNodeBug = mode; }
int  netNodeBug() { return g_netNodeBug; }

// ── Input nodes ──────────────────────────────────────────────────────────────────────────────────
// Byte-stream inputs (UDPInput/TcpClient/TcpServer/SerialInput): a matching-kind arrival this frame
// fires WasTrigger (UDPInput.cs:99 / TcpClient.cs:697 / SerialInput.cs:1485). TcpServer's first output is
// IsListening (level, held by Listen); TcpClient/SerialInput's second output is IsConnected — both are
// level flags the golden drives via the config bool (no live socket here), echoed for parity.
// DMX inputs (ArtnetInput/SacnInput): a matching-universe frame this frame fires Activity → extOut[0]
// (ArtnetInput.cs:684 / SacnInput.cs:1813 — a subscribed universe received data).
void cookNetInputNodes(ResidentEvalGraph& g) {
  ResidentEvalCtx rctx;
  const NetDeviceBus& bus = netDeviceBus();
  for (ResidentNode& rn : g.nodes) {
    const std::string& t = rn.opType;
    const bool isUdp    = t == "UDPInput";
    const bool isTcpCli = t == "TcpClient";
    const bool isTcpSrv = t == "TcpServer";
    const bool isSerial = t == "SerialInput";
    const bool isArtnet = t == "ArtnetInput";
    const bool isSacn   = t == "SacnInput";
    if (!(isUdp || isTcpCli || isTcpSrv || isSerial || isArtnet || isSacn)) continue;

    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);

    if (isArtnet || isSacn) {
      const int start = (int)std::lround(P["StartUniverse"]);
      const int num   = (int)std::lround(P["NumUniverses"]);
      const int myKind = isArtnet ? kArtnet : kSacn;
      bool active = false;
      for (const NetBusSignal& s : bus.in)
        if (s.kind == myKind && universeInRange(s.universe, start, num)) active = true;
      rn.extOut[0] = active ? 1.0f : 0.0f;   // Activity
      continue;
    }

    // Byte-stream input: kind-matched arrival → WasTrigger.
    const int myKind = isUdp ? kUdp : isTcpCli ? kTcpClient : isTcpSrv ? kTcpServer : kSerial;
    bool trig = false;
    for (const NetBusSignal& s : bus.in)
      if (s.kind == myKind) trig = true;
    rn.extOut[0] = trig ? 1.0f : 0.0f;       // WasTrigger (UDP/Serial) / IsListening (TcpServer, see below)

    // Two-output socket inputs: TcpServer [IsListening, ConnectionCount]; TcpClient/SerialInput
    // [WasTrigger, IsConnected]. The level flags ride the config bool (Listen/Connect) — no live socket
    // in the cook, so IsListening/IsConnected echo the enable (real-transport parity: app overwrites).
    if (isTcpSrv) {
      rn.extOut[0] = P["Listen"] > 0.5f ? 1.0f : 0.0f;                 // IsListening (cs:1129)
      rn.extOut[1] = (P["Listen"] > 0.5f && trig) ? 1.0f : 0.0f;       // ConnectionCount≥1 proxy
    } else if (isTcpCli || isSerial) {
      rn.extOut[1] = P["Connect"] > 0.5f ? 1.0f : 0.0f;               // IsConnected (cs:698 / cs:1505)
    }
  }
}

// ── Output nodes ─────────────────────────────────────────────────────────────────────────────────
// Send-edge gate (shared by every *Output). SendContinuously-style nodes (UDP/TCP/Serial with
// SendOnChange, Artnet/Sacn/DMX with a send enable) fire every frame the enable holds; the manual
// SendTrigger fires only on the rising edge. TEETH mode 1 drops the edge (fires while held → RED).
namespace {
bool sendConditionEdge(bool enableNow, NetOutputState& st) {
  const bool rising = enableNow && !st.triggered;   // rising edge
  st.triggered = enableNow;
  if (g_netNodeBug == 1) return enableNow;           // edge dropped → level-fire
  return rising;
}
}  // namespace

void cookNetOutputNodes(ResidentEvalGraph& g, std::map<std::string, NetOutputState>& state) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    const std::string& t = rn.opType;
    const bool isUdp   = t == "UDPOutput";
    const bool isTcpC  = t == "TcpClient";     // TcpClient is bidirectional; its send side rides here
    const bool isTcpS  = t == "TcpServer";
    const bool isSer   = t == "SerialOutput";
    const bool isWled  = t == "WLedSerialOutput";
    const bool isArt   = t == "ArtnetOutput";
    const bool isDmx   = t == "DMXOutput";
    const bool isSacn  = t == "SacnOutput";
    if (!(isUdp || isSer || isWled || isArt || isDmx || isSacn || isTcpS)) continue;
    // (TcpClient's WasTrigger/IsConnected are cooked by cookNetInputNodes; its scalar rail has no send
    // echo output, so it is not re-cooked here — avoids double-writing extOut[0].)
    (void)isTcpC;

    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    NetOutputState& st = state[rn.path];

    // The send enable per family: manual SendTrigger (UDP/TCP/Serial/Artnet/Sacn) or Connect-held.
    bool enable = false;
    if (isUdp || isSer)          enable = P["SendTrigger"] > 0.5f || P["SendOnChange"] > 0.5f;
    else if (isTcpS)             enable = P["SendTrigger"] > 0.5f || P["SendOnChange"] > 0.5f;
    else if (isArt || isSacn)    enable = P["SendTrigger"] > 0.5f;
    else if (isDmx || isWled)    enable = P["Connect"] > 0.5f;

    const bool doSend = sendConditionEdge(enable, st);
    if (!doSend) { rn.extOut[0] = 0.0f; continue; }

    // A send fired this frame: push a marker onto the net-out bus (the app drains it; the real DMX wire
    // packet is assembled by platform/dmx_packet at the app-forward layer — runtime never includes the
    // platform codec, per the leaf-seam rule) and echo the value-rail probe. The packet ASSEMBLY is
    // goldened byte-推 in selftests_io_dmx; here we prove the SEND-EDGE gate + the echo.
    int kind = kUdp;
    if (isArt)                       kind = kArtnet;
    else if (isSacn)                 kind = kSacn;
    else if (isDmx || isSer || isWled) kind = kSerial;   // all serial-backed senders
    else if (isTcpS)                 kind = kTcpServer;
    emitNetPacket(kind, /*universe*/ 1, /*bytes*/ {});   // marker: 1 send this frame
    rn.extOut[0] = 1.0f;   // send-happened echo (packets-sent / IsConnected)
  }
}

// frame_cook entry point: cook the net input + output families, then clear the bus. State map is a
// function-local static keyed by resident path (per-instance, survives projection rebuilds).
// DEFERRED-HW-VERIFY: once the real socket forwarder is wired app-side, it drains bus.out BEFORE this
// clear (the send side-effect leg — the physical socket). Today nothing forwards them (no live device);
// the golden drives the cooks directly and reads bus.out before its own clear.
void cookNetDeviceNodes(ResidentEvalGraph& g) {
  static std::map<std::string, NetOutputState> s_outState;
  cookNetInputNodes(g);
  cookNetOutputNodes(g, s_outState);
  endNetDeviceFrame();
}

}  // namespace sw
