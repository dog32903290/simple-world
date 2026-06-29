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
       nullptr,
       "flow.context"}
};

      // GetFloatVar — read the named float var, else FallbackDefault. TiXL flow/context/GetFloatVar.cs
      // (:14-28). DROP ICustomDropdownHolder (var-name dropdown UI). .t3: VariableName="f", Fallback=0.
static const MathOp _reg_GetFloatVar{
      {"GetFloatVar", "GetFloatVar",
       {{"Result", "Result", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "f"},
        {"FallbackDefault", "FallbackDefault", "Float", true, 0.0f, -1000.0f, 1000.0f}},
       nullptr,
       "flow.context"}
};

      // SetIntVar — write (int)Value into the named int var (truncate toward zero). TiXL flow/context/
      // SetIntVar.cs (no-SubGraph :61-64). Value on a Float port (no Int port type). .t3: Value=0, Name="i".
      // LogLevel (param-completion fan-out): MappedType<LogLevels> int input (SetIntVar.cs:77-78); sw renders
      // it as a Float port + Enum widget carrying the 4 level names (no new mapping basework — sw Widget::Enum
      // already exists, the MappedType is just an int dropdown). Default 0=None (SetIntVar.t3 DefaultValue=0).
      // Drives the empty-name warning via ctxVarLogSink (stepSetIntVar). KNOWN FORKS (recorded in
      // nodespec_integrity.sh): SubGraph(Command) + ClearAfterExecution live on the Cmd-rail SetIntVarCmd —
      // the value rail (no-SubGraph branch) has no Command output nor an after-subtree clear.
static const MathOp _reg_SetIntVar{
      {"SetIntVar", "SetIntVar",
       {{"Output", "Output", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "i"},
        {"Value", "Value", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"LogLevel", "LogLevel", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"None", "Warnings", "Changes", "AllUpdates"}, true}},
       nullptr,
       "flow.context"}
};

      // GetIntVar — read the named int var, else FallbackValue (both int-truncated). TiXL flow/context/
      // GetIntVar.cs (:16-50). DROP ICustomDropdownHolder (var-name dropdown UI). .t3: VariableName="i",
      // FallbackValue=0. LogUpdates (param-completion fan-out): MappedType<LogLevels> int input
      // (GetIntVar.cs:60-61) → Float port + Enum widget (4 level names). Default 0=None (GetIntVar.t3
      // DefaultValue=0). Drives the read-debug / undefined-warning emission via ctxVarLogSink (stepGetIntVar).
static const MathOp _reg_GetIntVar{
      {"GetIntVar", "GetIntVar",
       {{"Result", "Result", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "i"},
        {"FallbackValue", "FallbackValue", "Float", true, 0.0f, -1000.0f, 1000.0f},
        {"LogUpdates", "LogUpdates", "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
         {"None", "Warnings", "Changes", "AllUpdates"}, true}},
       nullptr,
       "flow.context"}
};

      // SetBoolVar — write the bool (0/1) into the named var on the INT channel (sw has no boolVars dict;
      // NAMED FORK: TiXL flow/context/SetBoolVar.cs uses context.BoolVariables, sw collapses bool→intVars as
      // 0/1 since ContextVarMap carries only floatVars+intVars). no-SubGraph branch (SetBoolVar.cs:39); empty
      // name → no-op (cs:19-23). BoolValue on a Float port (no Bool port type) → !=0 ⇒ 1. .t3: BoolValue=false,
      // VariableName="b". The SubGraph (Command) half is the separate command-rail SetBoolVarCmd (two-rail).
static const MathOp _reg_SetBoolVar{
      {"SetBoolVar", "SetBoolVar",
       {{"Output", "Output", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "b"},
        {"BoolValue", "BoolValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "flow.context"}
};

      // GetBoolVar — read the named bool var off the INT channel (!=0 ⇒ 1), else FallbackDefault (!=0 ⇒ 1).
      // TiXL flow/context/GetBoolVar.cs (:15-29). DROP ICustomDropdownHolder (var-name dropdown UI). NAMED FORK:
      // reads intVars (sw has no boolVars). .t3: VariableName="b", FallbackDefault=false.
static const MathOp _reg_GetBoolVar{
      {"GetBoolVar", "GetBoolVar",
       {{"Result", "Result", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "b"},
        {"FallbackDefault", "FallbackDefault", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "flow.context"}
};

      // SetVec3Var (sub-seam B) — write Vec3Value into the named vec3 var (typed vec3 channel; NAMED
      // FORK fork-ctxvar-vec3-typed-channel, see stateful_value_ops.h). STATEFUL in the cook sense
      // (evaluate==nullptr, cooked into extOut by the 2-pass writer-before-reader cook). Vec3 = 3 Float
      // ports (sw vec-as-3-floats, like AddVec3): the Vec3Value.x head carries Widget::Vec arity 3 so the
      // Inspector draws ONE block; the y/z components are plain arity-1 Floats. Output ports FIRST
      // (extOut[0..2]) — Command has no value-rail analog, so Output.x/.y/.z ECHO the written vec3 (golden
      // probe). TiXL flow/context/SetVec3Var.cs (no-SubGraph branch :42-44; empty name → no-op :20-24).
      // .t3: VariableName="pos", Vec3Value=(0,0,0).
static const MathOp _reg_SetVec3Var{
      {"SetVec3Var", "SetVec3Var",
       {{"Output.x", "Output.x", "Float", false},
        {"Output.y", "Output.y", "Float", false},
        {"Output.z", "Output.z", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "pos"},
        {"Vec3Value.x", "Vec3Value",   "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 3},
        {"Vec3Value.y", "Vec3Value.y", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 1},
        {"Vec3Value.z", "Vec3Value.z", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 1}},
       nullptr,
       "flow.context"}
};

      // GetVec3Var (sub-seam B) — read the named vec3 var onto Result.x/.y/.z, else FallbackDefault.
      // TiXL flow/context/GetVec3Var.cs (:24-31, `is Vector3` cast → plain typed-map hit here). DROP
      // ICustomDropdownHolder (var-name dropdown UI). Result ports FIRST (extOut[0..2]). FallbackDefault.x
      // is the Widget::Vec head (arity 3). .t3: VariableName="pos", FallbackDefault=(0,0,0).
static const MathOp _reg_GetVec3Var{
      {"GetVec3Var", "GetVec3Var",
       {{"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false},
        {"Result.z", "Result.z", "Float", false},
        {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "pos"},
        {"FallbackDefault.x", "FallbackDefault",   "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 3},
        {"FallbackDefault.y", "FallbackDefault.y", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 1},
        {"FallbackDefault.z", "FallbackDefault.z", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 1}},
       nullptr,
       "flow.context"}
};

      // BlendValues — blend between a MultiInput<float> list by F. TiXL float/process/BlendValues.cs.
      // Values (multiInput) MUST precede the trailing regular F — the eval reads F as in[n-1] and the
      // Values segment as in[0..n-2] (mixed-multiInput convention; no gather change, batch35).
static const MathOp _reg_BlendValues{
      {"BlendValues", "BlendValues",
       {{"Result", "Result", "Float", false},
        {"Values", "Values", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Slider, {}, false, 1, true},
        {"F", "F", "Float", true, 0.0f, 0.0f, 100.0f}},
       evalBlendValues,
       "numbers.float.process"}
};

}  // namespace
}  // namespace sw
