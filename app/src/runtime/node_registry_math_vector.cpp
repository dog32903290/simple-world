// runtime/node_registry_math_vector — self-registering MATH NodeSpec leaf:
// vec2 / vec3 value ops (component math, dot/cross, magnitude, lerp, normalize, rotate,
// remap, decompose) + InvertFloat.
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

      // [math-batch24] BEGIN specs
      // InvertFloat: (Invert?-1:1)*A. TiXL float/adjust/InvertFloat.cs.
      // TiXL InvertFloat.t3: A default=1.0, Invert default=true(1.0).
      // Invert is a bool input (TiXL InputSlot<bool>); mapped as Float with Widget::Bool.
      // NOTE: retained here (old-style MathOp) — no value_op_invertfloat.cpp canonical leaf yet.
static const MathOp _reg_InvertFloat{
      {"InvertFloat", "InvertFloat",
       {{"A",      "A",      "Float", true, 1.0f, -100.0f, 100.0f},
        {"Invert", "Invert", "Float", true, 1.0f, 0.0f,    1.0f, Widget::Bool},
        {"Result", "Result", "Float", false}},
       evalInvertFloat,
       "numbers.float.adjust"}
};

      // PadVec2Range — pad/scale a [min,max] range (A.x=min,A.y=max) about center. TiXL vec2/
      // PadVec2Range.cs. (.t3: A{0,0} UniformScale=1 GuaranteedRange{0,0} ClampMinExtend=0)
      // NOTE: retained here (old-style MathOp) — no value_op_padvec2range.cpp canonical leaf yet.
static const MathOp _reg_PadVec2Range{
      {"PadVec2Range", "PadVec2Range",
       {{"A.x", "A",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"A.y", "A.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"UniformScale", "UniformScale", "Float", true, 1.0f, -100.0f, 100.0f},
        {"GuaranteedRange.x", "GuaranteedRange",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
        {"GuaranteedRange.y", "GuaranteedRange.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
        {"ClampMinExtend", "ClampMinExtend", "Float", true, 0.0f, 0.0f, 100.0f},
        {"Result.x", "Result.x", "Float", false},
        {"Result.y", "Result.y", "Float", false}},
       evalPadVec2Range,
       "numbers.vec2"}
};

}  // namespace
}  // namespace sw
