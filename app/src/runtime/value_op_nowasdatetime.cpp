// NowAsDateTime value op (value-op self-registration seam leaf — DateTime route B PRODUCER #1).
// TiXL authority: Operators/Lib/string/datetime/NowAsDateTime.cs (verbatim below, 18 lines total):
//
//   NowAsDateTime.cs Update():
//     Output.Value = DateTime.Now;          // :17 — the whole op
//   Ports: none. Output = Slot<DateTime>.  NowAsDateTime.t3: Inputs [] (confirmed, re-read).
//
// ROUTE B (DateTime-as-epoch-Float, pinned in value_op_datetimetofloat.cpp): the DateTime rides a
// Float port as UTC Unix-epoch SECONDS. This op is the first of the two deferred PRODUCERS that
// leaf names ("NowAsDateTime / StringToDateTime — deferred") — it emits hostNowEpochSeconds()
// (runtime/datetime_host.h, the family's ONE wall-clock read with the goldens' determinism override).
//
// FORKS (named):
//   - fork-datetime-epoch-as-float: the epoch rides a Float port (24-bit mantissa) → a present-day
//     epoch (~1.77e9 s) quantizes to ~128 s steps in the CARRIER. The op MATH is exact; downstream
//     DateTimeToFloat field reads near the present are coarse (the pinned route-B limit, named in
//     that leaf). The golden pins float-EXACT epochs so the carrier never rounds.
//   - fork-datetime-utc-not-local: TiXL DateTime.Now is LOCAL-naive; route B emits UTC epoch
//     (datetime_host family fork — DateTimeToFloat.HourOffset is the zone knob).
//   - fork-wallclock-not-transport: TiXL reads DateTime.Now — the WALL clock, not Playback/LocalFxTime.
//     Faithful: this op does NOT touch sw's transport clock (the central time spine); it reads the
//     same wall clock TiXL does, via the datetime_host seam. (The work-order clock question, answered
//     from the .cs: wall clock, so no transport wiring.)
//   - fork-no-dirtyflag-trigger: TiXL's Output has no DirtyFlagTrigger.Animated (NowAsDateTime.cs:6)
//     — it re-evaluates when pulled. sw's value-eval pulls every frame → same observable behavior.
//
// TEETH hook (file-local; 0 = production, set ONLY by the --selftest-nowasdatetime golden via
// setNowAsDateTimeBug): 1 = DROP the wall-clock read (emit 0.0 instead of the epoch) — a REAL
// defect on the actual evaluate path (the golden's fixed expected epochs bite; no want-flip).
#include "runtime/datetime_host.h"      // hostNowEpochSeconds / setHostNowOverrideForTest (determinism seam)
#include "runtime/graph.h"              // NodeSpec, findSpec/evalFloat/pinId (golden)
#include "runtime/Particle.h"           // EvaluationContext full definition (golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

#include <cmath>
#include <cstdio>

namespace sw {

int runNowAsDateTimeSelfTest(bool injectBug);

namespace {

int g_nowAsDateTimeBug = 0;  // teeth hook: 1 = drop the wall-clock read (real defect)

// No inputs (NowAsDateTime.t3 Inputs: []); output = UTC epoch seconds now (route B DateTime).
float evalNowAsDateTime(int /*outIdx*/, const float* /*in*/, int /*n*/, const EvaluationContext&) {
  if (g_nowAsDateTimeBug == 1) return 0.0f;  // bug: the clock read is dropped (golden goes RED)
  return static_cast<float>(hostNowEpochSeconds());
}

}  // namespace

void setNowAsDateTimeBug(int mode) { g_nowAsDateTimeBug = mode; }

// Self-registration. File-scope static ValueOp — CMake globs value_op*.cpp; no shared edit point.
static const ValueOp _reg_nowasdatetime{
    {"NowAsDateTime", "NowAsDateTime",
     {{"Output", "Output", "Float", false}},
     evalNowAsDateTime,
     "string.datetime"},
    "nowasdatetime", runNowAsDateTimeSelfTest};

// --- NowAsDateTime golden ------------------------------------------------------------------------
// Determinism: the wall clock is PINNED via setHostNowOverrideForTest (the datetime_host seam) — no
// wall-clock assertion (GOLDEN_STANDARD: 時間相關 probe 餵固定時間). Expected values are the pinned
// epochs themselves (NowAsDateTime.cs:17 is the identity `Output = now`; the op has no other math),
// chosen float-EXACT (multiples of large powers of two) so the Float carrier never rounds:
//   G1: 1610612736 = 1.5 * 2^30  (2021-01-14T08:25:36Z — a present-scale epoch, exact in float)
//   G2: 1048576    = 2^20        (1970-01-13T03:16:16Z — a small epoch, exact in float)
// PROBE POSITION: G1 sits at present-day scale (发散中段 — a dropped clock read or a carrier bug
// diverges by 1.6e9); G2 proves the op tracks the seam (two DIFFERENT pins → two different outputs,
// killing any hardcoded-constant impl).
// injectBug: setNowAsDateTimeBug(1) DROPS the clock read on the REAL evaluate path → both probes
// read 0.0 → RED. Did-not-trip → return 0 (--bite NO-BITE list catches a dead tooth).
int runNowAsDateTimeSelfTest(bool injectBug) {
  const NodeSpec* spec = findSpec("NowAsDateTime");
  if (!spec) {
    std::printf("[selftest-nowasdatetime] FAIL: spec not registered\n");
    return 1;
  }
  int outIdx = -1;
  for (size_t i = 0; i < spec->ports.size(); ++i)
    if (spec->ports[i].id == "Output") outIdx = static_cast<int>(i);
  if (outIdx < 0) {
    std::printf("[selftest-nowasdatetime] FAIL: no Output port\n");
    return 1;
  }

  setNowAsDateTimeBug(injectBug ? 1 : 0);

  const double pins[2] = {1610612736.0, 1048576.0};  // float-exact epochs (see header comment)
  bool ok = true;
  for (int k = 0; k < 2; ++k) {
    setHostNowOverrideForTest(pins[k]);
    Graph g;
    Node nd;
    nd.id = g.nextId++;
    nd.type = "NowAsDateTime";
    g.nodes.push_back(nd);
    EvaluationContext ctx{};
    const float got = evalFloat(g, pinId(nd.id, outIdx), ctx, 0);
    const float want = static_cast<float>(pins[k]);
    const bool pass = (got == want);  // both sides float-exact — no epsilon needed
    ok = ok && pass;
    std::printf("[selftest-nowasdatetime] G%d pinned-now=%.1f got=%.1f want=%.1f -> %s\n", k + 1,
                pins[k], static_cast<double>(got), static_cast<double>(want),
                pass ? "PASS" : "FAIL");
  }

  // Hygiene: restore production behavior (clear the pin + the tooth).
  setHostNowOverrideForTest(-1.0);
  setNowAsDateTimeBug(0);

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-nowasdatetime] injectBug did NOT trip (clock-drop tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-nowasdatetime] injectBug correctly RED (clock read dropped → 0.0)\n");
    return 1;
  }
  std::printf("[selftest-nowasdatetime] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
