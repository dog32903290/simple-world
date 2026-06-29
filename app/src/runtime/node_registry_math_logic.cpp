// runtime/node_registry_math_logic — self-registering MATH NodeSpec leaf:
// logic / change-detection value ops (compare, has-changed, pulse, accumulator, latches,
// triggers, counters).
//
// Split from the 980-line node_registry_math.cpp (ratchet debt, ARCHITECTURE rule 4 + rule 7).
// Every spec below is moved VERBATIM from the old mathSpecs() manifest — name / ports / widgets /
// defaults / evaluate binding unchanged. Adding a math op here = drop a MathOp registrar; the
// central manifest is never touched again (mirror of the point-modify / image-filter / value-op
// self-registration sinks). Stateless ops carry their pure evaluate fn; stateful ops carry
// nullptr (cooked by frame_cook's stateful-value seam, dispatched by type name).
#include "runtime/graph.h"            // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink
#include "runtime/value_eval_ops.h"    // evalAdd, evalSine, evalClamp, … (pure value-node fns)

namespace sw {
namespace {

      // --- logic family (batch27). Bool outputs dissolve to Float 0/1 (Cut 32: no Bool port type). ---
      // IsGreater — Result = Value > Threshold. TiXL float/logic/IsGreater.cs (stateless; its
      // _lastResult change-gate is a dirty-flag opt, not load-bearing — see evalIsGreater fork note).
static const MathOp _reg_IsGreater{
      {"IsGreater", "IsGreater",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Result", "Result", "Float", false}},
       evalIsGreater,
       "numbers.float.logic"}
};

      // Compare — IsTrue per Mode(IsSmaller/IsEqual/IsLarger/IsNotEqual). TiXL float/logic/Compare.cs.
static const MathOp _reg_Compare{
      {"Compare", "Compare",
       {{"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"TestValue", "TestValue", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"IsSmaller", "IsEqual", "IsLarger", "IsNotEqual"}},
        {"Precision", "Precision", "Float", true, 0.001f, 0.0f, 1.0f},
        {"IsTrue", "IsTrue", "Float", false}},
       evalCompare,
       "numbers.float.logic"}
};

      // HasValueIncreased — HasIncreased = Value > lastValue + Threshold. TiXL float/logic/
      // HasValueIncreased.cs (stateful; output FIRST for the extOut-by-index path; nullptr eval).
static const MathOp _reg_HasValueIncreased{
      {"HasValueIncreased", "HasValueIncreased",
       {{"HasIncreased", "HasIncreased", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr,
       "numbers.float.logic"}
};

      // HasValueDecreased — HasDecreased = Value < lastValue - Threshold. TiXL float/process/
      // HasValueDecreased.cs (stateful; output FIRST; nullptr eval).
static const MathOp _reg_HasValueDecreased{
      {"HasValueDecreased", "HasValueDecreased",
       {{"HasDecreased", "HasDecreased", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f}},
       nullptr,
       "numbers.float.process"}
};

      // HasValueChanged — 3 outputs (HasChanged 0/1, Delta signed, DeltaOnHit) + change detection by
      // Mode with a MinTimeBetweenHits gate + rising-edge WasTriggered. TiXL float/logic/
      // HasValueChanged.cs. Outputs FIRST (stateful extOut-by-index); inputs in TiXL decl order. All
      // input defaults are type-zero (no InputSlot ctor default in the .cs). Bool dissolves to Float
      // 0/1: HasChanged out = 1/0; PreventContinuedChanges in = Float read as >0.5. Stateful: eval=nullptr.
static const MathOp _reg_HasValueChanged{
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
       nullptr,
       "numbers.float.logic"}
};

      // DetectPulse — fires HasChanged(0/1) on the rising edge of (damped−new) > Threshold, gated by
      // MinTimeBetweenHits; DebugValue = that pre-update delta. TiXL float/process/DetectPulse.cs.
      // Outputs FIRST (stateful extOut-by-index path), then inputs in TiXL decl order. Defaults from
      // DetectPulse.t3: Value=1.0, Threshold=0.0, Damping=0.95, MinTimeBetweenHits=0.075. _lastHitTime
      // inits to −∞ (−1e30f) in the step fn. Bool HasChanged dissolves to Float 0/1. Stateful: eval=nullptr.
static const MathOp _reg_DetectPulse{
      {"DetectPulse", "DetectPulse",
       {{"HasChanged", "HasChanged", "Float", false},
        {"DebugValue", "DebugValue", "Float", false},
        {"Value", "Value", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"Damping", "Damping", "Float", true, 0.95f, 0.0f, 1.0f},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.075f, 0.0f, 2.0f}},
       nullptr,
       "numbers.float.process"}
};

      // Accumulator — running accumulator; Running gates, ResetTrigger reloads StartValue,
      // Accumulate(PerFrame/PerSeconds), Modulo wraps. TiXL float/process/Accumulator.cs (stateful).
static const MathOp _reg_Accumulator{
      {"Accumulator", "Accumulator",
       {{"Result", "Result", "Float", false},
        {"Increment", "Increment", "Float", true, 1.0f, -10.0f, 10.0f},
        {"Accumulate", "Accumulate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"PerFrame", "PerSeconds"}},
        {"Running", "Running", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
        {"StartValue", "StartValue", "Float", true, 0.0f, -10.0f, 10.0f},
        {"ResetTrigger", "ResetTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Modulo", "Modulo", "Float", true, 0.0f, 0.0f, 100.0f}},
       nullptr,
       "numbers.float.process"}
};

      // HasVec2Changed — fires when Value moves > Threshold (Euclidean dist). Outputs HasChanged(0/1)
      // + Delta.x/.y. TiXL vec2/HasVec2Changed.cs (stateful; outputs FIRST; nullptr eval).
static const MathOp _reg_HasVec2Changed{
      {"HasVec2Changed", "HasVec2Changed",
       {{"HasChanged", "HasChanged", "Float", false},
        {"Delta.x", "Delta.x", "Float", false},
        {"Delta.y", "Delta.y", "Float", false},
        {"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"MinTimeBetweenHits", "MinTimeBetweenHits", "Float", true, 0.0f, 0.0f, 2.0f},
        {"PreventContinuedChanges", "PreventContinuedChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "numbers.vec2"}
};

      // HasVec3Changed — 7-output vec3 change detector. TiXL vec3/HasVec3Changed.cs (stateful; >3-out
      // seam batch33b; outputs FIRST; nullptr eval). Delta=signed, DeltaOnHit=abs Δ at last hit.
static const MathOp _reg_HasVec3Changed{
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
       nullptr,
       "numbers.vec3"}
};

      // PeakLevel — 4-output rising-step peak detector + moving sum. TiXL float/process/PeakLevel.cs
      // (stateful; >3-out seam; outputs FIRST; nullptr eval).
static const MathOp _reg_PeakLevel{
      {"PeakLevel", "PeakLevel",
       {{"AttackLevel", "AttackLevel", "Float", false},
        {"FoundPeak", "FoundPeak", "Float", false},
        {"TimeSincePeak", "TimeSincePeak", "Float", false},
        {"MovingSum", "MovingSum", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -10.0f, 10.0f},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 10.0f},
        {"MinTimeBetweenPeaks", "MinTimeBetweenPeaks", "Float", true, 0.0f, 0.0f, 2.0f}},
       nullptr,
       "numbers.float.process"}
};

      // CountInt — running integer counter; steps every evaluated frame TriggerIncrement/
      // TriggerDecrement is held true (LEVEL, faithful to TiXL — with defaults the output free-runs
      // 1,2,3,4,...), reloads DefaultValue on TriggerReset, wraps by Modulo. OnlyCountChanges gates the
      // step to frames where a trigger value CHANGED. TiXL int/logic/CountInt.cs (stateful; outputs
      // FIRST; nullptr eval). .t3 defaults: TriggerIncrement=true, Delta=1, OnlyCountChanges=false.
      // Trigger ports are Bool→Float (>0.5); int ports truncate.
static const MathOp _reg_CountInt{
      {"CountInt", "CountInt",
       {{"Result", "Result", "Float", false},
        {"TriggerIncrement", "TriggerIncrement", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
        {"TriggerDecrement", "TriggerDecrement", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"TriggerReset", "TriggerReset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"OnlyCountChanges", "OnlyCountChanges", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Delta", "Delta", "Float", true, 1.0f, -100.0f, 100.0f},
        {"DefaultValue", "DefaultValue", "Float", true, 0.0f, -100.0f, 100.0f},
        {"Modulo", "Modulo", "Float", true, 0.0f, 0.0f, 100.0f}},
       nullptr,
       "numbers.int.logic"}
};

      // FlipBool — latched bool; TOGGLES on the rising edge of Trigger (already rising-edge in the .cs
      // via MathUtils.WasTriggered), reloads DefaultValue on ResetTrigger (reset wins). TiXL bool/logic/
      // FlipBool.cs (stateful; output FIRST; nullptr eval). Bool dissolves to Float 0/1 (Cut 32).
static const MathOp _reg_FlipBool{
      {"FlipBool", "FlipBool",
       {{"Result", "Result", "Float", false},
        {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"ResetTrigger", "ResetTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"DefaultValue", "DefaultValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "numbers.bool.logic"}
};

      // HasIntChanged — HasChanged(0/1) when this frame's int-truncated Value differs from last frame's,
      // by Mode. TiXL int/logic/HasIntChanged.cs (stateful; output FIRST; nullptr eval). ReturnTrueIf enum
      // default=Changed(3); Modes: Never(0)/Increased(1)/Decreased(2)/Changed(3). Value int-truncates.
static const MathOp _reg_HasIntChanged{
      {"HasIntChanged", "HasIntChanged",
       {{"HasChanged", "HasChanged", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, -100.0f, 100.0f},
        {"ReturnTrueIf", "ReturnTrueIf", "Float", true, 3.0f, 0.0f, 3.0f, Widget::Enum,
         {"Never", "Increased", "Decreased", "Changed"}}},
       nullptr,
       "numbers.int.logic"}
};

      // ToggleBoolean — latched bool; FLIPS its held state on TriggerToggle, clears on TriggerReset.
      // TiXL bool/logic/ToggleBoolean.cs writes the trigger input back to false the same frame
      // (SetTypedInputValue) → once-per-press == rising-edge; replicated here as edge detection since
      // our Float ports are read-only at cook time (stateful; output FIRST; nullptr eval). Bool→Float
      // 0/1 (Cut 32). .t3 defaults: TriggerToggle=false, TriggerReset=false.
static const MathOp _reg_ToggleBoolean{
      {"ToggleBoolean", "ToggleBoolean",
       {{"Result", "Result", "Float", false},
        {"TriggerToggle", "TriggerToggle", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"TriggerReset", "TriggerReset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "numbers.bool.logic"}
};

      // FlipFlop — latch; SETS to true on a LEVEL Trigger, reloads DefaultValue on a LEVEL ResetTrigger
      // (reset wins), HOLDS otherwise. NO edge gating (contrast FlipBool's toggle, which edges). TiXL
      // bool/logic/FlipFlop.cs (stateful; output FIRST; nullptr eval). Bool→Float 0/1 (Cut 32).
      // .t3 defaults: DefaultValue=false, Trigger=false, ResetTrigger=false.
static const MathOp _reg_FlipFlop{
      {"FlipFlop", "FlipFlop",
       {{"Result", "Result", "Float", false},
        {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"ResetTrigger", "ResetTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"DefaultValue", "DefaultValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "numbers.bool.logic"}
};

      // HasBooleanChanged — HasChanged(0/1) when this frame's bool Value differs from last frame's, by
      // Mode. TiXL bool/logic/HasBooleanChanged.cs (stateful; output FIRST; nullptr eval). Modes enum
      // Changed(0)/Increased(1)/Decreased(2); .t3 DEFAULT Mode=Increased(1) → fires only False→True.
static const MathOp _reg_HasBooleanChanged{
      {"HasBooleanChanged", "HasBooleanChanged",
       {{"HasChanged", "HasChanged", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Mode", "Mode", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
         {"Changed", "Increased", "Decreased"}}},
       nullptr,
       "numbers.bool.logic"}
};

      // Trigger — bool gate: passes BoolValue through, OR (OnlyOnDown=true, the .t3 default) emits a
      // one-frame pulse on the RISING edge of BoolValue. TiXL bool/logic/Trigger.cs (stateful; output
      // FIRST; nullptr eval). Bool→Float 0/1 (Cut 32). .t3 defaults: OnlyOnDown=true, BoolValue=false.
      // ColorInGraph (Vec4 cosmetic — Trigger.cs l.30: ColorInGraph.DirtyFlag.Clear(); never touches
      // the output). Vec4 decomposed as 4 Float ports (fork-vec4-decompose-arity convention).
      // .t3 default: ColorInGraph={X:0.5359,Y:0.5359,Z:0.5359,W:0.5885} (a UI grey; dead knob).
static const MathOp _reg_Trigger{
      {"Trigger", "Trigger",
       {{"Result",       "Result",       "Float", false},
        {"BoolValue",    "BoolValue",    "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"OnlyOnDown",   "OnlyOnDown",   "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
        {"ColorInGraph.x", "ColorInGraph",   "Float", true, 0.5359f, 0.0f, 1.0f, Widget::Vec, {}, false, 4},
        {"ColorInGraph.y", "ColorInGraph.y", "Float", true, 0.5359f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"ColorInGraph.z", "ColorInGraph.z", "Float", true, 0.5359f, 0.0f, 1.0f, Widget::Vec, {}, false, 1},
        {"ColorInGraph.w", "ColorInGraph.w", "Float", true, 0.5885f, 0.0f, 1.0f, Widget::Vec, {}, false, 1}},
       nullptr,
       "numbers.bool.logic"}
};

      // KeepBoolean — bool sample-and-hold (the bool twin of FreezeValue) + a TimeSinceFreeze clock.
      // TiXL bool/process/KeepBoolean.cs (stateful; output FIRST; nullptr eval). Bool→Float 0/1 (Cut 32).
      // .t3 defaults: Value=false, Mode=FreezeWhileTrue(0), Freeze=false.
static const MathOp _reg_KeepBoolean{
      {"KeepBoolean", "KeepBoolean",
       {{"Result", "Result", "Float", false},
        {"TimeSinceFreeze", "TimeSinceFreeze", "Float", false},
        {"Value", "Value", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Freeze", "Freeze", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"FreezeWhileTrue", "UpdateWhenSwitchingToTrue"}}},
       nullptr,
       "numbers.bool.process"}
};

      // WasTrigger — fires a one-frame TRUE on the rising edge of a named trigger VARIABLE rising.
      // TiXL numbers/bool/logic/WasTrigger.cs. STATEFUL in the cook sense (evaluate==nullptr): it
      // reads context.FloatVariables (the host ContextVarMap.floatVars, the SAME channel Set/GetFloatVar
      // use) for the selected trigger source, then a cross-frame rising-edge gate (_lastValue/_wasHit).
      // It is a READER (NOT in isContextVarWriter) → runs in the reader pass after every Set*Var writer.
      // Output WasTriggered FIRST (extOut[0]). Trigger enum = {None,TriggerA,TriggerB,Custom}, .t3
      // default 1=TriggerA (reads floatVars["__TriggerA"]). NAMED FORK fork-wastrigger-varname-channel:
      // TiXL's string input is `CustomVariableName`; sw names it `VariableName` so it rides the rail's
      // canonical string channel (frame_cook resolves strInputs["VariableName"]) — without this rename
      // the Custom mode would be a DEAD knob (the rail never resolves any other string-port name). The
      // value semantics are byte-identical; only the inspector label diverges. .t3: VariableName="".
static const MathOp _reg_WasTrigger{
      {"WasTrigger", "WasTrigger",
       {{"WasTriggered", "WasTriggered", "Float", false},
        {"Trigger", "Trigger", "Float", true, 1.0f, 0.0f, 3.0f, Widget::Enum,
         {"None", "TriggerA", "TriggerB", "Custom"}},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
       nullptr,
       "numbers.bool.logic"}
};

}  // namespace
}  // namespace sw
