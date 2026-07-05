// runtime/node_registry_math_tracking_io — self-registering NodeSpec leaf for the io/freed + io/posistage
// camera/tracker family, the io/dmx point→list converters, io/audio AudioToneGenerator, and io/file
// WriteToFile. Sibling of node_registry_math_net_io.cpp (socket/DMX device nodes); split per ARCHITECTURE
// rule 7. Like the net_io family these nodes are DEVICE/HOST-driven: no pure evaluate() (their value comes
// from a UDP device bus, a GPU point readback, the audio mixer, or a file gate + their effect is a
// side-effect) → evaluate==nullptr; the graph SKIN (drag/appear/inspect parity) + a scalar status/echo
// value-rail probe are what these specs carry.
//
// TiXL authority per spec cited inline (Operators/Lib/io/{freed,posistage,dmx,audio,file}/*.cs). Ground
// truth mirrored read-only; the packet/value ASSEMBLY is goldened byte-推 in platform/freed_packet +
// platform/psn_packet + platform/points_dmx + platform/write_to_file (selftests_io_misc), and the tone
// synthesis core in runtime/tone_synth (tonesynth_golden). These NodeSpecs are the value-rail + skin.
//
// FAMILY-WIDE FORKS (named, faithful — NOT invented):
//  fork-tracking-buffer-currency-deferred: the CamerasAsBuffer / TrackersAsBuffer (Points buffer) and the
//    CameraDataAsDict / TrackersAsDict (Dict<float>) outputs are Points/Dict currency. FreeDInput/
//    PosiStageInput ship their Dict output (dict-currency IS live) + a scalar IsListening/CameraPos echo;
//    the raw GPU Point BUFFER output is documented TODO (the GPU tracker-buffer seam), NOT faked onto Float.
//  fork-net-shared-transport: LocalIpAddress/MulticastIpAddress/Port/TargetIp device-select drops to app
//    config (mirrors the net_io shared-transport fork); the node carries the semantically load-bearing
//    Listen/Connect/SendOnChange/SendTrigger knobs + the tracking payload ports.
//  fork-printtolog-dropped: PrintToLog inputs are editor logging, not node value → dropped.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

// ── io/freed ─────────────────────────────────────────────────────────────────────────────────────

// TiXL FreeDInput (Lib/io/freed/FreeDInput.cs) — bind+recv 29-byte "D1" packets → per-camera dict +
// GPU buffer + selected-camera scalars. STATEFUL (UDP bus), evaluate==nullptr; cook writes IsListening →
// extOut[0]. .t3 DEFAULTS: Listen=false, Port=6000, CameraId=-1, LocalIpAddress="0.0.0.0 (Any)". The
// CamerasAsBuffer (Points) is deferred (fork-tracking-buffer-currency-deferred); the CameraDataAsDict
// output IS carried (dict-currency live). Port/LocalIp = shared-transport fork.
static const MathOp _reg_FreeDInput{
    {"FreeDInput", "FreeDInput",
     {{"IsListening", "IsListening", "Float", false},  // extOut[0] — listener up (cs:49)
      {"CameraDataAsDict", "CameraDataAsDict", "Dict", false},  // per-camera /<id>/Pan… dict (cs:110)
      {"Listen", "Listen", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},              // cs:343
      {"Port", "Port", "Float", true, 6000.0f, 0.0f, 65535.0f, Widget::Slider},         // cs:349
      {"CameraId", "CameraId", "Float", true, -1.0f, -1.0f, 255.0f, Widget::Slider}},   // cs:352 (-1 = lowest)
     nullptr, "io.freed"}
};

// TiXL FreeDOutput (Lib/io/freed/FreeDOutput.cs) — build+send a 29-byte "D1" packet on trigger/change.
// STATEFUL send side-effect; cook echoes IsConnected → extOut[0]. .t3: Connect=false, TargetIpAddress=
// "127.0.0.1", TargetPort=6000, SendOnChange=false, SendTrigger=false, CameraId=0. Rotation/Position are
// Vector3 payload inputs (Vec widgets); Zoom/Focus/User are int payloads. shouldSend = WasTriggered ||
// SendOnChange (cs:45). Packet bytes goldened in platform/freed_packet.
static const MathOp _reg_FreeDOutput{
    {"FreeDOutput", "FreeDOutput",
     {{"IsConnected", "IsConnected", "Float", false},  // extOut[0] — socket open echo (cs:42)
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},            // cs:259
      {"TargetPort", "TargetPort", "Float", true, 6000.0f, 0.0f, 65535.0f, Widget::Slider},  // cs:268
      {"CameraId", "CameraId", "Float", true, 0.0f, 0.0f, 255.0f, Widget::Slider},      // cs:277
      {"Rotation.x", "Rotation",   "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, false, 3},  // cs:271 Vector3
      {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, false, 1},
      {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, false, 1},
      {"Position.x", "Position",   "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 3}, // cs:274 Vector3
      {"Position.y", "Position.y", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 1},
      {"Position.z", "Position.z", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 1},
      {"Zoom", "Zoom", "Float", true, 0.0f, 0.0f, 16777215.0f, Widget::Slider},         // cs:283 (0..0xFFFFFF)
      {"Focus", "Focus", "Float", true, 0.0f, 0.0f, 16777215.0f, Widget::Slider},       // cs:280
      {"User", "User", "Float", true, 0.0f, 0.0f, 65535.0f, Widget::Slider},            // cs:286 (0..0xFFFF)
      {"SendOnChange", "SendOnChange", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},  // cs:289
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},   // cs:292
     nullptr, "io.freed"}
};

// ── io/posistage ─────────────────────────────────────────────────────────────────────────────────

// TiXL PosiStageInput (Lib/io/posistage/PosiStageInput.cs) — join a PSN multicast group + recv chunk
// packets (root 0x6755) → per-tracker dict + GPU buffer. STATEFUL (UDP bus), evaluate==nullptr; cook
// writes IsListening → extOut[0]. .t3: Listen=false, Port=56565, LocalIpAddress="0.0.0.0",
// MulticastIpAddress="236.10.10.10". TrackersAsBuffer (Points) deferred; TrackersAsDict carried.
static const MathOp _reg_PosiStageInput{
    {"PosiStageInput", "PosiStageInput",
     {{"IsListening", "IsListening", "Float", false},  // extOut[0] — listener up (cs:64)
      {"TrackersAsDict", "TrackersAsDict", "Dict", false},  // per-tracker /<id>/Pos… dict (cs:106)
      {"Listen", "Listen", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},              // cs:339
      {"Port", "Port", "Float", true, 56565.0f, 0.0f, 65535.0f, Widget::Slider}},       // cs:348
     nullptr, "io.posistage"}
};

// TiXL PosiStageOutput (Lib/io/posistage/PosiStageOutput.cs) — read a tracker Point buffer, build+send a
// PSN_DATA packet on trigger/change. STATEFUL send side-effect; cook echoes IsConnected → extOut[0]. .t3:
// Connect=false, LocalIpAddress="127.0.0.1", TargetIpAddress="236.10.10.10", TargetPort=56565,
// SendOnChange=true, ServerName="T3 PSN Output". TrackerData (Points buffer) is the payload input
// (fork-tracking-buffer-currency-deferred: the input is carried as a Points pin; the CPU readback → PSN
// bytes is goldened in platform/psn_packet). Names MultiInput deferred.
static const MathOp _reg_PosiStageOutput{
    {"PosiStageOutput", "PosiStageOutput",
     {{"IsConnected", "IsConnected", "Float", false},  // extOut[0] — socket open echo (cs:85)
      {"TrackerData", "TrackerData", "Points", true},  // tracker Point buffer input (cs:51)
      {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},            // cs:21
      {"TargetPort", "TargetPort", "Float", true, 56565.0f, 0.0f, 65535.0f, Widget::Slider},  // cs:48
      {"SendOnChange", "SendOnChange", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},  // cs:36 (default true)
      {"SendTrigger", "SendTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},   // cs:39
     nullptr, "io.posistage"}
};

// ── io/dmx point converters ──────────────────────────────────────────────────────────────────────

// TiXL PointsToRGBList (Lib/io/dmx/obsolete/PointsToRGBList.cs) — read a Point buffer, emit round(Color*
// 255) as an (R,G,B) FloatList. STATELESS host readback, evaluate==nullptr (List output can't ride the
// Float rail; the value is the byte-推d conversion in platform/points_dmx). No knobs (updateContinuously).
static const MathOp _reg_PointsToRGBList{
    {"PointsToRGBList", "PointsToRGBList",
     {{"Points", "Points", "Points", true},            // point buffer input (cs:10)
      {"Result", "Result", "FloatList", false}},       // R,G,B triples per point (cs:13,109)
     nullptr, "io.dmx"}
};

// TiXL PointsToDmxLights (Lib/io/dmx/PointsToDMXLights.cs) — read effected+reference Point buffers, emit a
// DMX channel IntList (position/rotation/color/features mapped via SetDmxValue). STATEFUL (per-fixture
// shortest-path pan/tilt memory), evaluate==nullptr. The SetDmxValue 8/16-bit channel map is goldened in
// platform/points_dmx; the full IK pan/tilt solve is a heavier seam (fork-dmxlights-ik-deferred — the
// per-fixture _lastPanTilt cross-frame state + the atan2 pan/tilt basis). This spec carries the buffer
// inputs + the load-bearing channel-assignment + color/feature toggle knobs.
static const MathOp _reg_PointsToDmxLights{
    {"PointsToDmxLights", "PointsToDmxLights",
     {{"EffectedPoints", "EffectedPoints", "Points", true},    // cs:71
      {"ReferencePoints", "ReferencePoints", "Points", true},  // cs:74
      {"Result", "Result", "IntList", false},                  // DMX channel values (cs:17)
      {"FixtureChannelSize", "FixtureChannelSize", "Float", true, 0.0f, 0.0f, 512.0f, Widget::Slider},  // cs:78
      {"FitInUniverse", "FitInUniverse", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},   // cs:81
      {"FillUniverse", "FillUniverse", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},     // cs:84
      {"GetColor", "GetColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},             // cs:180
      {"RedChannel", "RedChannel", "Float", true, 0.0f, 0.0f, 512.0f, Widget::Slider},     // cs:186
      {"GreenChannel", "GreenChannel", "Float", true, 0.0f, 0.0f, 512.0f, Widget::Slider}, // cs:189
      {"BlueChannel", "BlueChannel", "Float", true, 0.0f, 0.0f, 512.0f, Widget::Slider},   // cs:192
      {"Is16BitColor", "Is16BitColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},    // cs:201
     nullptr, "io.dmx"}
};

// ── io/audio ─────────────────────────────────────────────────────────────────────────────────────

// TiXL AudioToneGenerator (Lib/io/audio/AudioToneGenerator.cs) — synthesize a procedural tone into the
// operator mixer (Sine/Square/Saw/Tri/White/Pink), ADSR-enveloped, Trigger/Gate modes. STATEFUL (BASS
// mixer stream), evaluate==nullptr; cook echoes IsPlaying → extOut[0]. The waveform synthesis CORE is
// goldened in runtime/tone_synth (tonesynth_golden); this spec is the NODE SKIN carrying the knobs +
// IsPlaying/GetLevel status. .t3 defaults: Frequency 440 (0 → 440 fallback), Volume 1, WaveformType Sine,
// TriggerMode Trigger, Envelope (A,D,S,R). The Result Command + real audio out are deferred-hw.
static const MathOp _reg_AudioToneGenerator{
    {"AudioToneGenerator", "AudioToneGenerator",
     {{"IsPlaying", "IsPlaying", "Float", false},   // extOut[0] — ADSR active echo (cs:52)
      {"GetLevel", "GetLevel", "Float", false},     // extOut[1] — output level (cs:55)
      {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},            // cs:24
      {"Frequency", "Frequency", "Float", true, 440.0f, 20.0f, 20000.0f, Widget::Slider},  // cs:27 (0→440)
      {"Duration", "Duration", "Float", true, 0.0f, 0.0f, 60.0f, Widget::Slider},       // cs:30 (0→∞)
      {"Volume", "Volume", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},            // cs:33
      {"Mute", "Mute", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},                  // cs:36
      {"WaveformType", "WaveformType", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum,   // cs:39
       {"Sine", "Square", "Sawtooth", "Triangle", "WhiteNoise", "PinkNoise"}},          // cs:202-208
      {"TriggerMode", "TriggerMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,     // cs:42
       {"Trigger", "Gate"}},
      // ADSR Vector4 (cs:46): X=Attack, Y=Decay, Z=Sustain, W=Release (cs:106-109 fallback defaults).
      {"Envelope.x", "Envelope",   "Float", true, 0.01f, 0.0f, 4.0f, Widget::Vec, {}, false, 4},   // Attack
      {"Envelope.y", "Envelope.y", "Float", true, 0.1f, 0.0f, 4.0f, Widget::Vec, {}, false, 1},    // Decay
      {"Envelope.z", "Envelope.z", "Float", true, 0.7f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},    // Sustain
      {"Envelope.w", "Envelope.w", "Float", true, 0.3f, 0.0f, 4.0f, Widget::Vec, {}, false, 1}},   // Release
     nullptr, "io.audio"}
};

// ── io/file ──────────────────────────────────────────────────────────────────────────────────────

// TiXL WriteToFile (Lib/io/file/WriteToFile.cs) — write Content to Filepath when Content changes; forward
// Content + Filepath. STATEFUL (last-content gate), evaluate==nullptr; cook writes on change + echoes the
// forwarded string. Content/Filepath are String inputs (strDef); the write-on-change gate + byte
// round-trip are goldened in platform/write_to_file. Result/OutFilepath (String) forwarded as the value.
static const MathOp _reg_WriteToFile{
    {"WriteToFile", "WriteToFile",
     // String input: default text rides `strDef` (PortSpec order: …pinless,vecArity,multiInput,strDef,required).
     {{"Content", "Content", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},  // cs:48
      {"Filepath", "Filepath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},  // cs:51
      {"Result", "Result", "String", false},           // forwarded Content (cs:40)
      {"OutFilepath", "OutFilepath", "String", false}}, // forwarded Filepath (cs:41)
     nullptr, "io.file"}
};

}  // namespace
}  // namespace sw
