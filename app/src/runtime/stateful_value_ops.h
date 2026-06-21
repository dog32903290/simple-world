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
struct ContextVarMap {
  std::map<std::string, float> floatVars;
  std::map<std::string, long> intVars;
};

// True if opType is a context-var WRITER (Set*Var family). The 2-pass ordering (writer-before-reader)
// in cookStatefulValueNodes uses this to run all writers before any reader, deterministically every
// frame (simple_world iterates g.nodes in build order, not dataflow → ordering imposed explicitly).
bool isContextVarWriter(const std::string& opType);

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
