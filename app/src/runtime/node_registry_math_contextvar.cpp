// runtime/node_registry_math_contextvar — self-registering MATH NodeSpec leaf:
// context-var bridge value ops (Set/Get Float/Int var) + BlendValues.
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

      // --- context-var YELLOW seam (block #1): Set*/Get*Var. STATEFUL in the cook sense (evaluate==
      // nullptr — cooked once per frame into extOut), but their cross-frame channel is the shared
      // ContextVarMap. The Output/Result port is FIRST (extOut[0] index mapping). VariableName is a
      // String input port — the String sub-seam carries its default text in PortSpec.strDef (field 12),
      // since the value rail is float-only; the Float-only resolvers skip dataType=="String" entirely.
      // SetFloatVar — write FloatValue into the named float var. TiXL flow/context/SetFloatVar.cs
      // (no-SubGraph branch :42-45; empty name → no-op :20-24). Output echoes the written value
      // (Command has no value-rail analog — named fork). .t3: FloatValue=0, VariableName="f".
static const MathOp _reg_SetFloatVar{
      {"SetFloatVar", "SetFloatVar",
       {{"Output", "Output", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "f"},
        {"FloatValue", "FloatValue", "Float", true, 0.0f, -1000.0f, 1000.0f}},
       nullptr}
};

      // GetFloatVar — read the named float var, else FallbackDefault. TiXL flow/context/GetFloatVar.cs
      // (:14-28). DROP ICustomDropdownHolder (var-name dropdown UI). .t3: VariableName="f", Fallback=0.
static const MathOp _reg_GetFloatVar{
      {"GetFloatVar", "GetFloatVar",
       {{"Result", "Result", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "f"},
        {"FallbackDefault", "FallbackDefault", "Float", true, 0.0f, -1000.0f, 1000.0f}},
       nullptr}
};

      // SetIntVar — write (int)Value into the named int var (truncate toward zero). TiXL flow/context/
      // SetIntVar.cs (no-SubGraph :61-64). Value on a Float port (no Int port type). .t3: Value=0, Name="i".
static const MathOp _reg_SetIntVar{
      {"SetIntVar", "SetIntVar",
       {{"Output", "Output", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "i"},
        {"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f}},
       nullptr}
};

      // GetIntVar — read the named int var, else FallbackValue (both int-truncated). TiXL flow/context/
      // GetIntVar.cs (:16-50). DROP LogLevels enum + dropdown. .t3: VariableName="i", FallbackValue=0.
static const MathOp _reg_GetIntVar{
      {"GetIntVar", "GetIntVar",
       {{"Result", "Result", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "i"},
        {"FallbackValue", "FallbackValue", "Float", true, 0.0f, -1000.0f, 1000.0f}},
       nullptr}
};

      // BlendValues — blend between a MultiInput<float> list by F. TiXL float/process/BlendValues.cs.
      // Values (multiInput) MUST precede the trailing regular F — the eval reads F as in[n-1] and the
      // Values segment as in[0..n-2] (mixed-multiInput convention; no gather change, batch35).
static const MathOp _reg_BlendValues{
      {"BlendValues", "BlendValues",
       {{"Result", "Result", "Float", false},
        {"Values", "Values", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider, {}, false, 1, true},
        {"F", "F", "Float", true, 0.0f, 0.0f, 100.0f}},
       evalBlendValues}
};

}  // namespace
}  // namespace sw
