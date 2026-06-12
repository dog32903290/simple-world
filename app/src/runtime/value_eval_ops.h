// runtime/value_eval_ops — pure value-node evaluate functions (no GPU), mechanically split
// out of node_registry.cpp (批次12-F, ARCHITECTURE rule 4: <400). One fn per value op;
// in[] is ordered by the Float input ports in the spec; n is the count. TiXL citations and
// named forks live next to each definition in the .cpp.
#pragma once

// EvaluationContext is a GLOBAL-namespace dual-compiled struct (eval_context.h) — forward
// declared OUTSIDE sw on purpose; nesting it inside sw would mint a second, incomplete type.
struct EvaluationContext;

namespace sw {

float evalTime(int, const float*, int, const EvaluationContext& ctx);
float evalConst(int, const float* in, int n, const EvaluationContext&);
float evalMultiply(int, const float* in, int n, const EvaluationContext&);
float evalSine(int, const float* in, int n, const EvaluationContext&);
float evalAdd(int, const float* in, int n, const EvaluationContext&);
float evalSub(int, const float* in, int n, const EvaluationContext&);
float evalDiv(int, const float* in, int n, const EvaluationContext&);
float evalClamp(int, const float* in, int n, const EvaluationContext&);
float evalRemap(int, const float* in, int n, const EvaluationContext&);
float evalAbs(int, const float* in, int n, const EvaluationContext&);
float evalFloor(int, const float* in, int n, const EvaluationContext&);
float evalLerp(int, const float* in, int n, const EvaluationContext&);
}  // namespace sw
