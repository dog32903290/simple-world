// runtime/curve_json — implementation. = TiXL CurveState.Write/Read + VDefinition.Write/Read.
#include "runtime/curve_json.h"

#include <cmath>

namespace sw {
namespace {

crude_json::number finiteOr0(double v) { return std::isfinite(v) ? (crude_json::number)v : 0.0; }

KeyInterpolation interpFromInt(int i) {
  if (i < 0 || i > 5) return KeyInterpolation::Linear;  // S15: clamp unknown enum -> Linear
  return (KeyInterpolation)i;
}
OutsideBehavior outsideFromInt(int i) {
  if (i < 0 || i > 3) return OutsideBehavior::Constant;
  return (OutsideBehavior)i;
}

}  // namespace

crude_json::value curveToJson(const Curve& c) {
  crude_json::object o;
  o["preCurve"] = (crude_json::number)(int)c.preCurveMapping;
  o["postCurve"] = (crude_json::number)(int)c.postCurveMapping;
  crude_json::array keys;
  for (const auto& kv : c.table()) {
    const VDefinition& d = kv.second;
    crude_json::object ko;
    ko["t"] = finiteOr0(kv.first);
    ko["v"] = finiteOr0(d.value);
    ko["in"] = (crude_json::number)(int)d.inInterpolation;
    ko["out"] = (crude_json::number)(int)d.outInterpolation;
    ko["inTan"] = finiteOr0(d.inTangentAngle);
    ko["outTan"] = finiteOr0(d.outTangentAngle);
    ko["tensionIn"] = finiteOr0(d.tensionIn);
    ko["tensionOut"] = finiteOr0(d.tensionOut);
    ko["weighted"] = d.weighted;
    ko["broken"] = d.brokenTangents;
    keys.push_back(crude_json::value(ko));
  }
  o["keys"] = crude_json::value(keys);
  return crude_json::value(o);
}

Curve curveFromJson(crude_json::value& v, int* droppedKeys) {
  Curve c;
  if (v["preCurve"].is_number())
    c.preCurveMapping = outsideFromInt((int)v["preCurve"].get<crude_json::number>());
  if (v["postCurve"].is_number())
    c.postCurveMapping = outsideFromInt((int)v["postCurve"].get<crude_json::number>());
  if (v["keys"].is_array()) {
    for (auto& kv : v["keys"].get<crude_json::array>()) {
      // Drop a key whose time/value is missing OR non-finite (NaN/Inf). The writer clamps non-finite
      // to 0, but a hand-crafted/tampered file (S15) can carry NaN — skip it and count the drop so the
      // loader can warn (was a silent skip).
      if (!kv["t"].is_number() || !kv["v"].is_number() ||
          !std::isfinite(kv["t"].get<crude_json::number>()) ||
          !std::isfinite(kv["v"].get<crude_json::number>())) {
        if (droppedKeys) ++*droppedKeys;
        continue;  // malformed/NaN key dropped
      }
      VDefinition d;
      d.value = kv["v"].get<crude_json::number>();
      if (kv["in"].is_number()) d.inInterpolation = interpFromInt((int)kv["in"].get<crude_json::number>());
      if (kv["out"].is_number()) d.outInterpolation = interpFromInt((int)kv["out"].get<crude_json::number>());
      if (kv["inTan"].is_number()) d.inTangentAngle = kv["inTan"].get<crude_json::number>();
      if (kv["outTan"].is_number()) d.outTangentAngle = kv["outTan"].get<crude_json::number>();
      if (kv["tensionIn"].is_number()) d.tensionIn = (float)kv["tensionIn"].get<crude_json::number>();
      if (kv["tensionOut"].is_number()) d.tensionOut = (float)kv["tensionOut"].get<crude_json::number>();
      if (kv["weighted"].is_boolean()) d.weighted = kv["weighted"].get<crude_json::boolean>();
      if (kv["broken"].is_boolean()) d.brokenTangents = kv["broken"].get<crude_json::boolean>();
      // Round the time key to TimePrecision (= CurveState.Read Math.Round) then raw-insert: this
      // preserves serialized Tangent-mode angles; the single updateTangents pass below recomputes
      // spline-mode angles (= TiXL: Tangent keys use stored angles, spline modes derive live).
      // Quantize through Curve::roundTime — the SAME banker's-round the live path (roundT) uses, so a
      // tie-time key (0.00005) loads into the same slot it would live-edit into (BROKEN-2 fix; was
      // std::round = round-half-away, a slot divergence vs nearbyint).
      double t = Curve::roundTime(kv["t"].get<crude_json::number>());
      d.u = t;
      c.table()[t] = d;
    }
    c.updateTangents();
  }
  return c;
}

crude_json::value curveArrayToJson(const std::vector<Curve>& arr) {
  crude_json::array out;
  for (const Curve& c : arr) out.push_back(curveToJson(c));
  return crude_json::value(out);
}

std::vector<Curve> curveArrayFromJson(crude_json::value& v, int* droppedKeys) {
  std::vector<Curve> out;
  if (!v.is_array()) return out;  // not an array -> empty (clean: clipboard tolerance)
  for (auto& cv : v.get<crude_json::array>()) {
    // A non-object element would hit crude_json operator[]'s std::terminate (a hostile clipboard
    // can craft scalar/string array elements) — skip it, same gate as copy_paste's child/wire loops.
    if (!cv.is_object()) continue;
    out.push_back(curveFromJson(cv, droppedKeys));
  }
  return out;
}

}  // namespace sw
