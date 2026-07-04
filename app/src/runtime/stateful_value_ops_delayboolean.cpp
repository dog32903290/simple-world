// runtime/stateful_value_ops_delayboolean — DelayBoolean: a bool trigger delayed by N frames through a
// FIFO queue. TiXL Lib/numbers/bool/process/DelayBoolean.cs.
//
// Stateful in the cook sense (evaluate==nullptr): its output is a PRIOR frame's Trigger, so it keeps a
// cross-frame Queue<bool> (DelayBoolean.cs:47). frame_cook (cookStatefulValueNodes) drives it once per
// frame; evalResidentFloat then returns extOut[0] for the no-evaluate node.
//
//   DelayBoolean.cs Update():                                                          (cs:17-44)
//     if (Math.Abs(context.LocalFxTime - _lastUpdateTime) < 0.001f) return;            // cs:19  sub-frame guard
//     _lastUpdateTime = context.LocalFxTime;                                           // cs:22
//     var frameCount = FrameCount.GetValue(context).Clamp(0, 500);                     // cs:23
//     var current    = Trigger.GetValue(context);                                      // cs:24
//     var result = false;                                                              // cs:26
//     if (frameCount == 0) { _queue.Clear(); result = current; }                       // cs:27-31
//     else {
//         while (_queue.Count > frameCount) { result = _queue.Dequeue(); }             // cs:34-37  drain excess (oldest first)
//         _queue.Enqueue(current);                                                     // cs:39     push newest
//     }
//     DelayedTrigger.Value = result;                                                   // cs:42
//
//   Ports (DelayBoolean.cs:49-53): Trigger = InputSlot<bool>, FrameCount = InputSlot<int>.
//   Output: DelayedTrigger = Slot<bool> (cs:9-10, DirtyFlagTrigger.Animated).
//   .t3 DefaultValue (DelayBoolean.t3): Trigger = false, FrameCount = 0.
//
// THE DELAY (the world-view): for frameCount>0 the queue holds up to frameCount+1 entries; once full,
// each frame dequeues the OLDEST (that becomes the result) then enqueues the newest. So the output at
// frame N equals the Trigger from frame N-(frameCount+1); before the queue fills, result stays the
// initial false. HAND-TRACE frameCount=2, Trigger seq [1,0,0,1,0]:
//     t0: q=[]      → enqueue 1 → q=[1]      result=false (initial)
//     t1: q=[1]     → enqueue 0 → q=[1,0]    result=false
//     t2: q=[1,0]   → enqueue 0 → q=[1,0,0]  result=false
//     t3: q=[1,0,0] → 3>2 dequeue 1(→result=1) → q=[0,0] → enqueue 1 → q=[0,0,1]  result=1  (= t0 Trigger)
//     t4: q=[0,0,1] → 3>2 dequeue 0(→result=0) → q=[0,1] → enqueue 0 → q=[0,1,0]  result=0  (= t1 Trigger)
//   So DelayedTrigger delays by frameCount+1 frames. The t3/t4 results are ONLY correct if the queue
//   PERSISTED across frames — that persistence is the whole point of the op.
//
// STATE: the queue lives in StatefulValueState::boolQueue (a bounded FIFO ring, 512 entries — covers the
//   Clamp(0,500)+1 max depth). boolQHead = oldest live index, boolQCount = live entries. Added additively
//   to StatefulValueState (only DelayBoolean touches it; every other op is byte-identical).
//
// FORKS (named):
//   • fork-delayboolean-drop-subframe-guard: the `Math.Abs(LocalFxTime - _lastUpdateTime) < 0.001f`
//     early-return (cs:19) is DROPPED. frame_cook cooks each node EXACTLY ONCE per frame (the Damp /
//     Spring / WasTrigger / change-detector precedent — every stateful leaf drops its own once-per-frame
//     guard for the same reason). No _lastUpdateTime stored. This is faithful given the once-per-frame
//     cook contract: TiXL's guard only suppresses SUB-frame double-evals, which cannot occur here.
//   • fork-delayboolean-int-bool-dissolve (Cut 32, runtime-wide): FrameCount (int) rides a Float param
//     (rounded toward zero via truncation); Trigger (bool) reads `!= 0`; DelayedTrigger (bool) emits
//     1.0/0.0. No Int/Bool port type on this rail.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>  // std::floor
#include <cstdint>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {

// DelayBoolean TEETH hook (--selftest-delayboolean). 0 = production; 1 = DROP the queue persistence
// (enqueue/dequeue on a FRESH empty queue each frame → the delay never materializes, result tracks the
// initial/immediate value → the delayed-output assertion at t3/t4 goes RED). Sticky module switch (the
// golden flips it around the REAL cook then resets), mirrors g_wasTriggerBug.
namespace { int g_delayBooleanBug = 0; }

namespace {

// stepDelayBoolean: TiXL DelayBoolean.cs Update() (verbatim; sub-frame guard dropped per fork).
void stepDelayBoolean(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                      StatefulValueState& st, float out[8], const TransportSnapshot&, ContextVarMap*,
                      const std::string&) {
  // cs:23 — FrameCount.Clamp(0, 500). Int dissolve: truncate toward zero over the non-negative range.
  int frameCount = (int)std::floor(getIn(in, "FrameCount", 0.0f));  // .t3 default 0
  if (frameCount < 0) frameCount = 0;
  if (frameCount > 500) frameCount = 500;
  const bool current = getIn(in, "Trigger", 0.0f) != 0.0f;  // cs:24, .t3 default false (bool-as-float)

  // -bug 1: run the enqueue/dequeue on a FRESH scratch queue (no persistence) → the cross-frame delay
  // vanishes; result collapses to current (frameCount==0-like) or initial false, diverging at t3/t4.
  const bool severed = (g_delayBooleanBug == 1);
  std::array<uint8_t, 512> scratch{};
  uint16_t head, count;
  std::array<uint8_t, 512>& q = severed ? scratch : st.boolQueue;
  if (severed) { head = 0; count = 0; }
  else { head = st.boolQHead; count = st.boolQCount; }

  bool result = false;  // cs:26
  if (frameCount == 0) {
    // cs:27-31 — clear the queue, pass the current value straight through.
    head = 0; count = 0;
    result = current;
  } else {
    // cs:34-37 — while (count > frameCount) dequeue the OLDEST (front), that value becomes `result`.
    while (count > frameCount) {
      result = (q[head] != 0);            // Dequeue() → result
      head = (uint16_t)((head + 1) % 512);
      --count;
    }
    // cs:39 — Enqueue(current) at the tail.
    const uint16_t tail = (uint16_t)((head + count) % 512);
    q[tail] = current ? 1u : 0u;
    ++count;
  }

  // Persist the ring back (skipped for the severed -bug leg so the fresh scratch is discarded → the
  // delay never accumulates across frames).
  if (!severed) { st.boolQHead = head; st.boolQCount = count; }

  out[0] = result ? 1.0f : 0.0f;  // cs:42 — DelayedTrigger.Value
}

}  // namespace

void setDelayBooleanBug(int mode) { g_delayBooleanBug = mode; }

static const StatefulOpReg _reg_DelayBoolean{"DelayBoolean", stepDelayBoolean};

}  // namespace sw
