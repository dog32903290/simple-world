// runtime/point_ops_timeclip — TimeClip: the Command-rail TIMELINE-WINDOW scope (TiXL
// Core/Operator/Slots/TimeClipSlot.cs:52-83 UpdateWithTimeRangeCheck + flow/TimeClip.cs). The
// per-instance authored twin of SetTime's param scope (point_ops_settime.h): where SetTime resolves
// {Absolute/Relative, NewTime} from INPUT PARAMS, TimeClip resolves its window + source mapping from the
// child's AUTHORED OutputData (ClipTimeData, projected onto ResidentNode::clipOut).
//
// TiXL semantics (TimeClipSlot.UpdateWithTimeRangeCheck — the SLOT wraps ANY TimeClipSlot<T> op):
//   if (LocalTime < TimeRange.Start || LocalTime >= TimeRange.End) return;          // :54-58 WINDOW GATE
//   prevTime = LocalTime; prevFx = LocalFxTime;                                     // :60-61
//   LocalTime   = Remap(prevTime, TimeRange.Start, End, SourceRange.Start, End);    // :64-65 (unclamped linear
//   LocalFxTime = Remap(prevFx,   TimeRange.Start, End, SourceRange.Start, End);    // :67-68  MathUtils.cs:368)
//   _baseUpdateAction(context);  LocalTime = prevTime; LocalFxTime = prevFx;        // :78-82 cook + restore
// Additionally the TimeClip OPERATOR publishes context.FloatVariables["_normalizedTime"] =
// (LocalFxTime_scoped - start)/(end - start) for downstream GetFloatVar (TimeClip.cs:19-22). We publish it
// into the ctxVars channel around the SubTree cook (the S3a var scope) so a scoped GetFloatVar reads it.
//
// MECHANISM (mirror of SetTime's LiveTimeScope, the S3b LiveCtxVarScope shape): a thread_local scope pushed
// by BOTH cook drivers around the SubTree cook, consulted by the value-rail readers via scopedTimeOr()
// (the SAME chain SetTime feeds — TimeClip and SetTime compose in the ONE time-scope stack, so a TimeClip
// inside a SetTime inside a TimeClip remaps correctly). A TimeClip push is a REMAP node
// (factor+offset), distinct from SetTime's Absolute/Relative offset node; scopedTimeOr walks both kinds.
//
// GATE OUTCOME: out-of-window → the driver contributes NO items for the SubTree (empty RenderCommand),
// exactly like TimeClipSlot returns before calling _baseUpdateAction. In-window → the SubTree cooks under
// the remapped fx clock. The gate reads the AMBIENT time (composed through any enclosing scope) so a
// TimeClip nested under a SetTime gates against the outer-transformed clock — faithful to LocalTime being
// the same mutated context field TiXL checks.
//
// NAMED FORKS:
// • fork-timeclip-fxclock-gate: TiXL gates + remaps on LocalTime (the playhead) AND LocalFxTime. sw threads
//   only the 16-byte GPU EvaluationContext (localFxTime, no localTime) into BOTH cook legs' command cook —
//   the playhead lives on ResidentEvalCtx and is NOT threaded into the deep command-cook fn (same shape as
//   SetTime's fork-settime-flat-fxclock-only, and sampleAutomation being the sole playhead reader). So BOTH
//   legs gate AND remap on localFxTime. The gate DECISION matches TiXL exactly whenever localTime==localFxTime
//   (playback); scrub-time divergence (localTime≠localFxTime) is deferred with playhead-into-command-cook
//   threading. At the golden's ambient (localTime==localFxTime) both legs gate identically. Documented, not silent.
// • fork-timeclip-normalizedtime-ctxvar: TiXL writes _normalizedTime into context.FloatVariables (a Float
//   context var). sw publishes it into the SAME ctxVars map the S3a SetFloatVar scope uses, so a downstream
//   GetFloatVar("_normalizedTime") resolves it. Only the TimeClip OPERATOR publishes it (not the shared
//   slot gate) — a MidiClip's TimeClipSlot gates+remaps but does NOT publish _normalizedTime.
#pragma once
#include <string>

#include "runtime/compound_graph.h"  // ClipTimeData (the authored window + source mapping)

namespace sw {

struct ContextVarMap;  // stateful_value_ops.h — the ctx-var channel _normalizedTime is published into

// Resolved TimeClip push: the window to gate against + the remap (TimeRange→SourceRange). inactive => the
// driver pushed nothing (non-TimeClip node / no authored clip / -bug) and the enclosing scope stays.
struct TimeClipScopeSpec {
  bool active = false;
  float timeStart = 0.0f, timeEnd = 0.0f;      // TimeRange — the window gate bounds (+ remap inMin/inMax)
  float sourceStart = 0.0f, sourceEnd = 0.0f;  // SourceRange — the remap outMin/outMax
};

// Build a scope spec from an authored clip (the driver resolves it from ResidentNode::clipOut[outSlot]).
TimeClipScopeSpec resolveTimeClipScope(const ClipTimeData& clip);

// Unclamped linear remap (= TiXL MathUtils.cs:368-373 float Remap): factor=(v-inMin)/(inMax-inMin);
// return factor*(outMax-outMin)+outMin. Degenerate inMin==inMax → returns outMin (avoids /0; TiXL divides
// by zero → NaN, but a zero-width clip is gated out before remap ever runs, so this guard only affects a
// hand-built degenerate call). Exposed so the golden hand-computes the expected remap independently.
float timeClipRemap(float v, float inMin, float inMax, float outMin, float outMax);

// Is `ambient` (a bars clock) INSIDE the clip's window? = TiXL TimeClipSlot.cs:54 (NOT(< start) AND
// NOT(>= end)) → [start, end). The driver gates the SubTree cook on this; false → contribute nothing.
bool timeClipInWindow(const TimeClipScopeSpec& spec, float ambient);

// The normalized position of `ambient` within the clip window = (ambient - start)/(end - start)
// (= TiXL TimeClip.cs:21). Published as the _normalizedTime ctx var by the TimeClip OP's driver branch.
// Degenerate window → 0.
float timeClipNormalized(const TimeClipScopeSpec& spec, float ambient);

// True iff opType is the TimeClip OPERATOR (the driver's Command branch consults this to publish
// _normalizedTime; the shared window gate applies to ANY carrying op via clipOut, not just this type).
bool isTimeClipOp(const std::string& opType);

// The ctx-var name the TimeClip operator publishes its normalized position into (= TiXL "_normalizedTime").
const char* timeClipNormalizedVarName();

// Publish _normalizedTime = timeClipNormalized(spec, ambient) into `vars`->floatVars (= TiXL
// context.FloatVariables["_normalizedTime"] = normalizedTime, TimeClip.cs:22). No-op when vars is null. The
// TimeClip OPERATOR's driver branch calls this INSIDE the window (after the remap push) so a downstream
// GetFloatVar("_normalizedTime") cooked in the SubTree (under LiveCtxVarScope) reads it. Not restored — TiXL
// leaves the var set (it overwrites context.FloatVariables and the per-frame ctxVars reset clears it).
void timeClipPublishNormalized(const TimeClipScopeSpec& spec, float ambient, ContextVarMap* vars);

// FLAT-leg clip source (test seam). The flat Graph Node carries NO authored clip data — clips live on the
// nested SymbolChild / resident ResidentNode::clipOut (the production path). The flat leg exists ONLY as a
// golden test leg (compound_graph.h header: "the flat Graph survives only as a golden T1 test leg"). So the
// flat driver resolves a TimeClip node's window from THIS thread_local, which the golden sets before a flat
// cook. active=false (production default) → the flat driver never gates (no production flat TimeClip). This
// is NOT a production codepath; the resident leg reads real authored data off clipOut.
TimeClipScopeSpec& flatTimeClipTestScope();

// Command-op registrar (SubTree Command in → Command out; forwards the cooked subtree items — the window
// gate + time remap is realized in the driver, like SetTime / SetRequestedResolution).
void registerTimeClipOp();

// -bug DRIVER flag (mirror of setTimeBugSkipPush): when true BOTH cook legs SKIP the TimeClip gate+remap —
// the SubTree cooks UNSCOPED (out-of-window clips leak items; in-window clips read the un-remapped clock),
// biting the golden RED on both legs. Reset after the cook (process hygiene).
bool& timeClipBugSkipScope();

// --selftest-timeclip (HARD GATE, flat + resident): probes INSIDE (< / == / mid / >= / >) the window +
// the source remap mid-value + the normalized ctx-var. injectBug skips the scope → RED. Full topology doc
// in the .cpp.
int runTimeClipSelfTest(bool injectBug);

}  // namespace sw
