// runtime/stateful_value_ops_ctxvarlog_selftest — the context-var LogLevel/LogUpdates knob golden
// (--selftest-ctxvarlog). RED-FIRST proof that the param-completion fan-out's two new MappedType<LogLevels>
// enum inputs (SetIntVar.LogLevel / GetIntVar.LogUpdates) are BEHAVIOUR-BEARING — i.e. the knob genuinely
// GATES the diagnostic emission, not a dead inspector field.
//
// Cooks THROUGH the production value-rail cook path (cookStatefulValueOp — the SAME entry frame_cook.cpp:245
// uses), never a hand-poked step fn, so the NodeSpec-default→param flow is exercised end to end.
//
// TiXL ground truth (the gated emission this asserts):
//   SetIntVar.cs:30-36 — empty VariableName + logLevel >= Warnings(1) → Log.Warning. None(0) → silent.
//   GetIntVar.cs:30-48 — read MISS + logUpdates >= Warnings(1) → Log.Warning; read HIT + >= AllUpdates(3) →
//                        Log.Debug. None(0) → silent on both.
// sw routes those into ctxVarLogSink() (no editor pane; runtime-owned leaf counters, like LogMessage's
// logSink). FORK (named): GetIntVar's _complainedOnce once-latch is dropped (editor-noise de-dupe; no
// per-instance frame memory) — gating BY LEVEL is faithful, the once-suppression is not modelled.
//
// TEETH (each cooks through, asserts the sink counter against the closed-form TiXL gate):
//   T1 SetIntVar empty-name @ None        → warnings unchanged (silent)          [the suppress tooth]
//   T2 SetIntVar empty-name @ Warnings    → warnings +1                          [the emit tooth]
//   T3 GetIntVar undefined-read @ None    → warnings unchanged (silent)          [the suppress tooth]
//   T4 GetIntVar undefined-read @ Warnings→ warnings +1                          [the emit tooth]
//   T5 GetIntVar hit @ AllUpdates         → updates +1                           [hit-debug tooth]
//   T6 GetIntVar hit @ Changes(2)         → updates unchanged (< AllUpdates)     [level-threshold tooth]
// -bug (ctxVarLogBug): the level is IGNORED (always emit) → T1 & T3's "None suppresses" assertions go RED
//   (warnings increments where it must stay flat). Proves the knob actually controls the gate.
//
// runtime leaf: pure CPU (no Metal — cookStatefulValueOp is host-side). No UI, no upward deps.
#include <cstdio>
#include <map>
#include <string>

#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS
#include "runtime/stateful_value_ops.h"  // cookStatefulValueOp / ContextVarMap / ctxVarLogSink / ctxVarLogBug

namespace sw {
namespace {

// Cook one value-rail context-var op through the production entry. `in` = its resolved Float params (incl.
// LogLevel/LogUpdates); `vars` = the shared map; `varName` = the resolved String VariableName.
void cookOne(const char* opType, const std::map<std::string, float>& in, ContextVarMap* vars,
             const std::string& varName) {
  StatefulValueState st;
  float out[8] = {0};
  cookStatefulValueOp(opType, in, 1.0f / 60.0f, 0.0f, st, out, TransportSnapshot{}, vars, varName);
}

}  // namespace

int runCtxVarLogSelfTest(bool injectBug) {
  ctxVarLogBug() = injectBug;

  struct Check { const char* label; bool ok; int got; int want; };
  Check checks[6];
  int ci = 0;

  // T1 — SetIntVar empty name @ LogLevel=None(0): silent (warnings must NOT increment). SetIntVar.cs:32-33.
  {
    ctxVarLogSink() = CtxVarLogSink{};
    cookOne("SetIntVar", {{"Value", 0.0f}, {"LogLevel", 0.0f}}, nullptr, /*empty name*/ "");
    int got = ctxVarLogSink().warnings;
    checks[ci++] = {"T1 SetIntVar empty@None silent", got == 0, got, 0};
  }
  // T2 — SetIntVar empty name @ LogLevel=Warnings(1): emit (warnings +1). SetIntVar.cs:32-33.
  {
    ctxVarLogSink() = CtxVarLogSink{};
    cookOne("SetIntVar", {{"Value", 0.0f}, {"LogLevel", 1.0f}}, nullptr, "");
    int got = ctxVarLogSink().warnings;
    checks[ci++] = {"T2 SetIntVar empty@Warnings emit", got == 1, got, 1};
  }
  // T3 — GetIntVar undefined read @ LogUpdates=None(0): silent. GetIntVar.cs:41-48 (complain gated >=Warnings).
  {
    ctxVarLogSink() = CtxVarLogSink{};
    ContextVarMap vars;  // empty → "i" undefined → read miss
    cookOne("GetIntVar", {{"FallbackValue", 0.0f}, {"LogUpdates", 0.0f}}, &vars, "i");
    int got = ctxVarLogSink().warnings;
    checks[ci++] = {"T3 GetIntVar miss@None silent", got == 0, got, 0};
  }
  // T4 — GetIntVar undefined read @ LogUpdates=Warnings(1): emit (warnings +1). GetIntVar.cs:41-48.
  {
    ctxVarLogSink() = CtxVarLogSink{};
    ContextVarMap vars;
    cookOne("GetIntVar", {{"FallbackValue", 0.0f}, {"LogUpdates", 1.0f}}, &vars, "i");
    int got = ctxVarLogSink().warnings;
    checks[ci++] = {"T4 GetIntVar miss@Warnings emit", got == 1, got, 1};
  }
  // T5 — GetIntVar HIT @ LogUpdates=AllUpdates(3): debug emit (updates +1). GetIntVar.cs:34-35.
  {
    ctxVarLogSink() = CtxVarLogSink{};
    ContextVarMap vars; vars.intVars["i"] = 42;  // defined → read hit
    cookOne("GetIntVar", {{"FallbackValue", 0.0f}, {"LogUpdates", 3.0f}}, &vars, "i");
    int got = ctxVarLogSink().updates;
    checks[ci++] = {"T5 GetIntVar hit@AllUpdates debug", got == 1, got, 1};
  }
  // T6 — GetIntVar HIT @ LogUpdates=Changes(2): below AllUpdates → silent (updates unchanged). GetIntVar.cs:34.
  // (NOTE: the -bug forces level→AllUpdates, so this would ALSO go RED under the bug — a second tooth.)
  {
    ctxVarLogSink() = CtxVarLogSink{};
    ContextVarMap vars; vars.intVars["i"] = 42;
    cookOne("GetIntVar", {{"FallbackValue", 0.0f}, {"LogUpdates", 2.0f}}, &vars, "i");
    int got = ctxVarLogSink().updates;
    checks[ci++] = {"T6 GetIntVar hit@Changes silent", got == 0, got, 0};
  }

  ctxVarLogSink() = CtxVarLogSink{};  // process hygiene
  ctxVarLogBug() = false;

  bool all = true;
  for (const Check& c : checks) {
    std::printf("[selftest-ctxvarlog]   %-34s got=%d want=%d -> %s\n", c.label, c.got, c.want,
                c.ok ? "ok" : "RED");
    all = all && c.ok;
  }

  if (injectBug) {
    if (all) {
      std::printf("[selftest-ctxvarlog] FAIL: injectBug still passed — the level gate is not actually wired "
                  "(LogLevel/LogUpdates does not control emission)\n");
      return 1;
    }
    std::printf("[selftest-ctxvarlog] injectBug correctly RED — level ignored → None/Changes cases emitted "
                "anyway (T1/T3/T6 tripped: the knob does gate the emission)\n");
    return 1;
  }
  std::printf("[selftest-ctxvarlog] %s\n", all ? "PASS" : "FAIL");
  return all ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/336, {"ctxvarlog", runCtxVarLogSelfTest});

}  // namespace sw
