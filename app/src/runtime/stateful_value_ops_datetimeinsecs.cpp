// runtime/stateful_value_ops_datetimeinsecs — DateTimeInSecs (batch: keyframe-anim lane).
//
// TiXL authority: Operators/Lib/numbers/anim/time/DateTimeInSecs.cs
//   Update (cs:14-24):
//     if (!Freeze) _lastValue = (int)DateTimeOffset.Now.ToUnixTimeSeconds();
//     Result = _lastValue;
//   Emits the current Unix wall-clock second as an int; Freeze LATCHES the last sampled value.
//
// This is a WALL-CLOCK node (the ONE the anim family has). It has no pure evaluate() — the value comes
// from the OS clock + a cross-frame latch (_lastValue) — so it rides frame_cook's stateful-value seam
// like the rest of this family. The STATEFUL behavior worth a parity golden is the Freeze LATCH, not the
// absolute wall value (which is non-deterministic by design). The golden drives a deterministic injected
// clock (dateTimeInSecsClockOverride) so the latch invariant (Freeze holds the prior sample) is testable.
//
// STATE: s[0] = _lastValue (the latched Unix second). init seeds it on the first cook.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>

#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

int g_dateTimeInSecsBug = 0;

// Deterministic clock seam for the golden: when >= 0, the op reads THIS injected Unix-second instead of
// the real OS clock (so the Freeze-latch invariant is testable without a wall-clock assertion). -1 (the
// production default) = read the real std::chrono system clock (= TiXL DateTimeOffset.Now.ToUnixTimeSeconds).
long long g_dateTimeInSecsClockOverride = -1;

long long nowUnixSeconds() {
  if (g_dateTimeInSecsClockOverride >= 0) return g_dateTimeInSecsClockOverride;
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

void stepDateTimeInSecs(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                        StatefulValueState& st, float out[3], const TransportSnapshot&,
                        ContextVarMap*, const std::string&) {
  const bool freeze = getIn(in, "Freeze", 0.0f) != 0.0f;

  int lastValue = (int)std::lround(st.s[0]);
  // On the first cook the latch is unseeded — TiXL's _lastValue field-inits to 0, but Freeze on the very
  // first frame would emit 0; sw seeds from the clock on the first cook so a Freeze-from-frame-1 latches a
  // real value (fork-datetime-first-frame-seed; observable only on the never-unfrozen frame-1 corner).
  if (!st.init) { lastValue = (int)nowUnixSeconds(); st.init = true; }

  // bug 1: IGNORE the Freeze gate → re-sample the clock every cook even when frozen (the dead-gate defect).
  if (!freeze || g_dateTimeInSecsBug == 1) lastValue = (int)nowUnixSeconds();

  st.s[0] = (float)lastValue;
  out[0] = (float)lastValue;  // Result (int → Float carry)
}

}  // namespace

void setDateTimeInSecsBug(int mode) { g_dateTimeInSecsBug = mode; }

// Golden-only deterministic-clock seam (decl in stateful_value_ops.h). Setting it to a fixed second
// makes the Freeze-latch invariant deterministic.
void setDateTimeInSecsClockOverride(long long unixSeconds) { g_dateTimeInSecsClockOverride = unixSeconds; }

static const StatefulOpReg _reg_DateTimeInSecs{"DateTimeInSecs", stepDateTimeInSecs};

}  // namespace sw
