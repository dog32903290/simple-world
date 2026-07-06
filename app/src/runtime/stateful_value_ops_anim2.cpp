// runtime/stateful_value_ops_anim2 — the keyframe-anim animator family (batch: keyframe-anim lane):
//   AdsrEnvelope / TriggerAnim.  Both are stateful value ops whose output depends on PRIOR frames
//   (envelope stage, trigger direction) — so, like the AnimValue family next door, they have no pure
//   evaluate() and are cooked once per frame by frame_cook's stateful-value seam (cookStatefulValueNodes
//   → cookStatefulValueOp), dispatched by type name. (SequenceAnim, the third lane node, lives in its
//   own leaf stateful_value_ops_sequenceanim.cpp — line-count ratchet split.)
//
// TiXL authority (ported by line number below, inline):
//   AdsrEnvelope : Operators/Lib/numbers/anim/AdsrEnvelope.cs  +  Core/Audio/AdsrCalculator.cs (the
//                  frame-based Update, cs:233-354 — the envelope math AdsrEnvelope delegates to).
//   TriggerAnim  : Operators/Lib/numbers/anim/animators/TriggerAnim.cs
//
// Each op carries its TEETH-hook global + public setter (setAdsrEnvelopeBug / setTriggerAnimBug) so the
// app-side goldens flip the bug AROUND the REAL production cook (mirror of setAnimValueBug in
// stateful_value_ops_anim.cpp).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <cstdint>
#include <map>
#include <string>

#include "runtime/stateful_value_op_registry.h"    // StatefulOpReg
#include "runtime/stateful_value_ops.h"            // StatefulValueState / TransportSnapshot / setters
#include "runtime/stateful_value_ops_internal.h"   // getIn / lerpf / enumOf / clamp01

namespace sw {
namespace {

// ─────────────────────────── shared shaping helpers (TiXL-verbatim) ───────────────────────────
// TiXL SchlickBias (AdsrEnvelope/TriggerAnim share the SAME formula:
// TriggerAnim.cs:179-182, AnimMath.cs:85-87). x / ((1/bias - 2)(1-x) + 1).
inline float schlickBias(float x, float bias) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}

// TiXL MathUtils.SmootherStep(min,max,value) (MathUtils.cs:146-150) — the Fade-based smootherstep the
// TriggerAnim shape table uses (SmoothStep/EaseIn/EaseOut). Fade(t)=t^3(t(6t-15)+10) (MathUtils.cs:141).
inline float smootherStep(float mn, float mx, float value) {
  float t = (value - mn) / (mx - mn);
  if (t < 0.0f) t = 0.0f;
  else if (t > 1.0f) t = 1.0f;
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// ============================================================================================
// AdsrEnvelope (TiXL numbers/anim/AdsrEnvelope.cs → Core/Audio/AdsrCalculator.cs frame-based Update)
// --------------------------------------------------------------------------------------------
// AdsrEnvelope.cs:56-74 reads inputs, extracts A/D/S/R from the Envelope Vector4 (cs:65-68 with the
// per-component "≤0 → default" fallbacks), calls _calculator.Update(gate, LocalFxTime, a,d,s,r, mode,
// duration), then Result = Lerp(Min, Max, calc.Value); IsActive = calc.IsActive (stage != Idle).
//
// AdsrCalculator.Update (cs:233-354) — the frame-based envelope (the one AdsrEnvelope uses; the
// sample-based UpdateSample path is NOT used by this op):
//   SetParameters (cs:66-72): attack/decay/release = Max(0.001, v); sustain = Clamp(v,0,1).
//   risingEdge = gate && !prevGate ; fallingEdge = !gate && prevGate ; prevGate = gate.  (cs:241-243)
//   deltaTime = currentTime - lastTime ; lastTime = currentTime.                          (cs:246-247)
//   if (deltaTime < 0 || deltaTime > 1) deltaTime = 0.016  (huge-jump guard).             (cs:250-251)
//   Gate mode:    rising → Attack (stageTime=0, releaseStart=Value); falling&&!Idle → Release.  (cs:253-268)
//   Trigger mode: rising → Attack (stageTime=0, totalTime=0, releaseStart=Value);
//                 if !Idle && !Release && duration<MAX && totalTime>=duration → Release.   (cs:269-290)
//   stageTime += dt ; if (!Release && !Idle) totalTime += dt.                             (cs:293-297)
//   stage machine (cs:300-351): Idle→0; Attack ramp stageTime/attack then →Decay@1;
//     Decay 1-progress*(1-sustain) then →Sustain@sustain; Sustain=sustain; Release
//     releaseStart*(1-progress) then →Idle@0.  Value = Clamp(Value,0,1).                  (cs:353)
//
// STATE (StatefulValueState.s[]): the AdsrCalculator private fields carried across frames.
//   s[0]=CurrentStage (0..4)  s[1]=stageTime  s[2]=totalTime  s[3]=prevGate(0/1)
//   s[4]=releaseStartValue    s[5]=lastTime   s[6]=Value
//   st.init = the first cook (TiXL's calculator is fresh: stage=Idle, all fields 0, prevGate=false).
//
// TIME (fork-adsr-time-seam): TiXL uses context.LocalFxTime (SECONDS). The step fn's `time` arg IS the
//   wall fx seconds seam clock (the whole time-op family's single-clock substitution). Exact.
//
// NAMED FORK fork-adsr-first-frame-lasttime: TiXL field-inits _lastTime=0, so on its very first Update
//   deltaTime = currentTime - 0. If currentTime>1 that hits the >1 guard → 0.016; if 0<currentTime≤1
//   it's used raw. To keep the golden DETERMINISTIC regardless of absolute clock, we seed s[5]=time on
//   the first cook (init) → first deltaTime=0. This differs from TiXL ONLY on frame 1 and ONLY when
//   0<currentTime≤1 (a partial first advance); every subsequent frame is byte-identical. Accepted: the
//   first frame's tiny delta is dominated by the huge-jump guard in any real playback start anyway.
int g_adsrEnvelopeBug = 0;

void stepAdsrEnvelope(const std::map<std::string, float>& in, float /*dt*/, float time,
                      StatefulValueState& st, float out[3], const TransportSnapshot&,
                      ContextVarMap*, const std::string&) {
  const bool gate = getIn(in, "Gate", 0.0f) != 0.0f;
  const int mode = enumOf(in, "Mode");  // 0=Gate, 1=Trigger (AdsrCalculator.TriggerMode)
  const float durationIn = getIn(in, "Duration", 0.0f);
  // Envelope Vector4 → A/D/S/R with AdsrEnvelope.cs:65-68 fallbacks.
  const float envA = getIn(in, "Envelope.x", 0.0f);
  const float envB = getIn(in, "Envelope.y", 0.0f);
  const float envC = getIn(in, "Envelope.z", 0.0f);
  const float envD = getIn(in, "Envelope.w", 0.0f);
  const float attackIn  = envA > 0.0f ? envA : 0.01f;
  const float decayIn   = envB > 0.0f ? envB : 0.1f;
  const float sustainIn = envC >= 0.0f ? clamp01(envC) : 0.7f;
  const float releaseIn = envD > 0.0f ? envD : 0.3f;
  const float minV = getIn(in, "Min", 0.0f);
  const float maxV = getIn(in, "Max", 0.0f);

  // SetParameters (AdsrCalculator.cs:66-72): floor A/D/R at 0.001, clamp sustain [0,1].
  const float attack  = attackIn  > 0.001f ? attackIn  : 0.001f;
  const float decay   = decayIn   > 0.001f ? decayIn   : 0.001f;
  const float sustain = clamp01(sustainIn);
  const float release = releaseIn > 0.001f ? releaseIn : 0.001f;
  // _duration = duration>0 ? duration : float.MaxValue  (cs:238)
  const float FLT_MAXV = 3.4028235e38f;
  const float duration = durationIn > 0.0f ? durationIn : FLT_MAXV;

  // Load carried state.
  int stage = (int)std::lround(st.s[0]);  // 0=Idle,1=Attack,2=Decay,3=Sustain,4=Release
  float stageTime = st.s[1];
  float totalTime = st.s[2];
  bool prevGate = st.s[3] != 0.0f;
  double releaseStart = (double)st.s[4];
  float value = st.s[6];
  if (!st.init) { st.s[5] = time; st.init = true; }  // fork-adsr-first-frame-lasttime (dt=0 on frame 1)
  const float lastTime = st.s[5];

  // Edges (cs:241-243).
  const bool risingEdge = gate && !prevGate;
  const bool fallingEdge = !gate && prevGate;
  prevGate = gate;

  // deltaTime + huge-jump guard (cs:246-251).
  float deltaTime = time - lastTime;
  st.s[5] = time;
  if (deltaTime < 0.0f || deltaTime > 1.0f) deltaTime = 0.016f;

  if (mode == 0) {  // Gate mode (cs:253-268)
    if (risingEdge) { stage = 1; stageTime = 0.0f; releaseStart = value; }
    else if (fallingEdge && stage != 0) { stage = 4; stageTime = 0.0f; releaseStart = value; }
  } else {  // Trigger mode (cs:269-290)
    if (risingEdge) { stage = 1; stageTime = 0.0f; totalTime = 0.0f; releaseStart = value; }
    if (stage != 0 && stage != 4 && duration < FLT_MAXV && totalTime >= duration) {
      stage = 4; stageTime = 0.0f; releaseStart = value;
    }
  }

  // Update timing (cs:293-297).
  stageTime += deltaTime;
  if (stage != 4 && stage != 0) totalTime += deltaTime;

  // Stage machine (cs:300-351).
  switch (stage) {
    case 0: value = 0.0f; break;  // Idle
    case 1:  // Attack
      if (stageTime < attack) value = stageTime / attack;
      else { value = 1.0f; stage = 2; stageTime = 0.0f; }
      break;
    case 2:  // Decay
      if (stageTime < decay) { const float p = stageTime / decay; value = 1.0f - p * (1.0f - sustain); }
      else { value = sustain; stage = 3; stageTime = 0.0f; }
      break;
    case 3: value = sustain; break;  // Sustain
    case 4:  // Release
      if (stageTime < release) { const float p = stageTime / release; value = (float)(releaseStart * (1.0 - (double)p)); }
      else { value = 0.0f; stage = 0; stageTime = 0.0f; }
      break;
  }
  value = clamp01(value);  // (cs:353)

  // bug 1: DROP the state write (stage/times never advance) — the cross-frame envelope machine
  //   freezes → the Attack→Decay→Sustain progression golden bites while a single-frame value stays 0.
  // bug 2: DROP the Lerp range mapping (Result forced to raw envelope value, ignoring Min/Max) →
  //   the range golden bites while IsActive (stage) stays correct.
  if (g_adsrEnvelopeBug != 1) {
    st.s[0] = (float)stage; st.s[1] = stageTime; st.s[2] = totalTime;
    st.s[3] = prevGate ? 1.0f : 0.0f; st.s[4] = (float)releaseStart; st.s[6] = value;
  }

  out[0] = (g_adsrEnvelopeBug == 2) ? value : lerpf(minV, maxV, value);  // Result = Lerp(Min,Max,Value)
  out[1] = (stage != 0) ? 1.0f : 0.0f;  // IsActive = CurrentStage != Idle (Bool→Float)
}

// ============================================================================================
// TriggerAnim (TiXL numbers/anim/animators/TriggerAnim.cs)
// --------------------------------------------------------------------------------------------
// A one-shot / ping-pong progress animator: a Trigger edge starts a Forward (or Backward) sweep of
// LastFraction over `Duration` seconds (after `Delay`), shaped by one of 6 MapShapes + SchlickBias,
// scaled to Base + Amplitude*shaped. HasCompleted fires when a Forward sweep reaches 1.
//
// TiXL Update (cs:28-169) ported faithfully:
//   triggered = Trigger || (no-connection trigger-var==1). Edge (triggered != _trigger, cs:58):
//     ForwardAndBackwards: _triggerTime=t; dir = triggered?Forward:Backward; _startProgress=LastFraction.
//     OnlyOnTrue + triggered:  _triggerTime=t; dir=Forward; LastFraction=0.
//     OnlyOnFalse + !triggered:_triggerTime=t; dir=Backward; LastFraction=1.
//   Progress integration (cs:93-159): per AnimMode branch + direction, advance LastFraction from
//     (currentTime - _triggerTime), honoring Delay in the Forward legs, clamping at 0/1 and dropping to
//     Direction.None at the ends (HasCompleted=true on the Forward→1 completion).
//   normalizedValue = SchlickBias(MapShapes[shape](LastFraction), bias)  (cs:171-177)
//   Result = Base + Amplitude * normalizedValue  (cs:168 — NOT a Lerp; the commented-out Lerp is cs:167)
//
// SHAPES (cs:186-194, MapShapes[]): 0 Linear=clamp01; 1 SmoothStep=SmootherStep(0,1,x); 2 EaseIn=
//   SmootherStep(0,1,x/2)*2; 3 EaseOut=SmootherStep(0,1,x/2+0.5)*2-1; 4 Shake=sin(x*40)*(1-clamp(x,
//   1e-4,1))^4; 5 Kick = x<=0 ? 0 : (1-clamp01(x)).
//
// TIME (fork-triggeranim-timemode): TiXL TimeMode picks LocalFxTime / PlayTime(=TimeInBars) /
//   AppRunTime(=RunTimeInSecs). sw maps: LocalFxTime → seam `time` (wall fx seconds);
//   AppRunTime → tr.runTimeSecs; PlayTime → tr.localTimeBars (the playhead in BARS). Same
//   TransportSnapshot channel the Ease/Time family already reads.
//
// STATE: s[0]=LastFraction  s[1]=_triggerTime  s[2]=_currentDirection(0=Fwd,1=Back,2=None)
//   s[3]=_trigger(0/1)  s[4]=_startProgress.  init seeds dir=None(2), trigger=0.
//
// FORKS (named):
//   • fork-triggeranim-trigger-var: the no-connection IntVariables[UseTriggerVar]==1 path (cs:54-55)
//     reads the context-var YELLOW seam intVars (same channel WasTrigger uses). When `vars` is null or
//     the var is unset → 0 (TiXL GetValueOrDefault(...,0)). The Trigger Float input is the connected path.
//   • fork-triggeranim-double-eval-guard: the LocalFxTime==_lastUpdateTime guard (cs:31-32) is DROPPED
//     (once-per-frame cook, Ease/AnimValue precedent).
int g_triggerAnimBug = 0;

// MapShapes[shape](f) — TiXL TriggerAnim.cs:186-194 ported VERBATIM.
inline float triggerMapShape(int shapeIndex, float f) {
  const float fc = clamp01(f);
  switch (shapeIndex) {
    case 0: return fc;                                      // Linear
    case 1: return smootherStep(0.0f, 1.0f, fc);           // SmoothStep
    case 2: return smootherStep(0.0f, 1.0f, fc / 2.0f) * 2.0f;               // EaseIn
    case 3: return smootherStep(0.0f, 1.0f, fc / 2.0f + 0.5f) * 2.0f - 1.0f; // EaseOut
    case 4: {  // Shake: sin(clamp01(f)*40) * pow(1 - clamp(f,1e-4,1), 4)
      const float cl = f < 0.0001f ? 0.0001f : (f > 1.0f ? 1.0f : f);
      const float p = 1.0f - cl;
      return std::sin(fc * 40.0f) * (p * p * p * p);
    }
    case 5: return f <= 0.0f ? 0.0f : (1.0f - fc);         // Kick
    default: return fc;
  }
}

void stepTriggerAnim(const std::map<std::string, float>& in, float /*dt*/, float time,
                     StatefulValueState& st, float out[3], const TransportSnapshot& tr,
                     ContextVarMap* vars, const std::string& varName) {
  const float baseValue = getIn(in, "Base", 0.0f);
  const float amplitude = getIn(in, "Amplitude", 0.0f);
  const float bias = getIn(in, "Bias", 0.5f);
  int shape = enumOf(in, "Shape"); if (shape < 0) shape = 0; else if (shape > 5) shape = 5;
  const float duration = getIn(in, "Duration", 1.0f);  // TiXL _duration field-default 1 = .t3 default
  const float delay = getIn(in, "Delay", 0.0f);
  const int animMode = enumOf(in, "AnimMode");   // 0=OnlyOnTrue,1=OnlyOnFalse,2=ForwardAndBackwards
  const int timeMode = enumOf(in, "TimeMode");   // 0=LocalFxTime,1=PlayTime,2=AppRunTime

  // currentTime per TimeMode (fork-triggeranim-timemode).
  double currentTime;
  if (timeMode == 1) currentTime = tr.localTimeBars;        // PlayTime = TimeInBars (bars)
  else if (timeMode == 2) currentTime = tr.runTimeSecs;     // AppRunTime = RunTimeInSecs
  else currentTime = (double)time;                          // LocalFxTime = seam clock (seconds)

  // Load state.
  double lastFraction = (double)st.s[0];
  double triggerTime = (double)st.s[1];
  int dir = st.init ? (int)std::lround(st.s[2]) : 2;  // None on frame 1
  bool prevTrigger = st.init ? (st.s[3] != 0.0f) : false;
  double startProgress = (double)st.s[4];
  // HasCompleted is a LATCH, not a pulse: TiXL clears it ONLY on a trigger edge (TriggerAnim.cs:60
  // `HasCompleted.Value = false` inside `if (triggered != _trigger)`) and sets it at Forward completion
  // (cs:141) — between those events the slot HOLDS its value across frames. The per-frame reset that
  // used to live below was an undeclared pulse fork (2026-07-06 audit, parity bug).
  bool hasCompleted = st.init ? (st.s[5] != 0.0f) : false;
  st.init = true;

  // triggered = Trigger || (no-connection var==1). fork-triggeranim-trigger-var.
  bool isTriggeredByVar = false;
  if (vars && !varName.empty()) {
    auto it = vars->intVars.find(varName);
    isTriggeredByVar = (it != vars->intVars.end() && it->second == 1);
  }
  const bool triggered = (getIn(in, "Trigger", 0.0f) != 0.0f) || isTriggeredByVar;

  if (triggered != prevTrigger) {  // edge (cs:58)
    prevTrigger = triggered;
    hasCompleted = false;  // latch clears on the edge only (cs:60)
    if (animMode == 2) {  // ForwardAndBackwards
      triggerTime = currentTime;
      dir = triggered ? 0 : 1;  // Forward : Backwards
      startProgress = lastFraction;
    } else {
      if (triggered) {
        if (animMode == 0) { triggerTime = currentTime; dir = 0; lastFraction = 0.0; }  // OnlyOnTrue
      } else {
        if (animMode == 1) { triggerTime = currentTime; dir = 1; lastFraction = 1.0; }  // OnlyOnFalse
      }
    }
  }

  if (animMode == 2) {  // ForwardAndBackwards (cs:93-122)
    const float dp = (float)((currentTime - triggerTime) / duration);
    if (dir == 0) {  // Forward
      const float delayedDp = (currentTime - triggerTime < delay) ? 0.0f
                              : (float)((currentTime - triggerTime - delay) / duration);
      lastFraction = startProgress + delayedDp;
      if (lastFraction >= 1.0) { hasCompleted = true; lastFraction = 1.0; dir = 2; }
    } else if (dir == 1) {  // Backwards
      lastFraction = startProgress - dp;
      if (lastFraction <= 0.0) { lastFraction = 0.0; dir = 2; }
    }
  } else {  // OnlyOnTrue / OnlyOnFalse (cs:123-159)
    if (dir == 0) {  // Forward
      const double timeSinceTrigger = currentTime - triggerTime;
      if (timeSinceTrigger < delay) { lastFraction = 0.0; }
      else {
        lastFraction = (timeSinceTrigger - delay) / duration;
        if (lastFraction >= 1.0) { lastFraction = 1.0; hasCompleted = true; dir = 2; }
      }
    } else if (dir == 1) {  // Backwards
      lastFraction = 1.0 + (triggerTime - currentTime) / duration;
      if (lastFraction < 0.0) { lastFraction = 0.0; dir = 2; }
    }
  }

  // normalizedValue + NaN/Inf guard (cs:161-165 — TiXL computes normalizedValue BEFORE the guard
  // resets LastFraction, so the guard only affects the STORED state, not this frame's value).
  const float normalizedValue = schlickBias(triggerMapShape(shape, (float)lastFraction), bias);
  if (std::isnan((float)lastFraction) || std::isinf((float)lastFraction)) lastFraction = 0.0;

  // bug 1: DROP the state write → LastFraction/dir/triggerTime frozen → the multi-frame sweep never
  //   progresses (the Forward-ramp golden at later frames bites) while a single frame stays at seed.
  // bug 2: DROP the SchlickBias+shape shaping (Result forced to Base + Amplitude*rawFraction) → the
  //   shape/bias golden bites while the completion/state stays correct.
  if (g_triggerAnimBug != 1) {
    st.s[0] = (float)lastFraction; st.s[1] = (float)triggerTime; st.s[2] = (float)dir;
    st.s[3] = prevTrigger ? 1.0f : 0.0f; st.s[4] = (float)startProgress;
    st.s[5] = hasCompleted ? 1.0f : 0.0f;  // latch persists (cs:60/141)
  }

  out[0] = (g_triggerAnimBug == 2) ? (baseValue + amplitude * (float)lastFraction)
                                   : (baseValue + amplitude * normalizedValue);  // Result (cs:168)
  out[1] = hasCompleted ? 1.0f : 0.0f;  // HasCompleted (Bool→Float)
}

}  // namespace

// TEETH hook setters (globals in the anonymous namespace above; app-side goldens flip around prod cook).
void setAdsrEnvelopeBug(int mode) { g_adsrEnvelopeBug = mode; }
void setTriggerAnimBug(int mode) { g_triggerAnimBug = mode; }

static const StatefulOpReg _reg_AdsrEnvelope{"AdsrEnvelope", stepAdsrEnvelope};
static const StatefulOpReg _reg_TriggerAnim{"TriggerAnim", stepTriggerAnim};

}  // namespace sw
