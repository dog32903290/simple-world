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
      {"Const", "Const",
       {{"value", "value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalConst},
      {"Multiply", "Multiply",
       {{"a", "a", "Float", true, 1.0f, -10.0f, 10.0f},
        {"b", "b", "Float", true, 1.0f, -10.0f, 10.0f},
        {"out", "out", "Float", false}},
       evalMultiply},
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
  };
  return specs;
}

}  // namespace sw
