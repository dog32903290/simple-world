// runtime/stateful_value_ops — the stateful cook for value nodes whose output depends on PRIOR
// frames (Damp / Spring / Ease / ...), ported faithfully from TiXL Lib/numbers/.../process.
// These ops have NO pure evaluate() — their value cannot come from inputs alone, they keep
// per-instance memory across frames — so, exactly like AudioReaction, they are cooked once per
// frame by the app's frame_cook (cookStatefulValueNodes), which writes their outputs onto
// ResidentNode::extOut; evalResidentFloat then returns extOut[outputPortIndex] for them (the
// GENERIC no-evaluate path, resident_eval_graph.cpp:58-65 — no per-type branch needed).
//
// Data-driven (ARCHITECTURE rule 7): a new stateful op = (1) a step fn here + a row in
// kStatefulValueOps, (2) a NodeSpec row (evaluate=nullptr) in node_registry_math. frame_cook
// needs NO edit — it iterates generically via isStatefulValueOp / cookStatefulValueOp.
//
// runtime leaf: pure computation, no hardware, no UI.
#pragma once
#include <array>
#include <map>
#include <string>

namespace sw {

// Per-instance memory across frames (TiXL private fields). One per node instance, keyed upstream
// by the resident node PATH (survives projection rebuilds + stays per-instance inside compounds,
// like AudioReactionState). A generic 12-float scratch covers every stateful value op (incl. vec3):
//   Damp:       s[0]=dampedValue, s[1]=velocity
//   Spring:     s[0]=springedValue, s[1]=result (the previous output)
//   DampVecN:   s[0..N-1]=damped per component, s[N..2N-1]=velocity per component
//   SpringVecN: s[0..N-1]=springed per component, s[N..2N-1]=result per component
//   Ease:       s[0]=startTime, s[1]=initial, s[2]=target, s[3]=prevInput, s[4]=prevEased
//   EaseVec2:   s[0]=startTime, s[1..2]=initial.xy, s[3..4]=target.xy, s[5..6]=prevInput.xy, s[7]=prevEased
//   EaseVec3:   s[0]=startTime, s[1..3]=initial.xyz, s[4..6]=target.xyz, s[7..9]=prevInput.xyz, s[10]=prevEased
// (prevEased = last frame's eased-t, one shared scalar — reconstructs the prior Result on restart
//  since frame_cook hands a zeroed out[] each frame; see stateful_value_ops.cpp easeImpl.)
// `init` = TiXL's _isFirstEval: the first cook seeds state from the input (no smoothing yet).
// (Widened s[8]→s[12] in batch26 for EaseVec3's 10 floats of state; additive, zero behavior
// change for the s[0..3] ops above.)
struct StatefulValueState {
  float s[12] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  bool  init = false;
};

// Read-only per-frame transport snapshot (batch: playback-transport seam). A few stateful ops need
// playback scalars the per-port `in` map cannot carry — the playback run clock, the BPM for
// bars<->secs, and the playback speed (pause-detect). frame_cook fills this from the in-scope
// Transport once per frame (the ONLY frame_cook edit) and hands it to cookStatefulValueOp; the op
// reads it host-side. It must NEVER touch the 16-byte GPU-shared EvaluationContext (eval_context.h:39
// static_assert) — these are CPU value sims, not shader uniforms. Keeping it a plain POD here means
// runtime/ does not depend on the app-owned g_transport static (ARCHITECTURE dependency direction).
//   runTimeSecs  = TiXL Playback.RunTimeInSecs — a PROCESS-LIFETIME wall-clock run timer (a Stopwatch
//                  started at static init, Playback.cs:159), independent of the playhead/pause/rate.
//   rate         = TiXL context.Playback.PlaybackSpeed (0 = paused; pause-detect for StopWatch).
//   bpm          = TiXL Playback.Bpm — bars = secs*bpm/240 (transport.h:37, the bars<->secs authority).
//   localTimeBars/localFxTimeBars/playbackTimeBars = the two-clock playhead / wall clock / playhead
//                  (BARS) for any future transport-reading op; StopWatch uses only runTimeSecs/rate/bpm.
struct TransportSnapshot {
  double runTimeSecs = 0.0;
  double localTimeBars = 0.0;
  double localFxTimeBars = 0.0;
  double playbackTimeBars = 0.0;
  double bpm = 120.0;
  double rate = 1.0;
  // frameSpeedFactor = TiXL Playback.FrameSpeedFactor — a display/render-rate ratio (default 1.0).
  // Set to Settings.FrameRate/60 only in render-to-file mode (TiXL Editor/RenderExport/RenderTiming.cs:124).
  // In simple_world (interactive, no render-to-file) this is always 1.0 — frame_cook sets it from the
  // process-global constant 1.0. Named here so GetFrameSpeedFactor can read it faithfully off the seam.
  double frameSpeedFactor = 1.0;
};

// === context-var YELLOW seam (block #1 of the visual-load-bearing-root directive) ===
// Host-side per-frame variable map = TiXL EvaluationContext.Bool/Int/FloatVariables (Dictionary,
// EvaluationContext.cs:156-158), Reset() each top-level eval (cs:43-58). This is a RUNTIME POD that
// NEVER touches the 16-byte GPU-shared EvaluationContext (eval_context.h:39 static_assert) — the GPU
// ctx is locked, so the map lives host-side, exactly like TransportSnapshot. Owned by a function-
// local static in frame_cook.cpp run() (mirrors s_svState/s_arState), cleared once per frame at the
// TOP of cookStatefulValueNodes (the Reset analog), populated by Set*Var (writer pass), read by
// Get*Var (reader pass). YELLOW = flat global map (cross-sibling visible, no scope stack); the RED
// scoped push/pop engine (SetFloatVar.cs:26-41 SubGraph branch) is deferred.
// Vec3 channel (sub-seam B): TiXL SetVec3Var/GetVec3Var store a boxed Vector3 in the SHARED
// context.ObjectVariables dict (object dictionary, SetVec3Var.cs:30-41 / GetVec3Var.cs:24-27 with an
// `is Vector3` cast). sw keeps the float/int convention of a TYPED channel instead (NAMED FORK
// fork-ctxvar-vec3-typed-channel: a typed std::array<float,3> map, not a boxed object dict). This is
// the SAME fork sw already took for float/int (TiXL FloatVariables IS typed, but the cleaner choice
// here matters: a typed vec3 channel can't collide with a float/int of the same name, and the round-
// trip value is byte-identical). Vec3 = 3 Floats (sw's vec-as-N-floats convention, same as AddVec3:
// Result.x/.y/.z) → SetVec3Var writes (x,y,z); GetVec3Var reads them onto out[0..2] → extOut[0..2],
// well within the out[8] stateful-cook budget. NO new cook pass, NO new rail — same 2-pass writer-
// before-reader flat YELLOW map as float/int. (Matrix is NOT here: GetMatrixVar must emit 16 floats
// = a 4-row Vector4[], which exceeds out[8] and rides the separate extColorOut matrix-output rail —
// see resident_matrix_output_cook.cpp; that's a cross-rail addition, not an additive value channel.)
// String channel (sub-seam C): TiXL SetStringVar/GetStringVar store/read a typed string in
// context.StringVariables (a typed Dictionary<string,string>, EvaluationContext.cs) — so this is the
// SAME typed-channel approach (NOT a boxed-object fork), the string twin of floatVars. But the String
// currency does NOT ride the stateful FLOAT cook (GetStringVar emits a Slot<string>, not a float): it
// rides cookStringNodes / extStrOut. So the per-frame CLEAR of stringVars happens in the SAME pass-0
// reset (frame_cook.cpp, next to floatVars/intVars/vec3Vars.clear()) — that pass runs BEFORE
// cookStringNodes — while the WRITE (SetStringVar) + READ (GetStringVar) happen inside cookStringNodes'
// own writer-first 2-pass split (resident_string_cook.cpp).
struct ContextVarMap {
  std::map<std::string, float> floatVars;
  std::map<std::string, long> intVars;
  std::map<std::string, std::array<float, 3>> vec3Vars;
  std::map<std::string, std::string> stringVars;  // String channel (sub-seam C): typed string vars
};

// True if opType is a context-var WRITER (Set*Var family). The 2-pass ordering (writer-before-reader)
// in cookStatefulValueNodes uses this to run all writers before any reader, deterministically every
// frame (simple_world iterates g.nodes in build order, not dataflow → ordering imposed explicitly).
bool isContextVarWriter(const std::string& opType);

// ── ctxVarLogSink — the LogLevel/LogUpdates diagnostic channel (param-completion fan-out, flow island) ──
// TiXL SetIntVar.LogLevel (SetIntVar.cs:28-55) and GetIntVar.LogUpdates (GetIntVar.cs:23-48) are
// MappedType<LogLevels> int inputs whose ONLY effect is gating Log.Warning/Log.Debug telemetry — they
// change NO value, NO map, NO render (faithful: the variable write/read is identical at every level).
// sw has no editor log pane, so the leaf exposes a tiny SINK (a per-level counter) an upper layer could
// later wire to a real console; the param-completion golden reads it to PROVE the LogLevel knob actually
// gates emission (so the new enum input is behaviour-bearing, not a dead inspector field). Mirrors the
// LogMessage logSink shape (point_ops_logmessage.cpp) — runtime owns the data, no upward dep.
//   LogLevels: 0=None, 1=Warnings, 2=Changes, 3=AllUpdates (SetIntVar.cs enum; GetIntVar shares it).
struct CtxVarLogSink {
  int warnings = 0;   // empty-name / undefined-var warnings emitted (level >= Warnings)
  int updates = 0;    // set/read debug lines emitted (level >= Changes for Set, AllUpdates for Get)
};
CtxVarLogSink& ctxVarLogSink();

// ctxVarLogBug — the --selftest-ctxvarlog TEETH hook. false = production (LogLevel/LogUpdates GATE the
// emission, faithful). true = the pre-fan-out degeneracy: IGNORE the level and ALWAYS emit (the dead-knob
// bug a missing/unwired LogLevel input would produce) → the LogLevel=None case emits anyway → the golden's
// "None suppresses" assertion goes RED. OFF in production; the golden flips it for the bug leg then resets.
bool& ctxVarLogBug();

// True if opType names a stateful value op (has a step fn in kStatefulValueOps). frame_cook uses
// this to pick which resident nodes to cook (the value-graph sibling of `opType == "AudioReaction"`).
bool isStatefulValueOp(const std::string& opType);

// Cook one stateful value node for this frame. `in` = its resolved Float inputs keyed by port id
// (from resolveResidentFloatInputs). `dt` = the RAW wall frame delta in seconds (TiXL Damp/Spring
// sample Playback.LastFrameDuration; each op clamps internally as TiXL does). `time` = wall
// seconds (for time-based ops like Ease; unused by Damp/Spring). Mutates `st`; writes up to 8
// outputs into out[0..7] (most ops ≤3; HasVec3Changed=7, PeakLevel=4). No-op if opType unknown.
// `tr` = the per-frame transport snapshot (see TransportSnapshot). Defaulted so the many existing
// stateless-of-transport callers (selftests + the Damp/Spring/... family that never read it) stay
// unchanged; only transport-reading ops (StopWatch) need to pass a real one.
// `vars` = the host-side context-var map (context-var seam); nullptr (default) for the ~30 ops that
// don't touch it (Damp/Spring/...). `varName` = the resolved String VariableName param (context-var
// ops only; from ResidentNode::strInputs). Both defaulted so every existing caller (selftests + the
// non-var family) is unchanged — same trick as `tr`.
void cookStatefulValueOp(const std::string& opType, const std::map<std::string, float>& in,
                         float dt, float time, StatefulValueState& st, float out[8],
                         const TransportSnapshot& tr = TransportSnapshot{},
                         ContextVarMap* vars = nullptr, const std::string& varName = std::string());

// Isolated proof: drive Damp (linear convergence + damped-spring branch with dt-clamp) and Spring
// (overshoot then settle) frame-by-frame against hand-computed TiXL trajectories. injectBug
// corrupts an expected step so the live assertions must FAIL (the --bite tooth).
int runStatefulValueSelfTest(bool injectBug);

// AnimValue TEETH hook (production-path golden, --selftest-animvalue). 0 = production (the default the
// real cook always uses); 1 = DROP the state write (the cross-frame WasHit tooth never advances);
// 2 = DROP the AnimMath call (Result becomes the raw normalizedTime). The golden sets this around the
// REAL cookStatefulValueNodes cook so a fixed (bug-independent) expected value bites. NOT a per-frame
// flag — it's a sticky module switch; the golden clears it back to 0 after each bug run.
void setAnimValueBug(int mode);

// AnimInt TEETH hook (--selftest-animint). 0 = production; 1 = DROP the state write (the cross-frame
// WasHit tooth never advances); 2 = DROP the positive-modulo wrap (Result becomes the raw (int)nt).
void setAnimIntBug(int mode);

// AnimBoolean TEETH hook (--selftest-animboolean). 0 = production; 1 = DROP the state write (the
// TriggerOutput tooth never advances); 2 = FREEZE the edge to 0 (TriggerOutput forced low).
void setAnimBooleanBug(int mode);

}  // namespace sw
