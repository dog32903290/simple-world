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
// kWsClient/kWsServer are the WebSocket families (live upgrade+frame stream in platform/ws_live; the app
// pushes a decoded WS message arrival as its kind, exactly like the raw-TCP kinds).
enum : int { kUdp = 0, kTcpClient = 1, kTcpServer = 2, kSerial = 3, kArtnet = 4, kSacn = 5,
             kWsClient = 6, kWsServer = 7 };

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
    // WebSocket families — WsClient rides TcpClient's [WasTrigger,IsConnected] shape; WsServer rides
    // TcpServer's [IsListening,ConnectionCount]. WebServer echoes IsRunning from its Listen enable.
    const bool isWsCli  = t == "WebSocketClient";
    const bool isWsSrv  = t == "WebSocketServer";
    const bool isWebSrv = t == "WebServer";
    if (!(isUdp || isTcpCli || isTcpSrv || isSerial || isArtnet || isSacn || isWsCli || isWsSrv ||
          isWebSrv))
      continue;

    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);

    if (isWebSrv) {
      // WebServer: IsRunning echoes the Listen enable (WebServer.cs:62 _listener?.IsListening). No
      // inbound message rail — it is a static HTTP responder (the live serve is the app-side seam).
      rn.extOut[0] = P["Listen"] > 0.5f ? 1.0f : 0.0f;
      continue;
    }

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
    const int myKind = isUdp ? kUdp : isTcpCli ? kTcpClient : isTcpSrv ? kTcpServer
                       : isSerial ? kSerial : isWsCli ? kWsClient : isWsSrv ? kWsServer : kUdp;
    bool trig = false;
    for (const NetBusSignal& s : bus.in)
      if (s.kind == myKind) trig = true;
    rn.extOut[0] = trig ? 1.0f : 0.0f;       // WasTrigger (UDP/Serial) / IsListening (TcpServer, see below)

    // Two-output socket inputs: TcpServer/WebSocketServer [IsListening, ConnectionCount]; TcpClient/
    // SerialInput/WebSocketClient [WasTrigger, IsConnected]. The level flags ride the config bool
    // (Listen/Connect) — no live socket in the cook, so IsListening/IsConnected echo the enable
    // (real-transport parity: app overwrites with the actual ws_live server/client state).
    if (isTcpSrv || isWsSrv) {
      rn.extOut[0] = P["Listen"] > 0.5f ? 1.0f : 0.0f;                 // IsListening (cs:1129 / WS cs:118)
      rn.extOut[1] = (P["Listen"] > 0.5f && trig) ? 1.0f : 0.0f;       // ConnectionCount≥1 proxy
    } else if (isTcpCli || isSerial || isWsCli) {
      rn.extOut[1] = P["Connect"] > 0.5f ? 1.0f : 0.0f;               // IsConnected (cs:698 / cs:1505 / WS cs:239)
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
    const bool isWsS   = t == "WebSocketServer";  // WS broadcast rides TcpServer's send-edge shape
    if (!(isUdp || isSer || isWled || isArt || isDmx || isSacn || isTcpS || isWsS)) continue;
    // (TcpClient/WebSocketClient's WasTrigger/IsConnected are cooked by cookNetInputNodes; their scalar
    // rail has no send echo output, so they are not re-cooked here — avoids double-writing extOut[0].)
    (void)isTcpC;

    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    NetOutputState& st = state[rn.path];

    // The send enable per family: manual SendTrigger (UDP/TCP/WS/Serial/Artnet/Sacn) or Connect-held.
    bool enable = false;
    if (isUdp || isSer)          enable = P["SendTrigger"] > 0.5f || P["SendOnChange"] > 0.5f;
    else if (isTcpS || isWsS)    enable = P["SendTrigger"] > 0.5f || P["SendOnChange"] > 0.5f;
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
    else if (isWsS)                  kind = kWsServer;
    emitNetPacket(kind, /*universe*/ 1, /*bytes*/ {});   // marker: 1 send this frame
    rn.extOut[0] = 1.0f;   // send-happened echo (packets-sent / IsConnected)
  }
}

// ── HTTP nodes (RequestUrl / LoadImageFromUrl) ─────────────────────────────────────────────────────
// The http request bus + the trigger-edge cook. See net_node_cook.h. The runtime DECIDES a GET should
// fire; the app performs it (platform/http_request, a forbidden upward dep from runtime).
namespace {
int g_httpNodeBug = 0;
enum : int { kHttpText = 0, kHttpImage = 1 };
}  // namespace

HttpBus& httpBus() {
  static HttpBus bus;
  return bus;
}
void endHttpFrame() { httpBus().requests.clear(); }
void setHttpNodeBug(int mode) { g_httpNodeBug = mode; }
int  httpNodeBug() { return g_httpNodeBug; }

// A GET fires on the TriggerRequest RISING edge (TiXL MathUtils.WasTriggered, RequestUrl.cs:20) OR a Url
// CHANGE to a non-empty value (RequestUrl.cs:22 Url.DirtyFlag.IsDirty; LoadImageFromUrl.cs:27 gates on
// non-empty). We take the non-empty gate for both (the safe superset — a blank Url has nothing to GET).
// TEETH mode 1 drops the rising-edge gate: a held trigger fires EVERY frame (→ the edge-gated golden RED).
void cookHttpNodes(ResidentEvalGraph& g, std::map<std::string, HttpNodeState>& state) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    const std::string& t = rn.opType;
    const bool isText  = t == "RequestUrl";
    const bool isImage = t == "LoadImageFromUrl";
    if (!(isText || isImage)) continue;

    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    // The trigger port is named differently per node; both are the rising-edge bool.
    const float trigVal = isText ? P["TriggerRequest"] : P["TriggerUpdate"];
    const bool trigNow = trigVal > 0.5f;

    // Url rides the resident String channel (strInputs), NOT the float rail.
    std::string url;
    auto it = rn.strInputs.find("Url");
    if (it != rn.strInputs.end()) url = it->second;

    HttpNodeState& st = state[rn.path];

    // Rising edge (or, with TEETH 1, level). MathUtils.WasTriggered: false→true.
    const bool rising = trigNow && !st.triggered;
    const bool triggerFires = (g_httpNodeBug == 1) ? trigNow : rising;
    st.triggered = trigNow;

    // Url change: only fires once urlSeen (first cook establishes the baseline — no spurious fire on
    // the very first cook when lastUrl is empty and url happens to be set).
    const bool urlChanged = st.urlSeen && (url != st.lastUrl);
    st.lastUrl = url;
    st.urlSeen = true;

    // The GET fires when a trigger or a url-change asks for it AND the url is non-empty (nothing to GET
    // from a blank url). The non-empty gate is LoadImageFromUrl.cs:27 (`!string.IsNullOrEmpty(url)`),
    // applied to both as the safe superset.
    const bool fire = (triggerFires || urlChanged) && !url.empty();
    if (fire) {
      httpBus().requests.push_back({isText ? kHttpText : kHttpImage, url, rn.path});
    }
    rn.extOut[0] = fire ? 1.0f : 0.0f;   // RequestFired echo (value rail; body/texture deferred)
  }
}

// frame_cook entry point: cook the net input + output families, then clear the bus. State map is a
// function-local static keyed by resident path (per-instance, survives projection rebuilds).
// DEFERRED-HW-VERIFY: once the real socket forwarder is wired app-side, it drains bus.out BEFORE this
// clear (the send side-effect leg — the physical socket). Today nothing forwards them (no live device);
// the golden drives the cooks directly and reads bus.out before its own clear.
void cookNetDeviceNodes(ResidentEvalGraph& g) {
  static std::map<std::string, NetOutputState> s_outState;
  static std::map<std::string, HttpNodeState> s_httpState;
  cookNetInputNodes(g);
  cookNetOutputNodes(g, s_outState);
  cookHttpNodes(g, s_httpState);
  // NOTE: endHttpFrame() is NOT called here — the APP drains httpBus().requests to the real GET AFTER
  // this cook (deferred-hw-verify, mirror of bus.out). The app clears it once drained; the golden drives
  // cookHttpNodes directly and clears its own bus. (endNetDeviceFrame stays here — the net bus is drained
  // synchronously within the cook; the http request is async so its bus outlives this cook.)
  endNetDeviceFrame();
}

}  // namespace sw
