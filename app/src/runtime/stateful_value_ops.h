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
// like AudioReactionState). A generic 8-float scratch covers every stateful value op (incl. vec3):
//   Damp:       s[0]=dampedValue, s[1]=velocity
//   Spring:     s[0]=springedValue, s[1]=result (the previous output)
//   DampVecN:   s[0..N-1]=damped per component, s[N..2N-1]=velocity per component
//   SpringVecN: s[0..N-1]=springed per component, s[N..2N-1]=result per component
// `init` = TiXL's _isFirstEval: the first cook seeds state from the input (no smoothing yet).
struct StatefulValueState {
  float s[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  bool  init = false;
};

// True if opType names a stateful value op (has a step fn in kStatefulValueOps). frame_cook uses
// this to pick which resident nodes to cook (the value-graph sibling of `opType == "AudioReaction"`).
bool isStatefulValueOp(const std::string& opType);

// Cook one stateful value node for this frame. `in` = its resolved Float inputs keyed by port id
// (from resolveResidentFloatInputs). `dt` = the RAW wall frame delta in seconds (TiXL Damp/Spring
// sample Playback.LastFrameDuration; each op clamps internally as TiXL does). `time` = wall
// seconds (for time-based ops like Ease; unused by Damp/Spring). Mutates `st`; writes up to 3
// outputs into out[0..2]. No-op (out untouched) if opType is unknown.
void cookStatefulValueOp(const std::string& opType, const std::map<std::string, float>& in,
                         float dt, float time, StatefulValueState& st, float out[3]);

// Isolated proof: drive Damp (linear convergence + damped-spring branch with dt-clamp) and Spring
// (overshoot then settle) frame-by-frame against hand-computed TiXL trajectories. injectBug
// corrupts an expected step so the live assertions must FAIL (the --bite tooth).
int runStatefulValueSelfTest(bool injectBug);

}  // namespace sw
