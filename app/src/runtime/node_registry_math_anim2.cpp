// runtime/node_registry_math_anim2 — self-registering MATH NodeSpec leaf: the keyframe-anim lane
// animators (AdsrEnvelope / TriggerAnim / SequenceAnim / DateTimeInSecs).
//
// Split from node_registry_math_anim.cpp (ARCHITECTURE rule 4 line-count ratchet). Every spec is a
// stateful op (evaluate==nullptr — cooked by frame_cook's stateful-value seam, step fns in
// stateful_value_ops_anim2.cpp / _sequenceanim.cpp / _datetimeinsecs.cpp). Adding an anim op here =
// drop a MathOp registrar; the central manifest is never touched (self-registration sink pattern).
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

      // ─────────────────────── keyframe-anim lane (batch: keyframe-anim) ───────────────────────

      // AdsrEnvelope (TiXL numbers/anim/AdsrEnvelope.cs → Core/Audio/AdsrCalculator.cs frame-based
      // Update). A gate/trigger ADSR envelope: Result = Lerp(Min, Max, envelope(0..1)); IsActive =
      // stage != Idle. STATEFUL (cross-frame stage machine, evaluate==nullptr; cooked by frame_cook's
      // stateful-value seam, step fn stateful_value_ops_anim2.cpp). Outputs FIRST (Result, IsActive);
      // IsActive is Bool→Float 0/1 (Cut 32). Inputs in TiXL Input decl order. .t3 defaults
      // (AdsrEnvelope.t3, re-read): Envelope=(0.1,0.1,1.0,0.1)=A/D/S/R, Min=0, Max=1, Gate=false,
      // Duration=1, Mode=0(Gate). Envelope Vector4 → 4 Float ports (fork-vec4-as-4-floats). Mode is a
      // MappedType<AdsrCalculator.TriggerMode> int → Widget::Enum. FORKS in step fn: single-clock time
      // (context.LocalFxTime → seam clock); first-frame lastTime seed (fork-adsr-first-frame-lasttime).
static const MathOp _reg_AdsrEnvelope{
      {"AdsrEnvelope", "AdsrEnvelope",
       {{"Result", "Result", "Float", false},
        {"IsActive", "IsActive", "Float", false},
        {"Gate", "Gate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0f, 60.0f, Widget::Slider},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum, {"Gate", "Trigger"}},
        {"Envelope.x", "Envelope",   "Float", true, 0.1f, 0.0f, 10.0f, Widget::Vec, {}, false, 4},
        {"Envelope.y", "Envelope.y", "Float", true, 0.1f, 0.0f, 10.0f, Widget::Vec, {}, false, 1},
        {"Envelope.z", "Envelope.z", "Float", true, 1.0f, 0.0f, 1.0f,  Widget::Vec, {}, false, 1},
        {"Envelope.w", "Envelope.w", "Float", true, 0.1f, 0.0f, 10.0f, Widget::Vec, {}, false, 1},
        {"Min", "Min", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Max", "Max", "Float", true, 1.0f, -10.0f, 10.0f}},
       nullptr,
       "numbers.anim"}
};

      // TriggerAnim (TiXL numbers/anim/animators/TriggerAnim.cs) — a one-shot / ping-pong progress
      // animator: Result = Base + Amplitude * SchlickBias(MapShapes[Shape](LastFraction), Bias); a
      // Trigger edge sweeps LastFraction over Duration (after Delay). HasCompleted fires on a Forward
      // sweep reaching 1. STATEFUL (LastFraction/direction/triggerTime across frames, evaluate==nullptr;
      // step fn stateful_value_ops_anim2.cpp). Outputs FIRST (Result, HasCompleted); HasCompleted is
      // Bool→Float. Inputs in TiXL Input decl order. .t3 defaults (TriggerAnim.t3, re-read): Trigger=
      // false, Shape=0(Linear), AnimMode=0(OnlyOnTrue), Duration=1, Base=0, Amplitude=1, Delay=0,
      // Bias=0.5, TimeMode=0(LocalFxTime), UseTriggerVar="__Trigger". Shape/AnimMode/TimeMode are
      // MappedType int enums → Widget::Enum. UseTriggerVar rides the String rail (VariableName-style)
      // — the no-connection trigger-var path (fork-triggeranim-trigger-var). FORKS: TimeMode LocalFxTime/
      // PlayTime/AppRunTime via TransportSnapshot; double-eval guard dropped.
static const MathOp _reg_TriggerAnim{
      {"TriggerAnim", "TriggerAnim",
       {{"Result", "Result", "Float", false},
        {"HasCompleted", "HasCompleted", "Float", false},
        {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Shape", "Shape", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum,
         {"Linear", "SmoothStep", "EaseIn", "EaseOut", "Shake", "Kick"}},
        {"AnimMode", "AnimMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"OnlyOnTrue", "OnlyOnFalse", "ForwardAndBackwards"}},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0001f, 60.0f, Widget::Slider},
        {"Base", "Base", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Amplitude", "Amplitude", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Delay", "Delay", "Float", true, 0.0f, 0.0f, 60.0f, Widget::Slider},
        {"Bias", "Bias", "Float", true, 0.5f, 0.0001f, 1.0f},
        {"TimeMode", "TimeMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"LocalFxTime", "PlayTime", "AppRunTime"}},
        {"VariableName", "UseTriggerVar", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider,
         {}, false, 1, false, "__Trigger"}},
       nullptr,
       "numbers.anim.animators"}
};

      // SequenceAnim (TiXL numbers/anim/animators/SequenceAnim.cs) — PLAYBACK path (recording DROPPED,
      // fork-seqanim-recording-dropped: the RecordingMode family rewrites the op's OWN Sequence input in
      // place via editor DirtyFlag machinery, which the read-only cook `in` map cannot do). A step
      // sequencer: the newline-separated Sequence digit-string defines rows of per-step strengths;
      // bar-normalized time indexes a step; OutputMode shapes it into Result; WasStep fires on entering a
      // non-zero step. STATEFUL (_lastStepIndex/_lastUpdateTime across frames, evaluate==nullptr; step fn
      // stateful_value_ops_anim2.cpp). Outputs FIRST (Result, WasStep); WasStep is Bool→Float. Inputs in
      // TiXL Input decl order. .t3 defaults (SequenceAnim.t3, re-read): Sequence="1101110111011101"×3
      // rows, SequenceIndex=0, UpdateMode=0(Time), Rate=1, Phase=0, OutputMode=1(NormalizedValue),
      // MinValue=0, MaxValue=1, Bias=0.5, OverrideTime=0, Direction=0(In), Interpolation=0(Sine).
      // UpdateMode/OutputMode/Direction/Interpolation are MappedType int enums → Widget::Enum. Sequence
      // rides the String rail (carried by the cook's single-string channel). RecordingMode/RecordValue
      // ports OMITTED (recording fork — the cook fixes RecordingMode to None).
static const MathOp _reg_SequenceAnim{
      {"SequenceAnim", "SequenceAnim",
       {{"Result", "Result", "Float", false},
        {"WasStep", "WasStep", "Float", false},
        {"Sequence", "Sequence", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
         "1101110111011101\n1101110111011101\n1101110111011101"},
        {"SequenceIndex", "SequenceIndex", "Float", true, 0.0f, 0.0f, 100.0f, Widget::Slider},
        {"UpdateMode", "UpdateMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"Time", "PingPong", "Random"}},
        {"Rate", "Rate", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f},
        {"OutputMode", "OutputMode", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
         {"Pulse", "NormalizedValue", "CharacterValue", "Interpolation"}},
        {"MinValue", "MinValue", "Float", true, 0.0f, -10.0f, 10.0f},
        {"MaxValue", "MaxValue", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Bias", "Bias", "Float", true, 0.5f, 0.0001f, 1.0f},
        {"OverrideTime", "OverrideTime", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"In", "Out", "InOut"}},
        {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce", "Linear"}}},
       nullptr,
       "numbers.anim.animators"}
};

      // DateTimeInSecs (TiXL numbers/anim/time/DateTimeInSecs.cs) — the Unix-wall-clock second, latched by
      // Freeze. STATEFUL (the _lastValue latch, evaluate==nullptr — the value comes from the OS clock +
      // cross-frame latch; step fn stateful_value_ops_datetimeinsecs.cpp). ONE output (Result, int→Float)
      // FIRST. .t3 default (DateTimeInSecs.t3): Freeze=false. Freeze is a bool → Widget::Bool.
static const MathOp _reg_DateTimeInSecs{
      {"DateTimeInSecs", "DateTimeInSecs",
       {{"Result", "Result", "Float", false},
        {"Freeze", "Freeze", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "numbers.anim.time"}
};

}  // namespace
}  // namespace sw
