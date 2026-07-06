// runtime/node_registry_math_net_io — self-registering NodeSpec leaf for the io/tcp + io/udp + io/serial
// + io/dmx operator family (socket & DMX-over-network device nodes). Sibling of node_registry_math_io.cpp
// (the midi/osc leaf); split per ARCHITECTURE rule 7. These nodes are DEVICE-driven: no pure evaluate()
// (their value comes from the net/serial device bus + their effect is a send side-effect) →
// evaluate==nullptr, cooked once per frame by frame_cook's io seam (cookNetDeviceNodes, net_node_cook.cpp),
// exactly like MidiInput/OscInput write extOut.
//
// TiXL authority per spec cited inline (Operators/Lib/io/{tcp,udp,serial,dmx}/*.cs). Ground truth
// mirrored read-only; the packet/frame ASSEMBLY value lives in platform/dmx_packet + platform/wled_frame
// and is goldened byte-推 (selftests_io_dmx / selftests_io_socket); these NodeSpecs are the graph SKIN
// (drag/appear/inspect parity) + the scalar status/echo value-rail probe.
//
// FAMILY-WIDE FORKS (named, faithful — NOT invented):
//  fork-net-message-currency-deferred: the STRING message rail (TcpClient/Server ReceivedString +
//    MessageParts, UDPInput/Output, SerialInput/Output) and the LIST<int> DMX-universe rail
//    (Artnet/Sacn/DMX Inputs/Outputs InputsValues/Result) are String/List currency — a SEPARATE seam
//    (seam/dict-currency). Each node ships its scalar/bool config knobs + a scalar status/echo output
//    (WasTrigger / IsConnected / echo) as the value rail; the String/List ports are documented TODO,
//    NOT faked onto the Float rail (same discipline as OscInput's Contents/Values deferral).
//  fork-net-shared-transport: the app owns one socket per family (net_loopback); the per-node
//    LocalIpAddress/Host/Port/TargetIp/PortName device-select drops to app config (mirrors the
//    midi/osc shared-transport fork) — the node carries the semantically-load-bearing knobs.
//  fork-net-printtolog-dropped: PrintToLog / telemetry inputs are editor logging, not node value → dropped.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

// ── io/udp ─────────────────────────────────────────────────────────────────────────────────────────

// TiXL UDPInput (Lib/io/udp/UDPInput.cs) — bind+recv UTF-8 datagrams. STATEFUL (net bus), evaluate==
// nullptr; cook writes WasTrigger → extOut[0]. .t3 DEFAULTS: Listen=false, Port=7000, ListLength=10,
// LocalIpAddress="0.0.0.0". String message rail (ReceivedString/ReceivedLines) + LastSender* deferred
// (fork-net-message-currency-deferred). Port/LocalIp = shared-transport fork; Listen + ListLength kept.
static const MathOp _reg_UDPInput{
    {"UDPInput", "UDPInput",
     {{"WasTrigger", "WasTrigger", "Float", false},   // extOut[0] — a datagram arrived this frame (cs:99)
      {"Listen", "Listen", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},          // cs:23
      {"Port", "Port", "Float", true, 7000.0f, 0.0f, 65535.0f, Widget::Slider},     // cs:32
      {"ListLength", "ListLength", "Float", true, 10.0f, 1.0f, 1000.0f, Widget::Slider}},  // cs:26
     nullptr, "io.udp"}
};

// TiXL UDPOutput (Lib/io/udp/UDPOutput.cs) — send UTF-8 datagram to target. STATEFUL send side-effect;
// cook echoes IsConnected → extOut[0]. .t3: Connect=false, SendOnChange=true, SendTrigger=false,
// Separator=" ", TargetIpAddress="127.0.0.1", TargetPort=7001, LocalIpAddress="127.0.0.1". MessageParts
// (MultiInput<string>) deferred (fork-net-message-currency-deferred). shouldSend = manualTrigger ||
// (sendOnChange && changed) (cs:417).
static const MathOp _reg_UDPOutput{
    {"UDPOutput", "UDPOutput",
     {{"IsConnected", "IsConnected", "Float", false},   // extOut[0] — socket open echo (cs:409)
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},            // cs:308
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},    // cs:329
      {"SendOnChange", "SendOnChange", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},  // cs:326
      {"TargetPort", "TargetPort", "Float", true, 7001.0f, 0.0f, 65535.0f, Widget::Slider}}, // cs:338
     nullptr, "io.udp"}
};

// ── io/tcp ─────────────────────────────────────────────────────────────────────────────────────────

// TiXL TcpClient (Lib/io/tcp/TcpClient.cs) — connect + recv/send UTF-8. STATEFUL; cook writes
// WasTrigger → extOut[0], IsConnected → extOut[1]. .t3: Connect=false, Host="localhost", Port=8080,
// SendOnChange=true, SendTrigger=false, Separator=" ", ListLength=10, LocalIpAddress="0.0.0.0 (Any)".
// Host is a String device-select (shared-transport fork); ReceivedString + MessageParts deferred.
static const MathOp _reg_TcpClient{
    {"TcpClient", "TcpClient",
     {{"WasTrigger", "WasTrigger", "Float", false},     // extOut[0] — a message arrived (cs:697)
      {"IsConnected", "IsConnected", "Float", false},    // extOut[1] — socket connected (cs:698)
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},           // cs:555
      {"Port", "Port", "Float", true, 8080.0f, 0.0f, 65535.0f, Widget::Slider},        // cs:570
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},   // cs:585
      {"SendOnChange", "SendOnChange", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}, // cs:582
      {"ListLength", "ListLength", "Float", true, 10.0f, 1.0f, 1000.0f, Widget::Slider}},   // cs:564
     nullptr, "io.tcp"}
};

// TiXL TcpServer (Lib/io/tcp/TcpServer.cs) — listen + accept + broadcast. STATEFUL; cook writes
// IsListening → extOut[0], ConnectionCount → extOut[1]. .t3: Listen=false, LocalIpAddress="0.0.0.0",
// Port=8080, SendOnChange=true, SendTrigger=false. Message (String) deferred.
static const MathOp _reg_TcpServer{
    {"TcpServer", "TcpServer",
     {{"IsListening", "IsListening", "Float", false},   // extOut[0] — listener up (cs:1129)
      {"ConnectionCount", "ConnectionCount", "Float", false},  // extOut[1] — # clients (cs:1130)
      {"Listen", "Listen", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},            // cs:1045
      {"Port", "Port", "Float", true, 8080.0f, 0.0f, 65535.0f, Widget::Slider},       // cs:1054
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},  // cs:1066
      {"SendOnChange", "SendOnChange", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},   // cs:1063
     nullptr, "io.tcp"}
};

// ── io/serial ────────────────────────────────────────────────────────────────────────────────────

// TiXL SerialInput (Lib/io/serial/SerialInput.cs) — read + line-split. STATEFUL; cook writes
// WasTrigger → extOut[0], IsConnected → extOut[1]. .t3: PortName="", BaudRate=9600, ListLength=10,
// Connect=false. PortName device-select (shared-transport fork); ReceivedString deferred.
static const MathOp _reg_SerialInput{
    {"SerialInput", "SerialInput",
     {{"WasTrigger", "WasTrigger", "Float", false},     // extOut[0] — a line arrived (cs:1485)
      {"IsConnected", "IsConnected", "Float", false},    // extOut[1] — port open (cs:1505)
      {"BaudRate", "BaudRate", "Float", true, 9600.0f, 0.0f, 4000000.0f, Widget::Slider},   // cs:1527
      {"ListLength", "ListLength", "Float", true, 10.0f, 1.0f, 1000.0f, Widget::Slider},    // cs:1528
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},               // cs:1529
     nullptr, "io.serial"}
};

// TiXL SerialOutput (Lib/io/serial/SerialOutput.cs) — Write / WriteLine. STATEFUL; cook echoes
// IsConnected → extOut[0]. .t3: PortName="", BaudRate=9600, Separator=" ", SendTrigger=false,
// SendOnChange=true, AddLineEnding=true, Connect=false. MessageParts (MultiInput<string>) deferred.
static const MathOp _reg_SerialOutput{
    {"SerialOutput", "SerialOutput",
     {{"IsConnected", "IsConnected", "Float", false},   // extOut[0] — port open echo (cs:1575)
      {"BaudRate", "BaudRate", "Float", true, 9600.0f, 0.0f, 4000000.0f, Widget::Slider},   // cs:1619
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},        // cs:1622
      {"SendOnChange", "SendOnChange", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},      // cs:1623
      {"AddLineEnding", "AddLineEnding", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},    // cs:1624
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},               // cs:1625
     nullptr, "io.serial"}
};

// TiXL WLedSerialOutput (Lib/io/serial/WLedSerialOutput.cs) — Adalight LED frame SENDER. The frame body
// is goldened byte-推 in platform/wled_frame (selftests_io-wled-frame). STATEFUL; cook echoes
// IsConnected → extOut[0]. The LED-count/map-mode/color-order/brightness knobs are the frame-builder
// inputs (WledColor list deferred = fork-net-message-currency-deferred). .t3 DEFAULTS: LedCount=0(=src),
// Brightness=1, MapMode=0(Stretch), ColorOrder=0(GRB), Connect=false, BaudRate=115200.
static const MathOp _reg_WLedSerialOutput{
    {"WLedSerialOutput", "WLedSerialOutput",
     {{"IsConnected", "IsConnected", "Float", false},   // extOut[0] — port open echo
      {"LedCount", "LedCount", "Float", true, 0.0f, 0.0f, 4096.0f, Widget::Slider},
      {"Brightness", "Brightness", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
      {"MapMode", "MapMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Stretch", "Repeat", "Lerp"}},
      {"ColorOrder", "ColorOrder", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum,
       {"GRB", "RGB", "BRG", "RBG", "BGR", "GBR"}},
      {"BaudRate", "BaudRate", "Float", true, 115200.0f, 0.0f, 4000000.0f, Widget::Slider},
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     nullptr, "io.serial"}
};

// ── io/json (HTTP) ─────────────────────────────────────────────────────────────────────────────────

// TiXL RequestUrl (Lib/io/json/RequestUrl.cs) — HTTP GET, body → Result string. STATEFUL (async
// download + rising-edge trigger), evaluate==nullptr; the http cook (net_node_cook http seam) writes
// RequestFired → extOut[0] on the frame a request is kicked. .t3 DEFAULTS: Url="" (String device-select,
// shared-transport fork), TriggerRequest=false. The Result STRING body is String currency → deferred
// (fork-net-message-currency-deferred), exactly like TcpClient's ReceivedString; the scalar RequestFired
// echo is the value rail. Request fires on TriggerRequest rising edge OR Url change (cs:18-23).
static const MathOp _reg_RequestUrl{
    {"RequestUrl", "RequestUrl",
     {{"RequestFired", "RequestFired", "Float", false},  // extOut[0] — a GET was kicked this frame (cs:20)
      {"Url", "Url", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},  // cs:8c7a6493
      {"TriggerRequest", "TriggerRequest", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},  // cs:4DC9DEF5
     nullptr, "io.json"}
};

// TiXL LoadImageFromUrl (Lib/image/generate/load/LoadImageFromUrl.cs) — HTTP GET → WIC decode → Texture2D.
// STATEFUL (async download + rising-edge trigger), evaluate==nullptr; the http cook writes RequestFired →
// extOut[0] on the frame a download is kicked. .t3 DEFAULTS: Url="" (String device-select, shared-transport
// fork), TriggerUpdate=false. The Texture2D output is texture currency riding the app image_decode path →
// deferred (fork-net-message-currency-deferred, texture leg); the scalar RequestFired echo is the value
// rail. Download fires on TriggerUpdate rising edge OR Url change to non-empty (cs:25-28); the 2xx gate is
// the download path's (cs:76), surfaced as RequestFired only when a GET is actually issued.
static const MathOp _reg_LoadImageFromUrl{
    {"LoadImageFromUrl", "LoadImageFromUrl",
     {{"RequestFired", "RequestFired", "Float", false},   // extOut[0] — a download was kicked (cs:26)
      {"Url", "Url", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},  // cs:21b2e219
      {"TriggerUpdate", "TriggerUpdate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},  // cs:710BA03D
     nullptr, "image.load"}
};

// ── io/websocket + io/http ───────────────────────────────────────────────────────────────────────

// TiXL WebSocketClient (Lib/io/websocket/WebSocketClient.cs) — ClientWebSocket connect + send/recv text.
// STATEFUL (net bus + live socket), evaluate==nullptr; cook writes WasTrigger → extOut[0], IsConnected →
// extOut[1] (mirror of TcpClient). .t3 DEFAULTS: Connect=false, Url="ws://localhost:8080", SendOnChange=
// true, SendTrigger=false, ListLength=10, ParsingMode=0(Raw), Delimiter=" ", Separator=" ". The String
// ReceivedString/Lines + MessageParts (MultiInput<string>) + Dict/List<float> parse rails are String/
// Dict currency → deferred (fork-net-message-currency-deferred); the scalar WasTrigger/IsConnected echo
// is the value rail. Url is a String device-select (shared-transport fork). Send fires on manualTrigger
// || (sendOnChange && changed) (cs:152).
static const MathOp _reg_WebSocketClient{
    {"WebSocketClient", "WebSocketClient",
     {{"WasTrigger", "WasTrigger", "Float", false},     // extOut[0] — a message arrived (cs:238)
      {"IsConnected", "IsConnected", "Float", false},    // extOut[1] — socket open (cs:239)
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},           // cs:29
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},   // cs:65
      {"SendOnChange", "SendOnChange", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}, // cs:62
      {"ListLength", "ListLength", "Float", true, 10.0f, 1.0f, 1000.0f, Widget::Slider}},  // cs:38
     nullptr, "io.websocket"}
};

// TiXL WebSocketServer (Lib/io/websocket/WebSocketServer.cs) — HttpListener.AcceptWebSocket accept loop +
// per-client recv + broadcast. STATEFUL; cook writes IsListening → extOut[0], ConnectionCount → extOut[1]
// (mirror of TcpServer). .t3 DEFAULTS: Listen=false, Port=8080, Path="", LocalIpAddress="0.0.0.0 (…)",
// SendOnChange=true, SendTrigger=false, ClientMessageParsingMode=0(Raw), ClientMessageDelimiter=" ".
// LastReceivedClientString + Parts + Dictionary rails deferred (fork-net-message-currency-deferred);
// Port/LocalIp/Path = shared-transport fork; Listen + Port kept as the load-bearing knobs.
static const MathOp _reg_WebSocketServer{
    {"WebSocketServer", "WebSocketServer",
     {{"IsListening", "IsListening", "Float", false},   // extOut[0] — listener up (cs:118)
      {"ConnectionCount", "ConnectionCount", "Float", false},  // extOut[1] — # clients (cs:119)
      {"Listen", "Listen", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},            // cs:524
      {"Port", "Port", "Float", true, 8080.0f, 0.0f, 65535.0f, Widget::Slider},       // cs:527
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},  // cs:539
      {"SendOnChange", "SendOnChange", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},   // cs:536
     nullptr, "io.websocket"}
};

// TiXL WebServer (Lib/io/http/WebServer.cs) — HTTP static server: serve a user HTML string on "/" (200
// text/html, cs:223), 404 any other path (cs:240). STATEFUL (live HttpListener); cook writes IsRunning →
// extOut[0]. .t3 DEFAULTS: Listen=false, LocalIpAddress="0.0.0.0 (Any)", Port=8080, HtmlContent=<default
// page>. The HtmlContent body is String currency → deferred (fork-net-message-currency-deferred); the
// scalar IsRunning echo is the value rail. Port/LocalIp = shared-transport fork; Listen + Port kept.
static const MathOp _reg_WebServer{
    {"WebServer", "WebServer",
     {{"IsRunning", "IsRunning", "Float", false},   // extOut[0] — listener up echo (cs:62)
      {"Listen", "Listen", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},            // cs:379
      {"Port", "Port", "Float", true, 8080.0f, 0.0f, 65535.0f, Widget::Slider}},      // cs:385
     nullptr, "io.http"}
};

// ── io/dmx ─────────────────────────────────────────────────────────────────────────────────────────

// TiXL ArtnetOutput (Lib/io/dmx/ArtnetOutput.cs) — Art-Net DMX SENDER (UDP 6454). Packet body goldened
// byte-推 in platform/dmx_packet (selftests_io-artnet-packet). STATEFUL; cook echoes a send-count probe
// → extOut[0]. .t3: SendTrigger=false, Reconnect=false, SendSync=false, SendUnicast=false,
// EnableArtNet4=true, MaxFps=60. InputsValues (MultiInput<List<int>>) + UniverseChannels (List<int>) +
// LocalIp/TargetIp deferred (fork-net-message-currency-deferred / shared-transport).
static const MathOp _reg_ArtnetOutput{
    {"ArtnetOutput", "ArtnetOutput",
     {{"Result", "Result", "Float", false},   // extOut[0] — packets-sent-this-frame echo (golden probe)
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},   // cs:586
      {"SendSync", "SendSync", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},         // cs:588
      {"SendUnicast", "SendUnicast", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},   // cs:589
      {"EnableArtNet4", "EnableArtNet4", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},  // cs:590
      {"MaxFps", "MaxFps", "Float", true, 60.0f, 0.0f, 240.0f, Widget::Slider}},       // cs:593
     nullptr, "io.dmx"}
};

// TiXL ArtnetInput (Lib/io/dmx/ArtnetInput.cs) — Art-Net DMX RECEIVER. Parse body goldened byte-推
// (selftests_io-artnet-parse). STATEFUL; cook echoes an activity probe → extOut[0]. .t3: Active=false,
// LocalIpAddress="0.0.0.0 (Any)", NumUniverses=1, StartUniverse=1, Timeout=1.2. Result (List<int>)
// deferred (fork-net-message-currency-deferred).
static const MathOp _reg_ArtnetInput{
    {"ArtnetInput", "ArtnetInput",
     {{"Activity", "Activity", "Float", false},   // extOut[0] — 1 if a subscribed universe is live
      {"Active", "Active", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},               // cs:618
      {"StartUniverse", "StartUniverse", "Float", true, 1.0f, 0.0f, 32767.0f, Widget::Slider}, // cs:633
      {"NumUniverses", "NumUniverses", "Float", true, 1.0f, 1.0f, 4096.0f, Widget::Slider},    // cs:624
      {"Timeout", "Timeout", "Float", true, 1.2f, 0.0f, 60.0f, Widget::Slider}},        // cs:636
     nullptr, "io.dmx"}
};

// TiXL DMXOutput (Lib/io/dmx/DMXOutput.cs) — USB-DMX serial dongle SENDER. Frame body goldened byte-推
// (selftests_io-dmx-serial). STATEFUL; cook echoes IsConnected → extOut[0]. .t3: Connect=false,
// MaxFps=40, PortName="". DmxUniverse (List<int>) + PortName deferred. (Serial baud is fixed 250000,
// cs:1028 — the DMX standard rate, not an exposed knob.)
static const MathOp _reg_DMXOutput{
    {"DMXOutput", "DMXOutput",
     {{"IsConnected", "IsConnected", "Float", false},   // extOut[0] — port open echo (cs:1043)
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},            // cs:949
      {"MaxFps", "MaxFps", "Float", true, 40.0f, 0.0f, 240.0f, Widget::Slider}},        // cs:958
     nullptr, "io.dmx"}
};

// TiXL SacnOutput (Lib/io/dmx/SacnOutput.cs) — E1.31 sACN SENDER (UDP 5568). Packet body goldened
// byte-推 (selftests_io-sacn-packet / -sync). STATEFUL; cook echoes a send-count probe → extOut[0].
// .t3: SendTrigger=false, Reconnect=false, SendUnicast=false, DiscoverSources=false, Priority=100,
// MaxFps=60, EnableSync=false, SyncUniverse=1. InputsValues/UniverseChannels + Local/TargetIp +
// SourceName(String) deferred.
static const MathOp _reg_SacnOutput{
    {"SacnOutput", "SacnOutput",
     {{"Result", "Result", "Float", false},   // extOut[0] — packets-sent-this-frame echo (golden probe)
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},   // cs:1704
      {"SendUnicast", "SendUnicast", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},   // cs:1706
      {"Priority", "Priority", "Float", true, 100.0f, 0.0f, 200.0f, Widget::Slider},   // cs:1709
      {"MaxFps", "MaxFps", "Float", true, 60.0f, 0.0f, 240.0f, Widget::Slider},        // cs:1711
      {"EnableSync", "EnableSync", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},     // cs:1712
      {"SyncUniverse", "SyncUniverse", "Float", true, 1.0f, 1.0f, 63999.0f, Widget::Slider}},  // cs:1713
     nullptr, "io.dmx"}
};

// TiXL SacnInput (Lib/io/dmx/SacnInput.cs) — E1.31 sACN RECEIVER (multicast). Parse body goldened
// byte-推 (selftests_io-sacn-parse). STATEFUL; cook echoes an activity probe → extOut[0]. .t3:
// Active=false, LocalIpAddress="0.0.0.0 (Any)", NumUniverses=1, StartUniverse=1, Timeout=0. Result
// (List<int>) deferred.
static const MathOp _reg_SacnInput{
    {"SacnInput", "SacnInput",
     {{"Activity", "Activity", "Float", false},   // extOut[0] — 1 if a subscribed universe is live
      {"Active", "Active", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},               // cs:1735
      {"StartUniverse", "StartUniverse", "Float", true, 1.0f, 1.0f, 63999.0f, Widget::Slider}, // cs:1752
      {"NumUniverses", "NumUniverses", "Float", true, 1.0f, 1.0f, 4096.0f, Widget::Slider},    // cs:1742
      {"Timeout", "Timeout", "Float", true, 0.0f, 0.0f, 60.0f, Widget::Slider}},         // cs:1755
     nullptr, "io.dmx"}
};

}  // namespace
}  // namespace sw
