// runtime/curve_json — Curve <-> crude_json (.swproj v2 animator segment). Split out of
// compound_save.cpp so that file stays one job (ARCHITECTURE rule 4): symbol/connection
// serialization there, Curve/VDefinition serialization here. = TiXL CurveState.Write/Read +
// VDefinition.Write/Read (the keys are ASCII-only — crude_json asserts on non-ASCII bytes, 🪤).
// Runtime leaf: curve.h + crude_json only.
#pragma once

#include "crude_json.h"
#include "runtime/curve.h"

namespace sw {

// One Curve -> {preCurve,postCurve,keys[{t,v,in,out,inTan,outTan,tensionIn,tensionOut,weighted,
// broken}]}. Always writes every field (no TiXL default-omission) so the roundtrip is
// unconditionally byte-stable. Non-finite numbers are clamped to 0 (the S15 writer-gate rule).
crude_json::value curveToJson(const Curve& c);

// Parse one Curve JSON object. Tolerant (S15): missing fields default, malformed keys skipped,
// unknown enum ints clamp (interp->Linear, outside->Constant). Sets keys then computes tangents once.
Curve curveFromJson(crude_json::value& v);

}  // namespace sw
