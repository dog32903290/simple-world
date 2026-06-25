// runtime/stateful_value_ops_time — Time and GetFrameSpeedFactor transport-YELLOW consumers.
//   Time         — TiXL Lib/numbers/anim/time/Time.cs (5-mode playhead/wall/runtime/frozen +
//                  SpeedFactor multiplier + Units toggle Bars/Secs)
//   GetFrameSpeedFactor — TiXL Lib/numbers/anim/time/GetFrameSpeedFactor.cs (display/render-rate
//                  ratio; default 1 in simple_world's interactive mode)
//
// Both ops consume per-frame TransportSnapshot scalars that the pure evaluate()/`in`-map cannot
// carry — the same reason ConvertTime/RunTime/ClipTime live in the stateful table (0 per-instance
// memory, but value depends on the frame's transport state). Zero state each.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

// --- Time (TiXL Lib/numbers/anim/time/Time.cs) — multi-mode time exposure with SpeedFactor.
//
// TiXL Time.cs Update():
//   var time = timeMode switch {
//     LocalIdleMotionFxTime => contextLocalFxTime,  // 0 (default)
//     LocalTime             => contextLocalTime,     // 1
//     PlaybackTime          => Playback.TimeInBars,  // 2
//     Runtime               => BarsFromSeconds(Playback.RunTimeInSecs),  // 3
//     Frozen                => 0,                    // 4
//   };
//   if (Units == 1 /*Secs*/)
//     out = SecondsFromBars(time * speedFactor);  // bars*speedFactor → seconds
//   else
//     out = time * speedFactor;                   // bare bars (or frozen 0)
//
// Transport seam mappings (TransportSnapshot fields):
//   contextLocalFxTime  = tr.localFxTimeBars  (= context.LocalFxTime = Playback.FxTimeInBars)
//   contextLocalTime    = tr.localTimeBars     (= context.LocalTime   = Playback.TimeInBars, raw)
//   Playback.TimeInBars = tr.playbackTimeBars  (= Playback.TimeInBars, same playhead)
//   RunTimeInSecs → BarsFromSeconds(RunTimeInSecs) = tr.runTimeSecs * bpm / 240
//   SecondsFromBars(x) = x * 240 / bpm  (transport.h:38)
//
// .t3 defaults: Mode=0 (LocalIdleMotionFxTime), Units=0 (Bars), SpeedFactor=1.0.
// Output: single float "Timefloat" (TiXL C# field name → op output slot name in sw spec).
//
// DirtyFlag.Trigger management (frozen mode → DirtyFlagTrigger.None / thaw → Animated): NOT ported.
// simple_world marks every transport-fed op as isLiveSource=true (resident_eval_cache.cpp:28 via
// opDeclaresLiveOutput) — no per-frame dirty flag toggling needed (the cook is always called).
//
// 0 state (stateless in per-instance memory), but lives in the stateful table because its value
// depends on the per-frame TransportSnapshot the pure evaluate()  cannot carry.
//
// FORK (named): R-1 run-clock origin — TiXL's Runtime mode reads Playback.RunTimeInSecs (an OS
// Stopwatch started at process launch); our tr.runTimeSecs is a wall-dt accumulator seeded at first
// cook (frame_cook.cpp). The baseline differs by the launch→first-cook interval, but the golden
// drives the accumulator directly so the seam value is exactly faithful on the value exposed.
void stepTime(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
              StatefulValueState&, float out[8], const TransportSnapshot& tr, ContextVarMap*,
              const std::string&) {
  const int  mode        = (int)std::lround(getIn(in, "Mode",        0.0f));  // .t3 0=LocalIdleMotionFxTime
  const float speedFactor =                  getIn(in, "SpeedFactor", 1.0f);  // .t3 1.0
  const int  units       = (int)std::lround(getIn(in, "Units",       0.0f));  // .t3 0=Bars

  double time = 0.0;
  switch (mode) {
    case 0: time = tr.localFxTimeBars;                      break;  // LocalIdleMotionFxTime
    case 1: time = tr.localTimeBars;                         break;  // LocalTime
    case 2: time = tr.playbackTimeBars;                      break;  // PlaybackTime
    case 3: time = tr.runTimeSecs * tr.bpm / 240.0;          break;  // Runtime = BarsFromSeconds(RunTimeInSecs)
    case 4: time = 0.0;                                      break;  // Frozen
    default: time = 0.0;                                     break;
  }

  if (units == 1) {
    // Secs: SecondsFromBars(time * speedFactor) = time * speedFactor * 240 / bpm
    out[0] = (float)(time * speedFactor * 240.0 / tr.bpm);
  } else {
    // Bars (default): time * speedFactor
    out[0] = (float)(time * speedFactor);
  }
}

// --- GetFrameSpeedFactor (TiXL Lib/numbers/anim/time/GetFrameSpeedFactor.cs)
//   Result = isValid ? FrameSpeedFactor : 1  where isValid = |FrameSpeedFactor| > 0.0001.
//   FrameSpeedFactor = TiXL Playback.FrameSpeedFactor — a display/render-rate ratio set by
//   RenderTiming.cs:124 (`Settings.FrameRate / 60.0f`) in render-to-file mode; default is 1.0.
//   In simple_world (interactive, no render-to-file path) tr.frameSpeedFactor is always 1.0,
//   so this op faithfully outputs 1.0 every frame. The isValid guard (return 1 if |val|<0.0001)
//   matches TiXL:17-19 verbatim and protects simulation ops from a zero/near-zero factor.
//   0 inputs, 0 state.
void stepGetFrameSpeedFactor(const std::map<std::string, float>& /*in*/, float /*dt*/, float /*time*/,
                             StatefulValueState&, float out[8], const TransportSnapshot& tr,
                             ContextVarMap*, const std::string&) {
  const bool isValid = std::fabs(tr.frameSpeedFactor) > 0.0001;
  out[0] = isValid ? (float)tr.frameSpeedFactor : 1.0f;
}

}  // namespace

static const StatefulOpReg _reg_Time{"Time", stepTime};
static const StatefulOpReg _reg_GetFrameSpeedFactor{"GetFrameSpeedFactor", stepGetFrameSpeedFactor};

}  // namespace sw
