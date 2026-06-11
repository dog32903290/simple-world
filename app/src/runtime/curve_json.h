// runtime/curve_json — Curve <-> crude_json (.swproj v2 animator segment). Split out of
// compound_save.cpp so that file stays one job (ARCHITECTURE rule 4): symbol/connection
// serialization there, Curve/VDefinition serialization here. = TiXL CurveState.Write/Read +
// VDefinition.Write/Read (the keys are ASCII-only — crude_json asserts on non-ASCII bytes, 🪤).
// Runtime leaf: curve.h + crude_json only.
#pragma once

#include <vector>

#include "crude_json.h"
#include "runtime/curve.h"

namespace sw {

// One Curve -> {preCurve,postCurve,keys[{t,v,in,out,inTan,outTan,tensionIn,tensionOut,weighted,
// broken}]}. Always writes every field (no TiXL default-omission) so the roundtrip is
// unconditionally byte-stable. Non-finite numbers are clamped to 0 (the S15 writer-gate rule).
crude_json::value curveToJson(const Curve& c);

// Parse one Curve JSON object. Tolerant (S15): missing fields default, malformed keys skipped,
// unknown enum ints clamp (interp->Linear, outside->Constant). Sets keys then computes tangents once.
// `droppedKeys` (optional out): incremented per key SKIPPED because its time/value was missing or
// non-finite (NaN/Inf) — the loader (compound_save) turns a non-zero count into an S15 warning so a
// silently-dropped key is visible (was previously a silent skip). Pass nullptr to ignore.
Curve curveFromJson(crude_json::value& v, int* droppedKeys = nullptr);

// One channel-array (CurveArray = vector<Curve>, one Curve per scalar channel) <-> a JSON array of
// curve objects. Used by the clipboard (copy/paste carries a copied child's animation curves) — the
// curve serialization SSOT so the clipboard and the .swproj animator segment never fork. parse is
// S15-tolerant: a non-object array element is skipped (a hostile clipboard can't abort), each curve
// is parsed by curveFromJson. droppedKeys (optional) accumulates malformed/NaN key drops across all
// curves in the array.
crude_json::value curveArrayToJson(const std::vector<Curve>& arr);
std::vector<Curve> curveArrayFromJson(crude_json::value& v, int* droppedKeys = nullptr);

}  // namespace sw
