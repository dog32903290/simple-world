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

Curve curveFromJson(crude_json::value& v) {
  Curve c;
  if (v["preCurve"].is_number())
    c.preCurveMapping = outsideFromInt((int)v["preCurve"].get<crude_json::number>());
  if (v["postCurve"].is_number())
    c.postCurveMapping = outsideFromInt((int)v["postCurve"].get<crude_json::number>());
  if (v["keys"].is_array()) {
    for (auto& kv : v["keys"].get<crude_json::array>()) {
      if (!kv["t"].is_number() || !kv["v"].is_number()) continue;  // malformed key dropped
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
      double t = std::round(kv["t"].get<crude_json::number>() * 10000.0) / 10000.0;
      d.u = t;
      c.table()[t] = d;
    }
    c.updateTangents();
  }
  return c;
}

}  // namespace sw
