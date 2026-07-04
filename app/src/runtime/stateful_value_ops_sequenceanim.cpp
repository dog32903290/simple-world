// runtime/stateful_value_ops_sequenceanim — SequenceAnim (batch: keyframe-anim lane), split out of
// stateful_value_ops_anim2.cpp for the ARCHITECTURE rule 4 line-count ratchet. A stateful step
// sequencer cooked by frame_cook's stateful-value seam. TEETH hook + setter (setSequenceAnimBug) so
// the app-side golden flips the bug AROUND the REAL production cook.
//
// TiXL authority: Operators/Lib/numbers/anim/animators/SequenceAnim.cs (PLAYBACK path only).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "runtime/anim_math.h"                     // anim_math::fmodFloored / xxHash
#include "runtime/stateful_value_op_registry.h"    // StatefulOpReg
#include "runtime/stateful_value_ops.h"            // StatefulValueState / setSequenceAnimBug
#include "runtime/stateful_value_ops_easing.h"     // sw::applyEasing (Interpolation OutputMode)
#include "runtime/stateful_value_ops_internal.h"   // getIn / lerpf / enumOf / clamp01

namespace sw {
namespace {

// TiXL SchlickBias (SequenceAnim.cs:307-310). x / ((1/bias - 2)(1-x) + 1).
inline float schlickBias(float x, float bias) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}

// ============================================================================================
// SequenceAnim (TiXL numbers/anim/animators/SequenceAnim.cs) — PLAYBACK path
// --------------------------------------------------------------------------------------------
// A step-sequencer: a newline-separated `Sequence` string of digit chars ('0'..'9') defines rows of
// per-step strengths (char-'0')/9 ∈ [0,1]; SequenceIndex picks a row; the bar-normalized time indexes
// a step; OutputMode shapes the step strength into Result; WasStep fires on entering a non-zero step.
//
// TiXL Update (cs:72-274) PLAYBACK path ported faithfully (recording DROPPED — named fork below):
//   time = LocalFxTime*Rate + Phase ; if OverrideTime connected: time = OverrideTime*Rate.  (cs:119-124)
//   NormalizedBarTime = Fmod(time,1).Clamp(0, 0.999999).                                     (cs:126)
//   UpdateMode (cs:209-225): Time (default, no-op); PingPong: modTime=Fmod((time+1)/2,1),
//     NBT = |2*modTime-1|.Clamp; Random: seed=(uint)(time*N), NBT = (XxHash(seed)%N)/N + fraction.
//   stepIndex = floor(NBT * N) (clamp ≥0).                                                   (cs:227-231)
//   WasStep = N>0 && stepIndex != _lastStepIndex && seq[stepIndex] > 0 ; _lastStepIndex = stepIndex. (cs:232-233)
//   stepStrength = seq[stepIndex] ; stepTime = (NBT*N - stepIndex).Clamp(0,1) ;              (cs:235-237)
//   biasedTime = SchlickBias(stepTime, Bias) ; easedProgress = ApplyEasing(stepTime, dir, mode) ;
//   stepBeat = (1 - biasedTime) * stepStrength.                                              (cs:238-241)
//   OutputMode (cs:245-269): Pulse=Lerp(min,max,stepBeat); NormalizedValue=Lerp(min,max,strength);
//     CharacterValue=strength*9; Interpolation=Lerp(min,max, Lerp(strength, nextStrength, easedProgress)).
//
// STATE: s[0]=_lastStepIndex  s[1]=_lastUpdateTime.  init: _lastStepIndex seeds 0 (TiXL field-init 0);
//   we track it so WasStep's cross-frame "entered a new step" edge is real. The backwards-time-jump
//   reset (cs:75-78: LocalFxTime < _lastUpdateTime-0.1 → _lastStepIndex=-1) is honored via s[1].
//
// TIME (fork-seqanim-time-seam): LocalFxTime → seam `time` (seconds). OverrideTime honored when nonzero
//   (single-clock fork, same as AnimValue — the resolver can't see HasInputConnections).
//
// NAMED FORK fork-seqanim-recording-dropped: the RecordingMode family (cs:128-205) MUTATES the op's OWN
//   Sequence input string in place via the editor DirtyFlag machinery (Sequence.TypedInputValue.Value =
//   ...; DirtyFlag.Invalidate; IsDefault=false — cs:44-51). That is an editor-side input-rewrite the
//   resolved-Float/String cook seam cannot perform (the `in` map is read-only per frame). The PLAYBACK
//   path (UpdateMode Time/PingPong/Random × the 4 OutputModes) is fully ported; RecordingMode is fixed
//   to None. Same "editor-only input-mutation dropped" bargain the SetBpm/SetPlayback Command drop took.
//   Recording is a live-input-authoring gesture, not a playback value.
//
// The Sequence string rides the STRING rail (ResidentNode::strInputs), handed via the `varName` slot the
// cook already threads for context-var ops — here it carries the resolved Sequence string. (This op does
// NOT read context vars; it repurposes the SAME string-passing channel the seam already provides.)
int g_sequenceAnimBug = 0;

// Parse the SequenceIndex-th newline-row of `def` into per-step strengths (cs:277-304). Char c →
// (c-'0')/9 clamped [0,1]. Empty def → empty sequence. index wraps (Mod) over the row count.
static std::vector<float> parseSequenceRow(const std::string& def, int sequenceIndex) {
  std::vector<std::vector<float>> rows;
  std::vector<float> cur;
  for (size_t i = 0; i <= def.size(); ++i) {
    if (i == def.size() || def[i] == '\n') { rows.push_back(cur); cur.clear(); }
    else { float v = (float)(def[i] - '0') / 9.0f; cur.push_back(clamp01(v)); }
  }
  if (rows.empty()) return {};
  // CurrentSequence = _sequences[CurrentSequenceIndex.Mod(count)]  (cs:302, positive modulo)
  int n = (int)rows.size();
  int idx = sequenceIndex % n; if (idx < 0) idx += n;
  return rows[idx];
}

void stepSequenceAnim(const std::map<std::string, float>& in, float /*dt*/, float time,
                      StatefulValueState& st, float out[3], const TransportSnapshot&,
                      ContextVarMap*, const std::string& sequenceStr) {
  const float minValue = getIn(in, "MinValue", 0.0f);
  const float maxValue = getIn(in, "MaxValue", 0.0f);
  const float bias = getIn(in, "Bias", 0.5f);
  const float rate = getIn(in, "Rate", 1.0f);
  const float phase = getIn(in, "Phase", 0.0f);
  const int sequenceIndex = enumOf(in, "SequenceIndex");
  const int updateMode = enumOf(in, "UpdateMode");   // 0=Time,1=PingPong,2=Random
  const int outputMode = enumOf(in, "OutputMode");   // 0=Pulse,1=NormalizedValue,2=CharacterValue,3=Interpolation
  const int easeDir = enumOf(in, "Direction");
  const int easeMode = enumOf(in, "Interpolation");
  const float overrideTime = getIn(in, "OverrideTime", 0.0f);

  // Backwards-time-jump reset (cs:75-78) + carry last step.
  int lastStepIndex = (int)std::lround(st.s[0]);
  const double lastUpdateTime = st.init ? (double)st.s[1] : (double)time;
  if ((double)time < lastUpdateTime - 0.1) lastStepIndex = -1;
  st.init = true;
  st.s[1] = time;

  std::vector<float> seq = parseSequenceRow(sequenceStr, sequenceIndex);
  const int N = (int)seq.size();
  if (N == 0) { out[0] = 0.0f; out[1] = 0.0f; if (g_sequenceAnimBug != 1) st.s[0] = (float)lastStepIndex; return; }

  // time (cs:119-124): OverrideTime connected → override*rate; else LocalFxTime*rate + phase.
  double t = (overrideTime != 0.0f) ? ((double)overrideTime * (double)rate)
                                    : ((double)time * (double)rate + (double)phase);
  // NormalizedBarTime = Fmod(time,1).Clamp(0, 0.999999)  (cs:126)
  double nbt = anim_math::fmodFloored(t, 1.0);
  if (nbt < 0.0) nbt = 0.0; else if (nbt > 0.999999) nbt = 0.999999;

  if (updateMode == 2) {  // Random (cs:212-219)
    const uint32_t seed = (uint32_t)(t * (double)N);
    const uint32_t randomValue = anim_math::xxHash(seed);
    double fraction = std::fmod(nbt * N, 1.0) / N;
    if (fraction < 0.0) fraction = 0.0; else if (fraction > 0.999999) fraction = 0.999999;
    nbt = (double)(randomValue % (uint32_t)N) / (double)N + fraction;
    if (nbt < 0.0) nbt = 0.0; else if (nbt > 0.999999) nbt = 0.999999;
  } else if (updateMode == 1) {  // PingPong (cs:221-224)
    const double modTime = anim_math::fmodFloored((t + 1.0) / 2.0, 1.0);
    nbt = std::fabs(2.0 * modTime - 1.0);
    if (nbt < 0.0) nbt = 0.0; else if (nbt > 0.999999) nbt = 0.999999;
  }

  int stepIndex = (int)std::floor(nbt * N);
  if (stepIndex < 0) stepIndex = 0;

  const bool wasStep = N > 0 && stepIndex != lastStepIndex && seq[stepIndex] > 0.0f;  // (cs:232)

  const float stepStrength = seq[stepIndex];
  float stepTime = (float)(nbt * N - stepIndex);
  stepTime = clamp01(stepTime);
  const float biasedTime = schlickBias(stepTime, bias);
  const float easedProgress = applyEasing(stepTime, easeDir, easeMode);
  const float stepBeat = (1.0f - biasedTime) * stepStrength;

  float result = 0.0f;
  switch (outputMode) {
    case 0: result = lerpf(minValue, maxValue, stepBeat); break;                    // Pulse
    case 1: result = lerpf(minValue, maxValue, stepStrength); break;                // NormalizedValue
    case 2: result = stepStrength * 9.0f; break;                                    // CharacterValue (MaxCharacterValue=9)
    case 3: {  // Interpolation
      const int nextIdx = (stepIndex + 1) % N;
      const float nextStrength = seq[nextIdx];
      float interp = lerpf(stepStrength, nextStrength, easedProgress);
      result = lerpf(minValue, maxValue, interp);
      break;
    }
  }

  // bug 1: DROP the state write (_lastStepIndex frozen) → WasStep's cross-frame "entered a new step"
  //   edge breaks (re-fires or never fires) while Result stays right.
  // bug 2: DROP the OutputMode shaping (Result forced to the raw stepStrength, ignoring Min/Max/bias) →
  //   the OutputMode golden bites while WasStep (state) stays correct.
  if (g_sequenceAnimBug != 1) st.s[0] = (float)stepIndex;

  out[0] = (g_sequenceAnimBug == 2) ? stepStrength : result;  // Result
  out[1] = wasStep ? 1.0f : 0.0f;  // WasStep (Bool→Float)
}

}  // namespace

void setSequenceAnimBug(int mode) { g_sequenceAnimBug = mode; }

static const StatefulOpReg _reg_SequenceAnim{"SequenceAnim", stepSequenceAnim};

}  // namespace sw
