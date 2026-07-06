// runtime/point_ops_settime — SetTime: the Command-rail SUBTREE-TIME scope (TiXL numbers/anim/time/
// SetTime.cs:18-49) + the shared LiveTimeScope both cook drivers push around the SubTree cook.
//
// TiXL semantics (SetTime.cs, the SubTree.HasInputConnections branch :23-43):
//   prevLocal = context.LocalTime; prevFx = context.LocalFxTime;
//   if (mode == Absolute) { LocalTime = newTime; LocalFxTime = newTime; }       // :28-32
//   else                  { LocalTime += newTime; LocalFxTime += newTime; }      // :33-37 (Relative AND
//                                                                                //  GlobalAbsolute-with-subtree)
//   Result = SubTree.GetValue(context);  LocalTime = prevLocal; LocalFxTime = prevFx;   // :40-42
//
// MECHANISM (Arm A, 裁定 2026-07-05): a thread_local scope — the EXACT S3b LiveCtxVarScope shape
// (point_ops_setvarcmd.h) — pushed by BOTH cook drivers around the SubTree cook, consulted by the value-rail
// readers (flat graph.cpp evalFloat's evaluate() call; resident resident_eval_graph.cpp's transient-ec build
// + sampleAutomation). Symmetric on both legs (the S2c mirror law); NOT a per-leg rc-mutation.
//
// The scope stores only {absolute, newTime, prev} and the readers COMPOSE the chain against their own
// ambient clock: effective(ambient) = absolute ? newTime : effective_prev(ambient) + newTime. Absolute cuts
// the chain, Relative adds onto the enclosing scope — exactly TiXL's stacked context mutations. No ambient
// clock is threaded into the drivers.
//
// NAMED FORKS:
// • fork-settime-flat-fxclock-only: TiXL scopes BOTH LocalTime and LocalFxTime. sw's flat leg reads time off
//   the byte-locked 16-byte GPU EvaluationContext, which has NO localTime field (localTime = the playhead is
//   a resident-only concept, ResidentEvalCtx). So the flat leg scopes ONLY localFxTime (the anim/oscillate/
//   perlin clock); the resident leg scopes localFxTime AND localTime (automation sampling via
//   sampleAutomation). ctx.time (the flat SECONDS clock) is not a TiXL Local* clock and stays untouched.
// • fork-settime-globalabsolute-unscoped-unsupported: TiXL's GlobalAbsolute WITHOUT a SubTree writes the
//   context clocks globally with NO restore (SetTime.cs:44-48). sw rebuilds the eval clocks from the
//   Transport every frame, so an unrestored write has no frame-persistent home; the no-subtree global write
//   is not supported. WITH a subtree, GlobalAbsolute behaves like Relative (the .cs else-branch), faithful.
#pragma once
#include <map>
#include <string>

namespace sw {

// One resolved SetTime push: how this scope transforms an ambient clock. inactive => the driver pushed
// nothing (non-SetTime node / -bug) and the enclosing scope (or the raw ambient) stays in effect.
struct SetTimeScopeSpec {
  bool active = false;
  bool absolute = false;   // OffsetMode 0 = Absolute (set); 1/2 = Relative/GlobalAbsolute (add, .cs:33-37)
  float newTime = 0.0f;    // the NewTime input (bars — the TiXL Local*/FxTime unit)
};

// Resolve one SetTime node's params into a scope spec (params: "NewTime", "OffsetMode"). Always active
// (the caller gates on opType + -bug); mode trunc-toward-0 like the C# enum cast.
SetTimeScopeSpec resolveSetTimeScope(const std::map<std::string, float>& params);

// RAII scope guard (mirror of LiveCtxVarScope): construct around the SubTree cook in the driver's Command
// branch; nests (saves + restores the enclosing scope). An inactive spec leaves the prior chain untouched.
struct LiveTimeScope {
  explicit LiveTimeScope(const SetTimeScopeSpec& spec);
  ~LiveTimeScope();
  LiveTimeScope(const LiveTimeScope&) = delete;
  LiveTimeScope& operator=(const LiveTimeScope&) = delete;
 private:
  const void* prev_;
  bool engaged_;
};

// A TimeClip REMAP push onto the SAME time-scope chain (TiXL TimeClipSlot remaps LocalFxTime by an
// unclamped linear map). Distinct chain-node kind from SetTime's offset: composes with SetTime and other
// TimeClips (scopedTimeOr walks both kinds). TimeClip (point_ops_timeclip) owns the semantic wrapper; this
// is the shared chain primitive so the two time-scoping ops live in ONE stack, not two competing ones.
struct TimeRemapScopeSpec {
  bool active = false;
  float inMin = 0.0f, inMax = 0.0f;    // TimeRange.Start / End
  float outMin = 0.0f, outMax = 0.0f;  // SourceRange.Start / End
};

// RAII guard pushing a remap node onto the time-scope chain (mirror of LiveTimeScope). Inactive = no-op.
struct LiveTimeRemapScope {
  explicit LiveTimeRemapScope(const TimeRemapScopeSpec& spec);
  ~LiveTimeRemapScope();
  LiveTimeRemapScope(const LiveTimeRemapScope&) = delete;
  LiveTimeRemapScope& operator=(const LiveTimeRemapScope&) = delete;
 private:
  const void* prev_;
  bool engaged_;
};

// True iff ANY SetTime scope is active on this thread. The cook drivers' nodeParams memos consult this
// (alongside liveCtxVars()) to resolve FRESH and UNCACHED while a scope is live — a time-driven param
// resolved under the scope is ambient-dependent, not a graph property (the S3b memo law).
bool liveTimeScopeActive();

// Compose the active scope chain against `ambient` (a bars clock): innermost-out, Absolute cuts the chain,
// Relative adds. No scope active => `ambient` unchanged. The readers call this:
//   • flat evalFloat: scoped 16-byte ctx.localFxTime before evaluate()      (fx clock, both legs)
//   • resident evalResidentFloat's transient-ec build: ec.time/ec.localFxTime (fx clock)
//   • resident sampleAutomation: the curve sample position                   (localTime, resident-only)
float scopedTimeOr(float ambient);

// True iff opType is the SetTime scope writer (the drivers' Command branch consults this).
bool isSetTimeScopeWriter(const std::string& opType);

// -bug DRIVER flag (mirror of setVarBugSkipWrite): when true, BOTH cook legs SKIP the SetTime push —
// a probe inside the SubTree reads the UNSCOPED ambient clock, biting the golden RED on both legs.
bool& setTimeBugSkipPush();

// Command-op registrar (SubTree Command in → Command out; forwards the cooked subtree items — the time
// effect is realized in the driver's push, like SetRequestedResolution / SetFloatVarCmd).
void registerSetTimeOp();

// --selftest-settime (HARD GATE, both legs × Absolute/Relative): a value-rail fx-time probe drives a stamp
// op INSIDE the SubTree (scoped) and a sibling stamp OUTSIDE (unscoped restore proof). injectBug skips the
// push → the inside stamp reads the ambient clock → RED.
int runSetTimeSelfTest(bool injectBug);

}  // namespace sw
