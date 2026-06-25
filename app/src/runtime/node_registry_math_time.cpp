// runtime/node_registry_math_time — self-registering MATH NodeSpec leaf:
// time / transport value ops (Time, HasTimeChanged, StopWatch, ConvertTime, RunTime,
// DelayTriggerChange).
//
// Split from the 980-line node_registry_math.cpp (ratchet debt, ARCHITECTURE rule 4 + rule 7).
// Every spec below is moved VERBATIM from the old mathSpecs() manifest — name / ports / widgets /
// defaults / evaluate binding unchanged. Adding a math op here = drop a MathOp registrar; the
// central manifest is never touched again (mirror of the point-modify / image-filter / value-op
// self-registration sinks). Stateless ops carry their pure evaluate fn; stateful ops carry
// nullptr (cooked by frame_cook's stateful-value seam, dispatched by type name).
#include "runtime/graph.h"            // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink
#include "runtime/value_eval_ops.h"    // evalSine, evalConst, … (pure value-node fns; evalTime removed)

namespace sw {
namespace {

      // --- Time (TiXL Lib/numbers/anim/time/Time.cs) — multi-mode time exposure with SpeedFactor.
      // 5 modes: LocalIdleMotionFxTime(0=default)/LocalTime(1)/PlaybackTime(2)/Runtime(3)/Frozen(4).
      // Units=0 Bars (default), Units=1 Secs (applies SecondsFromBars = time*240/bpm after SpeedFactor).
      // Stateful (evaluate==nullptr) — cooked by frame_cook's stateful-value seam from TransportSnapshot.
      // Output "Timefloat" = the TiXL C# slot field name. Marked live (opDeclaresLiveOutput, cached).
      // .t3 defaults: Mode=0, Units=0, SpeedFactor=1.0.
      // FORK (named): R-1 run-clock origin for Mode=3 (Runtime), same class as RunTime/StopWatch.
static const MathOp _reg_Time{
      {"Time", "Time",
       {{"Timefloat", "Timefloat", "Float", false},
        {"Mode",        "Mode",        "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
         {"LocalIdleMotionFxTime", "LocalTime", "PlaybackTime", "Runtime", "Frozen"}},
        {"Units",       "Units",       "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Bars", "Secs"}},
        {"SpeedFactor", "SpeedFactor", "Float", true, 1.0f, -5.0f, 5.0f}},
       nullptr,
       "numbers.anim.time"}
};

      // HasTimeChanged — time-edge detector: HasChanged(0/1) + DeltaTime, comparing this frame's clock
      // to last frame's by Mode. TiXL anim/time/HasTimeChanged.cs (stateful; outputs FIRST; nullptr eval).
      // Outputs FIRST: HasChanged, DeltaTime. .t3 defaults: WhichTime=LocalFxTime(1), Threshold=0,
      // Mode=DidChange(2). Bool HasChanged→Float 0/1 (Cut 32). FORKS (see step fn): the cook seam hands a
      // SINGLE clock (wall fx seconds), so WhichTime is an INERT parity port (all 4 targets read `time`);
      // Mode 3 (DidAdvancedWithMotionBlur) degrades to DidAdvanced as the __MotionBlurPass var is always
      // absent (no context-var map) — exactly TiXL's no-MB-pass else branch.
static const MathOp _reg_HasTimeChanged{
      {"HasTimeChanged", "HasTimeChanged",
       {{"HasChanged", "HasChanged", "Float", false},
        {"DeltaTime", "DeltaTime", "Float", false},
        {"WhichTime", "WhichTime", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
         {"LocalTime", "LocalFxTime", "GlobalTime", "GlobalFxTime"}},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"Mode", "Mode", "Float", true, 2.0f, 0.0f, 3.0f, Widget::Enum,
         {"DidRewind", "DidAdvanced", "DidChange", "DidAdvancedWithMotionBlur"}}},
       nullptr,
       "numbers.anim.time"}
};

      // StopWatch — run-clock stopwatch: Delta = elapsed since the last ResetTrigger rising edge,
      // LastDuration = the segment length captured at that reset. TiXL anim/time/StopWatch.cs (stateful;
      // outputs FIRST; nullptr eval — cooked by frame_cook's stateful-value seam). The clock is TiXL's
      // Playback.RunTimeInSecs (a process-lifetime wall run timer, NOT the playhead). DurationIn is a
      // compile-time Widget::Enum selector (TimeInSecs/BeatTime); BeatTime converts secs→bars via the
      // transport bpm (bars=secs*bpm/240). Bool ports (ResetTrigger/PauseWithPlayback) →Float 0/1 (Cut 32).
      // .t3 defaults: ResetTrigger=false, DurationIn=TimeInSecs(0), PauseWithPlayback=false.
      // FORKS (see step fn): R-1 run-clock origin (Σ wall dt from first cook; Delta baseline-invariant),
      // R-2 float-state precision over multi-hour absolute run time (same class as existing time ops).
static const MathOp _reg_StopWatch{
      {"StopWatch", "StopWatch",
       {{"Delta", "Delta", "Float", false},
        {"LastDuration", "LastDuration", "Float", false},
        {"ResetTrigger", "ResetTrigger", "Float", true, 0.0f, 0.0f, 1.0f},
        {"DurationIn", "DurationIn", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"TimeInSecs", "BeatTime"}},
        {"PauseWithPlayback", "PauseWithPlayback", "Float", true, 0.0f, 0.0f, 1.0f}},
       nullptr,
       "numbers.anim.time"}
};

      // --- transport-YELLOW consumers (Cut86 補縫): more ops on the StopWatch transport seam. ---
      // ConvertTime — bpm bars<->secs converter. TiXL anim/time/ConvertTime.cs (stateful in the cook
      // sense: evaluate==nullptr — its Result depends on the per-frame transport bpm the pure `in`-map
      // can't carry, like StopWatch). Result = Mode switch{BarsToSeconds=>time*240/bpm,
      // SecondsToBars=>time*bpm/240} (transport.h:37-38). Live bpm read (bpm=240 halves a BarsToSeconds).
      // The TiXL null-Playback IStatusProvider warning is DROPPED (no status system). .t3: Mode=0, Time=0.
static const MathOp _reg_ConvertTime{
      {"ConvertTime", "ConvertTime",
       {{"Result", "Result", "Float", false},
        {"Time", "Time", "Float", true, 0.0f, 0.0f, 100.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"BarsToSeconds", "SecondsToBars"}}},
       nullptr,
       "numbers.anim.time"}
};

      // RunTime — TimeInSeconds = Playback.RunTimeInSecs (the PROCESS-LIFETIME wall run clock, NOT the
      // playhead — keeps advancing while paused). TiXL anim/time/RunTime.cs (stateful; evaluate==nullptr;
      // no inputs). The seam carries the clock in TransportSnapshot::runTimeSecs. R-1 FORK: our run clock
      // is a wall-dt accumulator seeded at first cook (origin differs from TiXL's OS Stopwatch by the
      // launch→first-cook interval); RunTime is a pure exposure of it, value-parity exact on the seam.
static const MathOp _reg_RunTime{
      {"RunTime", "RunTime",
       {{"TimeInSeconds", "TimeInSeconds", "Float", false}},
       nullptr,
       "numbers.anim.time"}
};

      // ClipTime — Time = (float)context.LocalTime (the PLAYHEAD clock in BARS, raw = Playback.TimeInBars,
      // bpm-INVARIANT). TiXL anim/time/ClipTime.cs (stateful in the cook sense: 0 state but value depends
      // on the per-frame TransportSnapshot the pure evaluate() cannot carry → evaluate==nullptr, cooked by
      // frame_cook's stateful-value seam; step = stepClipTime passes through tr.localTimeBars). No inputs.
static const MathOp _reg_ClipTime{
      {"ClipTime", "ClipTime",
       {{"Time", "Time", "Float", false}},
       nullptr,
       "numbers.anim.time"}
};

      // LastFrameDuration — Duration = (float)Playback.LastFrameDuration (the raw wall frame delta in
      // seconds). TiXL anim/time/LastFrameDuration.cs (transport-fed; evaluate==nullptr; step =
      // stepLastFrameDuration reads the cook's `dt` = Playback.LastFrameDuration). 0 state, no inputs.
static const MathOp _reg_LastFrameDuration{
      {"LastFrameDuration", "LastFrameDuration",
       {{"Duration", "Duration", "Float", false}},
       nullptr,
       "numbers.anim.time"}
};

      // GetBpm — Result = (float)Playback.Current.Bpm (the live tempo). TiXL anim/vj/GetBpm.cs
      // (transport-fed; evaluate==nullptr; step = stepGetBpm reads tr.bpm). 0 state, no inputs.
      // The TiXL null-Playback warning is dropped (no status system; seam always supplies tr.bpm>0).
static const MathOp _reg_GetBpm{
      {"GetBpm", "GetBpm",
       {{"Result", "Result", "Float", false}},
       nullptr,
       "numbers.anim.vj"}
};

      // GetFrameSpeedFactor (TiXL Lib/numbers/anim/time/GetFrameSpeedFactor.cs) — display/render-rate
      // ratio: Result = isValid ? FrameSpeedFactor : 1  (isValid = |val|>0.0001). TiXL sets
      // FrameSpeedFactor to Settings.FrameRate/60 in render-to-file mode (RenderTiming.cs:124); in
      // simple_world's interactive path tr.frameSpeedFactor is always 1.0. Stateful (evaluate==nullptr;
      // step reads tr.frameSpeedFactor). 0 state, no inputs.
static const MathOp _reg_GetFrameSpeedFactor{
      {"GetFrameSpeedFactor", "GetFrameSpeedFactor",
       {{"FrameSpeedFactor", "FrameSpeedFactor", "Float", false}},
       nullptr,
       "numbers.anim.time"}
};

      // DelayTriggerChange — TWO-EDGE change detector (NOT rising-edge WasTriggered): on ANY trigger
      // change it snapshots the change time + prior delayed output, then holds stateIfDelayed until
      // remainingTime=refTime-currentTime+delayDuration runs out. TiXL bool/process/DelayTriggerChange.cs
      // (stateful; evaluate==nullptr; 6 state floats). Outputs FIRST: DelayedTrigger(bool→Float 0/1),
      // RemainingTime. .t3 defaults: TimeMode=AppRunTime_InSecs(6), Mode=DelayTrue(0), DelayDuration=1,
      // Trigger=false. FAITHFUL first-second: DelayTrue holds true before any edge while currentTime<1
      // (s0 inits 0; remaining=0-currentTime+1>0) — not seeded away. F-1 FORK: our snapshot sets
      // playbackTimeBars==localTimeBars==playhead (frame_cook:210-212), so LocalTime_* and PlayTime_*
      // modes read the same clock here (TiXL's nested-time-remap divergence isn't modeled on flat graphs).
      // TimeMode is a compile-time Widget::Enum selector over the 7 snapshot clocks (bars/secs ×
      // LocalFx/Local/Play + AppRunTime); bars→secs = bars*240/bpm (transport.h). Trigger bool→Float 0/1.
static const MathOp _reg_DelayTriggerChange{
      {"DelayTriggerChange", "DelayTriggerChange",
       {{"DelayedTrigger", "DelayedTrigger", "Float", false},
        {"RemainingTime", "RemainingTime", "Float", false},
        {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f},
        {"DelayDuration", "DelayDuration", "Float", true, 1.0f, 0.0f, 100.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"DelayTrue", "DelayFalse", "DelayBoth"}},
        {"TimeMode", "TimeMode", "Float", true, 6.0f, 0.0f, 6.0f, Widget::Enum,
         {"LocalFxTime_InBars", "LocalFxTime_InSecs", "LocalTime_InBars", "LocalTime_InSecs",
          "PlayTime_InBars", "PlayTime_InSecs", "AppRunTime_InSecs"}}},
       nullptr,
       "numbers.bool.process"}
};

}  // namespace
}  // namespace sw
