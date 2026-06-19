// runtime/node_registry_math — NodeSpec table for MATH / VALUE ops.
// Ops: Time, AudioReaction, Const, Multiply, Sine, Add, Sub, Div, Clamp, Remap, Abs, Floor, Lerp.
// These are pure Float value-node ops plus stateful audio/time value sources.
// Split from node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
#include "runtime/node_registry_math.h"
#include "runtime/graph.h"
#include "runtime/value_eval_ops.h"  // evalTime, evalConst, evalMultiply, etc.

namespace sw {

const std::vector<NodeSpec>& mathSpecs() {
  static const std::vector<NodeSpec> specs = {
      // --- Value nodes (Task 2) ---
      {"Time", "Time", {{"out", "out", "Float", false}}, evalTime},
      // TiXL AudioReaction (full parity): 3 outputs + 10 params. STATEFUL — cooked in main
      // from the live spectrum (runtime/audio_reaction) because it needs the whole spectrum
      // (too big for ctx) + per-node memory; so it has no pure evaluate() and evalFloat reads
      // its outputs from Node::outCache. Params are pinless (Inspector knobs, no canvas pins).
      {"AudioReaction", "AudioReaction",
       {{"Level", "Level", "Float", false},
        {"WasHit", "WasHit", "Float", false},
        {"HitCount", "HitCount", "Float", false},
        {"Amplitude", "Amplitude", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider, {}, true},
        {"InputBand", "InputBand", "Float", true, 2.0f, 0.0f, 4.0f, Widget::Enum,
         {"RawFft", "NormalizedFft", "FrequencyBands", "Peaks", "Attacks"}, true},
        {"WindowCenter", "WindowCenter", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowWidth", "WindowWidth", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"WindowEdge", "WindowEdge", "Float", true, 1.0f, 0.0001f, 1.0f, Widget::Slider, {}, true},
        {"Threshold", "Threshold", "Float", true, 0.5f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.1f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"Output", "Output", "Float", true, 3.0f, 0.0f, 4.0f, Widget::Enum,
         {"Pulse", "TimeSinceHit", "Count", "Level", "AccumulatedLevel"}, true},
        {"Bias", "Bias", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider, {}, true},
        {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr},
      // --- Stateful value ops (批次25 stateful-value seam) ---
      // Like AudioReaction: evaluate==nullptr (no pure fn — they keep per-instance memory across
      // frames); cooked once per frame by frame_cook::cookStatefulValueNodes into ResidentNode::
      // extOut; evalResidentFloat reads extOut[outputPortIndex] (generic no-evaluate path). The
      // step math + parity citations live in runtime/stateful_value_ops.cpp.
      // Damp — exponential / critically-damped smoothing toward a target. TiXL float/process/Damp.cs.
      {"Damp", "Damp",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Damping", "Damping", "Float", true, 0.9f, 0.0f, 1.0f},
        {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"LinearInterpolation", "DampedSpring"}}},
       nullptr},
      // DampAngle — Damp in angle space (re-targets through the shortest angular delta). TiXL
      // float/process/DampAngle.cs.
      {"DampAngle", "DampAngle",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -360.0f, 360.0f},
        {"Damping", "Damping", "Float", true, 0.9f, 0.0f, 1.0f},
        {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"LinearInterpolation", "DampedSpring"}}},
       nullptr},
      // DampVec2 / DampVec3 — component-wise Damp. TiXL vec2/DampVec2.cs, vec3/DampVec3.cs.
      // Outputs FIRST (stateful path reads extOut by port index → Result.* must be ports 0..N-1).
      {"DampVec2", "DampVec2",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Damping", "Damping", "Float", true, 0.9f, 0.0f, 1.0f},
        {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"LinearInterpolation", "DampedSpring"}}},
       nullptr},
      {"DampVec3", "DampVec3",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Damping", "Damping", "Float", true, 0.9f, 0.0f, 1.0f},
        {"Method", "Method", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"LinearInterpolation", "DampedSpring"}}},
       nullptr},
      // DeltaSinceLastFrame — Value minus its value last frame. TiXL floats/process/DeltaSinceLastFrame.cs.
      // (Threshold port exists in TiXL but is unused by its math — kept for port parity.)
      {"DeltaSinceLastFrame", "DeltaSinceLastFrame",
       {{"Change", "Change", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr},
      // FreezeValue — sample-and-hold; DeltaSinceFreeze = Value−frozen. TiXL float/process/FreezeValue.cs.
      {"FreezeValue", "FreezeValue",
       {{"Result", "Result", "Float", false},
        {"DeltaSinceFreeze", "DeltaSinceFreeze", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Freeze", "Freeze", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"FreezeWhileTrue", "UpdateWhenSwitchingToTrue"}}},
       nullptr},
      // Spring — spring physics toward a target (overshoots, settles). TiXL float/process/Spring.cs.
      {"Spring", "Spring",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Tension", "Tension", "Float", true, 0.1f, 0.0f, 1.0f},
        {"Strength", "Strength", "Float", true, 0.5f, 0.0f, 4.0f}},
       nullptr},
      // SpringVec2 / SpringVec3 — component-wise Spring. TiXL vec2/process/SpringVec2.cs, vec3/process/SpringVec3.cs.
      {"SpringVec2", "SpringVec2",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Tension", "Tension", "Float", true, 0.1f, 0.0f, 1.0f},
        {"Strength", "Strength", "Float", true, 0.5f, 0.0f, 4.0f}},
       nullptr},
      {"SpringVec3", "SpringVec3",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Tension", "Tension", "Float", true, 0.1f, 0.0f, 1.0f},
        {"Strength", "Strength", "Float", true, 0.5f, 0.0f, 4.0f}},
       nullptr},
      // Ease — time-based eased re-target toward a changing input. TiXL float/process/Ease.cs.
      // Port order = TiXL InputSlot decl order MINUS the dropped UseAppRunTime (named fork): Value,
      // Duration, Direction(enum), Interpolation(enum). Duration default 1.0 (TiXL .cs has no source
      // default; SymbolJson supplies one — 1.0s is the faithful neutral). Stateful: evaluate=nullptr.
      {"Ease", "Ease",
       {{"Result", "Result", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"In", "Out", "InOut"}},
        {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
       nullptr},
      // EaseVec2 / EaseVec3 — component-wise Ease (shared eased-t, no cross-channel bleed). TiXL
      // vec2/process/EaseVec2.cs, vec3/process/EaseVec3.cs. Result.* outputs FIRST (stateful path
      // reads extOut by port index), then Value.* (Vec convention), Duration, Direction, Interpolation.
      {"EaseVec2", "EaseVec2",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"In", "Out", "InOut"}},
        {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
       nullptr},
      {"EaseVec3", "EaseVec3",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false},
        {"Value.x", "Value", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Direction", "Direction", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"In", "Out", "InOut"}},
        {"Interpolation", "Interpolation", "Float", true, 0.0f, 0.0f, 10.0f, Widget::Enum,
         {"Linear", "Sine", "Quad", "Cubic", "Quart", "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce"}}},
       nullptr},
      // --- logic family (batch27). Bool outputs dissolve to Float 0/1 (Cut 32: no Bool port type). ---
      // IsGreater — Result = Value > Threshold. TiXL float/logic/IsGreater.cs (stateless; its
      // _lastResult change-gate is a dirty-flag opt, not load-bearing — see evalIsGreater fork note).
      {"IsGreater", "IsGreater",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Result", "Result", "Float", false}},
       evalIsGreater},
      // Compare — IsTrue per Mode(IsSmaller/IsEqual/IsLarger/IsNotEqual). TiXL float/logic/Compare.cs.
      {"Compare", "Compare",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"TestValue", "TestValue", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"IsSmaller", "IsEqual", "IsLarger", "IsNotEqual"}},
        {"Precision", "Precision", "Float", true, 0.001f, 0.0f, 1.0f},
        {"IsTrue", "IsTrue", "Float", false}},
       evalCompare},
      // HasValueIncreased — HasIncreased = Value > lastValue + Threshold. TiXL float/logic/
      // HasValueIncreased.cs (stateful; output FIRST for the extOut-by-index path; nullptr eval).
      {"HasValueIncreased", "HasValueIncreased",
       {{"HasIncreased", "HasIncreased", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr},
      // HasValueDecreased — HasDecreased = Value < lastValue - Threshold. TiXL float/process/
      // HasValueDecreased.cs (stateful; output FIRST; nullptr eval).
      {"HasValueDecreased", "HasValueDecreased",
       {{"HasDecreased", "HasDecreased", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr},
      // HasValueChanged — 3 outputs (HasChanged 0/1, Delta signed, DeltaOnHit) + change detection by
      // Mode with a MinTimeBetweenHits gate + rising-edge WasTriggered. TiXL float/logic/
      // HasValueChanged.cs. Outputs FIRST (stateful extOut-by-index); inputs in TiXL decl order. All
      // input defaults are type-zero (no InputSlot ctor default in the .cs). Bool dissolves to Float
      // 0/1: HasChanged out = 1/0; PreventContinuedChanges in = Float read as >0.5. Stateful: eval=nullptr.
      {"HasValueChanged", "HasValueChanged",
       {{"HasChanged", "HasChanged", "Float", false},
        {"Delta", "Delta", "Float", false},
        {"DeltaOnHit", "DeltaOnHit", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"Changed", "Increased", "Decreased"}},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.0f, 0.0f, 10.0f},
        {"PreventContinuedChanges", "PreventContinuedChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
      // DetectPulse — fires HasChanged(0/1) on the rising edge of (damped−new) > Threshold, gated by
      // MinTimeBetweenHits; DebugValue = that pre-update delta. TiXL float/process/DetectPulse.cs.
      // Outputs FIRST (stateful extOut-by-index path), then inputs in TiXL decl order. Defaults from
      // DetectPulse.t3: Value=1.0, Threshold=0.0, Damping=0.95, MinTimeBetweenHits=0.075. _lastHitTime
      // inits to −∞ (−1e30f) in the step fn. Bool HasChanged dissolves to Float 0/1. Stateful: eval=nullptr.
      {"DetectPulse", "DetectPulse",
       {{"HasChanged", "HasChanged", "Float", false},
        {"DebugValue", "DebugValue", "Float", false},
        {"Value", "Value", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"Damping", "Damping", "Float", true, 0.95f, 0.0f, 1.0f},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.075f, 0.0f, 2.0f}},
       nullptr},
      // Accumulator — running accumulator; Running gates, ResetTrigger reloads StartValue,
      // Accumulate(PerFrame/PerSeconds), Modulo wraps. TiXL float/process/Accumulator.cs (stateful).
      {"Accumulator", "Accumulator",
       {{"Result", "Result", "Float", false},
        {"Increment", "Increment", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Accumulate", "Accumulate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"PerFrame", "PerSeconds"}},
        {"Running", "Running", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
        {"StartValue", "StartValue", "Float", true, 0.0f, -10.0f, 10.0f},
        {"ResetTrigger", "ResetTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Modulo", "Modulo", "Float", true, 0.0f, 0.0f, 100.0f}},
       nullptr},
      // --- clean stateless vec value ops (batch31, completeness tier). Multi-output: inputs first,
      // outputs after (eval component k = outIdx - n). ---
      // DivideVector2 — (A/B)/UniformScale component-wise. TiXL vec2/DivideVector2.cs. (.t3: A{0,0} B{1,1} U=1)
      {"DivideVector2", "DivideVector2",
       {{"A.x", "A",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"A.y", "A.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x", "B",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"B.y", "B.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, -100.0f, 100.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalDivideVector2},
      // Vec2ToVec3 — (XY.x, XY.y, Z). TiXL vec2/Vec2ToVec3.cs. (.t3: Z=0)
      {"Vec2ToVec3", "Vec2ToVec3",
       {{"XY.x", "XY",  "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"XY.y", "XY.y","Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Z", "Z", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalVec2ToVec3},
      // EulerToAxisAngle — Rotation(radians) → Axis(unit vec3) + Angle(rad). TiXL vec3/EulerToAxisAngle.cs.
      {"EulerToAxisAngle", "EulerToAxisAngle",
       {{"Rotation.x", "Rotation",  "Float", true, 0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 3},
        {"Rotation.y", "Rotation.y","Float", true, 0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 1},
        {"Rotation.z", "Rotation.z","Float", true, 0.0f, -6.2832f, 6.2832f, Widget::Vec, {}, false, 1},
        {"Axis.x", "Axis.x", "Float", false},
        {"Axis.y", "Axis.y", "Float", false},
        {"Axis.z", "Axis.z", "Float", false},
        {"Angle", "Angle", "Float", false}},
       evalEulerToAxisAngle},
      // RemapVec2 — component remap, Mode(Normal/Clamped/Modulo). TiXL vec2/RemapVec2.cs.
      // (.t3: Value{0,0} RangeIn{0,0}/{1,1} RangeOut{0,0}/{1,1} Mode=0)
      {"RemapVec2", "RemapVec2",
       {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RangeInMin.x", "RangeInMin",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"RangeInMin.y", "RangeInMin.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RangeInMax.x", "RangeInMax",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"RangeInMax.y", "RangeInMax.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RangeOutMin.x", "RangeOutMin",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"RangeOutMin.y", "RangeOutMin.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"RangeOutMax.x", "RangeOutMax",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"RangeOutMax.y", "RangeOutMax.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Normal", "Clamped", "Modulo"}},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalRemapVec2},
      // HasVec2Changed — fires when Value moves > Threshold (Euclidean dist). Outputs HasChanged(0/1)
      // + Delta.x/.y. TiXL vec2/HasVec2Changed.cs (stateful; outputs FIRST; nullptr eval).
      {"HasVec2Changed", "HasVec2Changed",
       {{"HasChanged", "HasChanged", "Float", false},
        {"Delta.x", "Delta.x", "Float", false},
        {"Delta.y", "Delta.y", "Float", false},
        {"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.0f, 0.0f, 2.0f},
        {"PreventContinuedChanges", "PreventContinuedChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
      // PadVec2Range — pad/scale a [min,max] range (A.x=min,A.y=max) about center. TiXL vec2/
      // PadVec2Range.cs. (.t3: A{0,0} UniformScale=1 GuaranteedRange{0,0} ClampMinExtend=0)
      {"PadVec2Range", "PadVec2Range",
       {{"A.x", "A",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"A.y", "A.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, -100.0f, 100.0f},
        {"GuaranteedRange.x", "GuaranteedRange",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"GuaranteedRange.y", "GuaranteedRange.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"ClampMinExtend", "ClampMinExtend", "Float", true, 0.0f, 0.0f, 100.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalPadVec2Range},
      // HasVec3Changed — 7-output vec3 change detector. TiXL vec3/HasVec3Changed.cs (stateful; >3-out
      // seam batch33b; outputs FIRST; nullptr eval). Delta=signed, DeltaOnHit=abs Δ at last hit.
      {"HasVec3Changed", "HasVec3Changed",
       {{"HasChanged", "HasChanged", "Float", false},
        {"Delta.x", "Delta.x", "Float", false},
        {"Delta.y", "Delta.y", "Float", false},
        {"Delta.z", "Delta.z", "Float", false},
        {"DeltaOnHit.x", "DeltaOnHit.x", "Float", false},
        {"DeltaOnHit.y", "DeltaOnHit.y", "Float", false},
        {"DeltaOnHit.z", "DeltaOnHit.z", "Float", false},
        {"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Changed", "Increased", "Decreased"}},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.0f, 0.0f, 2.0f},
        {"PreventContinuedChanges", "PreventContinuedChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
      // PeakLevel — 4-output rising-step peak detector + moving sum. TiXL float/process/PeakLevel.cs
      // (stateful; >3-out seam; outputs FIRST; nullptr eval).
      {"PeakLevel", "PeakLevel",
       {{"AttackLevel", "AttackLevel", "Float", false},
        {"FoundPeak", "FoundPeak", "Float", false},
        {"TimeSincePeak", "TimeSincePeak", "Float", false},
        {"MovingSum", "MovingSum", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"MinTimeBetweenPeaks", "MinTimeBetweenPeaks", "Float", true, 0.0f, 0.0f, 2.0f}},
       nullptr},
      // CountInt — running integer counter; steps every evaluated frame TriggerIncrement/
      // TriggerDecrement is held true (LEVEL, faithful to TiXL — with defaults the output free-runs
      // 1,2,3,4,...), reloads DefaultValue on TriggerReset, wraps by Modulo. OnlyCountChanges gates the
      // step to frames where a trigger value CHANGED. TiXL int/logic/CountInt.cs (stateful; outputs
      // FIRST; nullptr eval). .t3 defaults: TriggerIncrement=true, Delta=1, OnlyCountChanges=false.
      // Trigger ports are Bool→Float (>0.5); int ports truncate.
      {"CountInt", "CountInt",
       {{"Result", "Result", "Float", false},
        {"TriggerIncrement", "TriggerIncrement", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
        {"TriggerDecrement", "TriggerDecrement", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"TriggerReset", "TriggerReset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"OnlyCountChanges", "OnlyCountChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Delta", "Delta", "Float", true, 1.0f, -100.0f, 100.0f},
        {"DefaultValue", "DefaultValue", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Modulo", "Modulo", "Float", true, 0.0f, 0.0f, 100.0f}},
       nullptr},
      // FlipBool — latched bool; TOGGLES on the rising edge of Trigger (already rising-edge in the .cs
      // via MathUtils.WasTriggered), reloads DefaultValue on ResetTrigger (reset wins). TiXL bool/logic/
      // FlipBool.cs (stateful; output FIRST; nullptr eval). Bool dissolves to Float 0/1 (Cut 32).
      {"FlipBool", "FlipBool",
       {{"Result", "Result", "Float", false},
        {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"ResetTrigger", "ResetTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"DefaultValue", "DefaultValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
      // HasIntChanged — HasChanged(0/1) when this frame's int-truncated Value differs from last frame's,
      // by Mode. TiXL int/logic/HasIntChanged.cs (stateful; output FIRST; nullptr eval). ReturnTrueIf enum
      // default=Changed(3); Modes: Never(0)/Increased(1)/Decreased(2)/Changed(3). Value int-truncates.
      {"HasIntChanged", "HasIntChanged",
       {{"HasChanged", "HasChanged", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -100.0f, 100.0f},
        {"ReturnTrueIf", "ReturnTrueIf", "Float", true, 3.0f, 0.0f, 3.0f, Widget::Enum,
         {"Never", "Increased", "Decreased", "Changed"}}},
       nullptr},
      // ToggleBoolean — latched bool; FLIPS its held state on TriggerToggle, clears on TriggerReset.
      // TiXL bool/logic/ToggleBoolean.cs writes the trigger input back to false the same frame
      // (SetTypedInputValue) → once-per-press == rising-edge; replicated here as edge detection since
      // our Float ports are read-only at cook time (stateful; output FIRST; nullptr eval). Bool→Float
      // 0/1 (Cut 32). .t3 defaults: TriggerToggle=false, TriggerReset=false.
      {"ToggleBoolean", "ToggleBoolean",
       {{"Result", "Result", "Float", false},
        {"TriggerToggle", "TriggerToggle", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"TriggerReset", "TriggerReset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
      // FlipFlop — latch; SETS to true on a LEVEL Trigger, reloads DefaultValue on a LEVEL ResetTrigger
      // (reset wins), HOLDS otherwise. NO edge gating (contrast FlipBool's toggle, which edges). TiXL
      // bool/logic/FlipFlop.cs (stateful; output FIRST; nullptr eval). Bool→Float 0/1 (Cut 32).
      // .t3 defaults: DefaultValue=false, Trigger=false, ResetTrigger=false.
      {"FlipFlop", "FlipFlop",
       {{"Result", "Result", "Float", false},
        {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"ResetTrigger", "ResetTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"DefaultValue", "DefaultValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr},
      // HasBooleanChanged — HasChanged(0/1) when this frame's bool Value differs from last frame's, by
      // Mode. TiXL bool/logic/HasBooleanChanged.cs (stateful; output FIRST; nullptr eval). Modes enum
      // Changed(0)/Increased(1)/Decreased(2); .t3 DEFAULT Mode=Increased(1) → fires only False→True.
      {"HasBooleanChanged", "HasBooleanChanged",
       {{"HasChanged", "HasChanged", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Mode", "Mode", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
         {"Changed", "Increased", "Decreased"}}},
       nullptr},
      // BlendValues — blend between a MultiInput<float> list by F. TiXL float/process/BlendValues.cs.
      // Values (multiInput) MUST precede the trailing regular F — the eval reads F as in[n-1] and the
      // Values segment as in[0..n-2] (mixed-multiInput convention; no gather change, batch35).
      {"BlendValues", "BlendValues",
       {{"Result", "Result", "Float", false},
        {"Values", "Values", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider, {}, false, 1, true},
        {"F", "F", "Float", true, 0.0f, 0.0f, 100.0f}},
       evalBlendValues},
      {"Const", "Const",
       {{"value", "value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalConst},
      {"Multiply", "Multiply",
       {{"a", "a", "Float", true, 1.0f, -10.0f, 10.0f},
        {"b", "b", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalMultiply},
      // Sum — Σ of a MultiInput Float port (批次25 MultiInput seam). TiXL float/basic/Sum.cs.
      // The single "Input" port accepts N wires; eval expands them into in[] and evalSum reduces.
      // (PortSpec field 12 = multiInput=true.)
      {"Sum", "Sum",
       {{"Result", "Result", "Float", false},
        {"Input", "Input", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider, {}, false, 1, true}},
       evalSum},
      {"Sine", "Sine",
       {{"x", "x", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalSine},
      // --- Math value ops (批次12 lane F; TiXL: Operators/Lib/numbers/float/{basic,adjust,process}/) ---
      // Add: Input1 + Input2 (TiXL Add.cs)
      {"Add", "Add",
       {{"Input1", "Input1", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Input2", "Input2", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalAdd},
      // Sub: Input1 - Input2 (TiXL Sub.cs)
      {"Sub", "Sub",
       {{"Input1", "Input1", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Input2", "Input2", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalSub},
      // Div: A / B; B==0 → 0 (TiXL Div.cs; fork: NaN→0, see evalDiv comment)
      {"Div", "Div",
       {{"A", "A", "Float", true, 1.0f, -10.0f, 10.0f},
        {"B", "B", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalDiv},
      // Clamp: clamp(Value, Min, Max) (TiXL Clamp.cs; MathUtils.Clamp = Min(Max(v,min),max))
      {"Clamp", "Clamp",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Min", "Min", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Max", "Max", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalClamp},
      // Remap: linear remap [RangeInMin..RangeInMax] → [RangeOutMin..RangeOutMax]
      // (TiXL adjust/Remap.cs; fork: BiasAndGain+Mode omitted — see evalRemap comment)
      {"Remap", "Remap",
       {{"Value",       "Value",       "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeInMin",  "RangeInMin",  "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeInMax",  "RangeInMax",  "Float", true, 1.0f, -10.0f, 10.0f},
        {"RangeOutMin", "RangeOutMin", "Float", true, 0.0f, -10.0f, 10.0f},
        {"RangeOutMax", "RangeOutMax", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalRemap},
      // Abs: |Value| (TiXL adjust/Abs.cs)
      {"Abs", "Abs",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalAbs},
      // Floor: truncate toward zero via (int) cast (TiXL adjust/Floor.cs; fork: trunc not floor)
      {"Floor", "Floor",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalFloor},
      // Lerp: A + (B-A)*F (TiXL process/Lerp.cs; fork: Clamp bool input omitted, always unclamped)
      {"Lerp", "Lerp",
       {{"A", "A", "Float", true, 0.0f, -10.0f, 10.0f},
        {"B", "B", "Float", true, 1.0f, -10.0f, 10.0f},
        {"F", "F", "Float", true, 0.5f, 0.0f,   1.0f},
        {"out", "out", "Float", false}},
       evalLerp},
      // [overnight-math] BEGIN specs
      // Sqrt: MathF.Sqrt(Value). TiXL Sqrt.cs. Fork: negative input → 0 (not NaN).
      {"Sqrt", "Sqrt",
       {{"Value", "Value", "Float", true, 1.0f, 0.0f, 100.0f},  // TiXL Sqrt.t3 DefaultValue=1.0
        {"Result", "Result", "Float", false}},
       evalSqrt},
      // Pow: Math.Pow(Value, Exponent). TiXL Pow.cs. No fork from TiXL logic.
      {"Pow", "Pow",
       {{"Value",    "Value",    "Float", true, 1.0f, -10.0f, 10.0f},  // TiXL Pow.t3 DefaultValue=1.0
        {"Exponent", "Exponent", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Result",   "Result",   "Float", false}},
       evalPow},
      // Modulo: floor-modulo. TiXL Modulo.cs. ModuloValue==0 → 0.
      {"Modulo", "Modulo",
       {{"Value",       "Value",       "Float", true, 0.0f, -100.0f, 100.0f},
        {"ModuloValue", "ModuloValue", "Float", true, 1.0f, -100.0f, 100.0f},
        {"Result",      "Result",      "Float", false}},
       evalModulo},
      // Ceil: Math.Ceiling(Value). TiXL Ceil.cs. No fork.
      {"Ceil", "Ceil",
       {{"Value",  "Value",  "Float", true, 0.0f, -100.0f, 100.0f},
        {"Result", "Result", "Float", false}},
       evalCeil},
      // SmoothStep: SmootherStep(Min,Max,Value) = Perlin fade t^3(6t^2-15t+10).
      // TiXL SmoothStep.cs uses MathUtils.SmootherStep (= Fade, NOT classic smoothstep).
      {"SmoothStep", "SmoothStep",
       {{"Min",    "Min",    "Float", true, 0.0f, -10.0f, 10.0f},
        {"Max",    "Max",    "Float", true, 1.0f, -10.0f, 10.0f},
        {"Value",  "Value",  "Float", true, 1.0f, -10.0f, 10.0f},  // TiXL SmoothStep.t3 DefaultValue=1.0
        {"Result", "Result", "Float", false}},
       evalSmoothStep},
      // Log: Math.Log(Value, Base). TiXL Log.cs. Fork: value≤0/base≤0/base=1 → 0 (not NaN).
      {"Log", "Log",
       {{"Value",  "Value",  "Float", true, 1.0f, 0.0f, 100.0f},
        {"Base",   "Base",   "Float", true, 1.0f, 0.0f, 100.0f},  // TiXL Log.t3 DefaultValue=1.0 (base=1 caught by fork below)
        {"Result", "Result", "Float", false}},
       evalLog},
      // Cos: Math.Cos(Input). TiXL Cos.cs. No fork.
      {"Cos", "Cos",
       {{"Input",  "Input",  "Float", true, 0.0f, -10.0f, 10.0f},
        {"Result", "Result", "Float", false}},
       evalCos},
      // [overnight-math] END specs
      // [math-batch22] BEGIN specs
      // Round: quantize Value to StepsPerUnit steps with RoundRatio edge smoothing.
      // TiXL float/adjust/Round.cs. Ports from Round.cs (3 inputs) + Round.t3 defaults:
      //   StepsPerUnit default=1.0, RoundRatio default=0.0, Value default=0.0.
      // Fork: StepsPerUnit==0 → passthrough (TiXL: div-by-zero → NaN).
      {"Round", "Round",
       {{"Value",        "Value",        "Float", true, 0.0f, -100.0f, 100.0f},
        {"StepsPerUnit", "StepsPerUnit", "Float", true, 1.0f, 0.001f,  100.0f},
        {"RoundRatio",   "RoundRatio",   "Float", true, 0.0f, 0.0f,    1.0f},
        {"Result",       "Result",       "Float", false}},
       evalRound},
      // Atan2: atan2(Vector.x, Vector.y). TiXL float/trigonometry/Atan2.cs:
      //   "MathF.Atan2(v.X, v.Y)" — note argument order is (X, Y) not standard (Y, X).
      // Vec2 input decomposed into two Float ports (fork-atan2-arg-order named in evalAtan2).
      // Atan2.t3: Vector default {X:0, Y:0}.
      {"Atan2", "Atan2",
       {{"Vector.x", "Vector",   "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 2},
        {"Vector.y", "Vector.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalAtan2},
      // Sigmoid: 1/(1+e^(Stretch*Value)). TiXL float/adjust/Sigmoid.cs:
      //   "1f/(1+ MathF.Pow(MathF.E, pow * v))"
      // NOTE: NOT standard sigmoid; Stretch is multiplier (positive = decreasing S-curve).
      // Sigmoid.t3: Value default=1.0, Stretch default=1.0.
      {"Sigmoid", "Sigmoid",
       {{"Value",   "Value",   "Float", true, 1.0f, -10.0f, 10.0f},
        {"Stretch", "Stretch", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Result",  "Result",  "Float", false}},
       evalSigmoid},
      // AddVec3: Input1 + Input2 (component-wise). TiXL vec3/AddVec3.cs.
      // Each Vec3 decomposed into 3 Float ports (head Widget::Vec, vecArity=3).
      // Three output pins: Result.x / Result.y / Result.z.
      // AddVec3.t3: both inputs default {X:0, Y:0, Z:0}.
      {"AddVec3", "AddVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalAddVec3},
      // SubVec3: Input1 - Input2 (component-wise). TiXL vec3/SubVec3.cs.
      // SubVec3.t3: both inputs default {X:0, Y:0, Z:0}.
      {"SubVec3", "SubVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalSubVec3},
      // ScaleVector3: A * B * ScaleUniform (component-wise). TiXL vec3/ScaleVector3.cs:
      //   "Result.Value = a * b * u;"
      // ScaleVector3.t3: A default {1,1,1}, B default {1,1,1}, ScaleUniform default=1.0.
      {"ScaleVector3", "ScaleVector3",
       {{"A.x",          "A",           "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"A.y",          "A.y",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"A.z",          "A.z",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x",          "B",           "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"B.y",          "B.y",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.z",          "B.z",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"ScaleUniform", "ScaleUniform","Float", true, 1.0f, -100.0f, 100.0f},
        {"Result.x",     "Result.x",    "Float", false},
        {"Result.y",     "Result.y",    "Float", false},
        {"Result.z",     "Result.z",    "Float", false}},
       evalScaleVector3},
      // [math-batch22] END specs
      // [math-batch23] BEGIN specs
      // Magnitude: length(Input). TiXL vec3/Magnitude.cs:
      //   "Result.Value = Input.GetValue(context).Length();"
      // Input decomposed into 3 Float ports. Single scalar output "Result".
      // Magnitude.t3: Input default {X:0, Y:0, Z:0}.
      {"Magnitude", "Magnitude",
       {{"Input.x", "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input.y", "Input.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input.z", "Input.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",  "Result",  "Float", false}},
       evalMagnitude},
      // DotVec3: dot(Input1, Input2). TiXL vec3/DotVec3.cs:
      //   "Result.Value = Vector3.Dot(Input1.GetValue(context), Input2.GetValue(context));"
      // DotVec3.t3: both inputs default {X:0, Y:0, Z:0}.
      {"DotVec3", "DotVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalDotVec3},
      // Vec3Distance: distance(Input1, Input2). TiXL vec3/Vec3Distance.cs:
      //   "Result.Value = Vector3.Distance(Input1.GetValue(context), Input2.GetValue(context));"
      // Vec3Distance.t3: both inputs default {X:0, Y:0, Z:0}.
      {"Vec3Distance", "Vec3Distance",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalVec3Distance},
      // Vector3Components: decompose Vec3 → X/Y/Z. TiXL vec3/Vector3Components.cs:
      //   "X.Value = value.X; Y.Value = value.Y; Z.Value = value.Z;"
      // Vector3Components.t3: Value default {X:0, Y:0, Z:0}.
      {"Vector3Components", "Vector3Components",
       {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"X", "X", "Float", false},
        {"Y", "Y", "Float", false},
        {"Z", "Z", "Float", false}},
       evalVector3Components},
      // RotateVector3: CreateFromAxisAngle(Axis, Angle°) * VectorA * Scale. TiXL vec3/RotateVector3.cs:
      //   "var angle = Angle.GetValue(context) / 180 * MathF.PI;"
      //   "var m = Matrix4x4.CreateFromAxisAngle(axis, angle);"
      //   "Result.Value = Vector3.TransformNormal(vec, m) * Scale.GetValue(context);"
      // RotateVector3.t3: VectorA default {1,0,0}, Angle default=0, Axis default {0,0,1}, Scale default=1.
      // Fork: fork-angle-degrees (Angle in degrees → radians). fork-axis-normalize.
      // Three output pins: Result.x / Result.y / Result.z.
      {"RotateVector3", "RotateVector3",
       {{"VectorA.x", "VectorA",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"VectorA.y", "VectorA.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"VectorA.z", "VectorA.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Angle",     "Angle",     "Float", true, 0.0f, -360.0f, 360.0f},
        {"Axis.x",    "Axis",      "Float", true, 0.0f, -1.0f,   1.0f,   Widget::Vec, {}, false, 3},
        {"Axis.y",    "Axis.y",    "Float", true, 0.0f, -1.0f,   1.0f,   Widget::Vec, {}, false, 1},
        {"Axis.z",    "Axis.z",    "Float", true, 1.0f, -1.0f,   1.0f,   Widget::Vec, {}, false, 1},
        {"Scale",     "Scale",     "Float", true, 1.0f, -10.0f,  10.0f},
        {"Result.x",  "Result.x",  "Float", false},
        {"Result.y",  "Result.y",  "Float", false},
        {"Result.z",  "Result.z",  "Float", false}},
       evalRotateVector3},
      // [math-batch23] END specs
      // [math-batch24] BEGIN specs
      // InvertFloat: (Invert?-1:1)*A. TiXL float/adjust/InvertFloat.cs.
      // TiXL InvertFloat.t3: A default=1.0, Invert default=true(1.0).
      // Invert is a bool input (TiXL InputSlot<bool>); mapped as Float with Widget::Bool.
      {"InvertFloat", "InvertFloat",
       {{"A",      "A",      "Float", true, 1.0f, -100.0f, 100.0f},
        {"Invert", "Invert", "Float", true, 1.0f, 0.0f,    1.0f, Widget::Bool},
        {"Result", "Result", "Float", false}},
       evalInvertFloat},
      // CrossVec3: Vector3.Cross(Input1, Input2). TiXL vec3/CrossVec3.cs.
      // TiXL CrossVec3.t3: both inputs default {X:0, Y:0, Z:0}.
      {"CrossVec3", "CrossVec3",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalCrossVec3},
      // LerpVec3: Vector3.Lerp(A,B,F) with Clamp bool. TiXL vec3/LerpVec3.cs.
      // TiXL LerpVec3.t3: A default {0,0,0}, B default {0,0,0}, F default=0, Clamp default=false(0).
      // Clamp is a bool input (TiXL InputSlot<bool>); mapped as Float with Widget::Bool.
      {"LerpVec3", "LerpVec3",
       {{"A.x",   "A",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"A.y",   "A.y",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"A.z",   "A.z",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x",   "B",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"B.y",   "B.y",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.z",   "B.z",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"F",     "F",     "Float", true, 0.0f, 0.0f,    1.0f},
        {"Clamp", "Clamp", "Float", true, 0.0f, 0.0f,    1.0f, Widget::Bool},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalLerpVec3},
      // NormalizeVector3: normalize(A)*Factor; guard: length<=0.001 passthrough. TiXL vec3/NormalizeVector3.cs.
      // TiXL NormalizeVector3.t3: A default {0,0,0}, Factor default=1.0.
      // Fork: fork-normalize-zero-guard: TiXL's explicit >0.001f threshold.
      {"NormalizeVector3", "NormalizeVector3",
       {{"A.x",    "A",      "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"A.y",    "A.y",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"A.z",    "A.z",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Factor", "Factor", "Float", true, 1.0f, -10.0f,  10.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalNormalizeVector3},
      // RoundVec3: per-component Round/Floor/Ceil scaled by Precision. TiXL vec3/RoundVec3.cs.
      // TiXL RoundVec3.t3: Value default {0,0,0}, Precision default {1,1,1}, Mode default=0 (Round).
      // Mode is an Enum input (TiXL: Modes {Round,Floor,Ceiling}).
      // Fork: fork-roundvec3-precision-zero: Precision==0 → 0 (TiXL: NaN).
      {"RoundVec3", "RoundVec3",
       {{"Value.x",     "Value",       "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
        {"Value.y",     "Value.y",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Value.z",     "Value.z",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Precision.x", "Precision",   "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 3},
        {"Precision.y", "Precision.y", "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 1},
        {"Precision.z", "Precision.z", "Float", true, 1.0f, 0.001f,  100.0f, Widget::Vec, {}, false, 1},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum, {"Round","Floor","Ceiling"}},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false}},
       evalRoundVec3},
      // AddVec2: Input1+Input2 (component-wise). TiXL vec2/AddVec2.cs.
      // TiXL AddVec2.t3: both inputs default {X:0, Y:0}.
      // Vec2 decomposed into 2 Float ports (head Widget::Vec vecArity=2).
      {"AddVec2", "AddVec2",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalAddVec2},
      // DotVec2: Vector2.Dot(Input1, Input2). TiXL vec2/DotVec2.cs.
      // TiXL DotVec2.t3: both inputs default {X:0, Y:0}. Single scalar output "Result".
      {"DotVec2", "DotVec2",
       {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",   "Result",   "Float", false}},
       evalDotVec2},
      // Vec2Magnitude: length(Input). TiXL vec3/Vec2Magnitude.cs (file lives in vec3/ folder in TiXL).
      // TiXL Vec2Magnitude.t3: Input default {X:0, Y:0}.
      {"Vec2Magnitude", "Vec2Magnitude",
       {{"Input.x", "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Input.y", "Input.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Result",  "Result",  "Float", false}},
       evalVec2Magnitude},
      // Vector2Components: decompose Vec2 → X, Y. TiXL vec2/Vector2Components.cs.
      // TiXL Vector2Components.t3: Value default {X:0, Y:0}.
      {"Vector2Components", "Vector2Components",
       {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"X", "X", "Float", false},
        {"Y", "Y", "Float", false}},
       evalVector2Components},
      // ScaleVector2: A*B*UniformScale (component-wise). TiXL vec2/ScaleVector2.cs:
      //   "Result.Value = a * b * u;"
      // TiXL ScaleVector2.t3: A default {0,0}, B default {1,1}, UniformScale default=1.0.
      {"ScaleVector2", "ScaleVector2",
       {{"A.x",          "A",           "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"A.y",          "A.y",         "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"B.x",          "B",           "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"B.y",          "B.y",         "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale","Float", true, 1.0f, -100.0f, 100.0f},
        {"Result.x",     "Result.x",    "Float", false},
        {"Result.y",     "Result.y",    "Float", false}},
       evalScaleVector2},
      // [math-batch24] END specs
  };
  return specs;
}

}  // namespace sw
