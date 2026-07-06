// runtime/node_registry_math_link — self-registering MATH NodeSpec leaf: AbletonLinkSync (network beat
// sync). Reads the app-owned Ableton Link session (platform/link_sync) each frame and emits Result /
// Tempo / IsConnected. New per-subfamily leaf (SW_MATH_SRCS glob — no CMake edit).
//
// TiXL: numbers/anim/time/AbletonLinkSync.cs (.t3). Cooked by the app's per-frame cooker
// (cookLinkSyncNodes, frame_cook) — evaluate==nullptr — because it needs the live LinkSnapshot (a
// native session handle) the resolved-Float map cannot carry (same externally-cooked pattern as
// AudioReaction). Result/Tempo/IsConnected outputs FIRST (extOut by port index).
//
// .t3 defaults (AbletonLinkSync.t3, load-bearing): OutputType=0 (Bars), TriggerStartPlaying=false,
// TriggerStopPlaying=false, TriggerReconnect=false, AutoConnect=true, PauseIfDisconnected=true.
//
// Result-selection (ReturnTypes, cs:278-285): the OutputType enum picks Bars/Phase/Beats/Time/Quantum.
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

static const MathOp _reg_AbletonLinkSync{
    {"AbletonLinkSync", "AbletonLinkSync",
     {{"Result", "Result", "Float", false},
      {"Tempo", "Tempo", "Float", false},
      {"IsConnected", "IsConnected", "Float", false},  // bool-as-float (1/0) — the value rail is float
      {"OutputType", "OutputType", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Bars", "Phase", "Beats", "Time", "Quantum"}},
      {"TriggerStartPlaying", "TriggerStartPlaying", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"TriggerStopPlaying", "TriggerStopPlaying", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"TriggerReconnect", "TriggerReconnect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"AutoConnect", "AutoConnect", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
      {"PauseIfDisconnected", "PauseIfDisconnected", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},
     nullptr,
     "numbers.anim.time"}};

}  // namespace
}  // namespace sw
