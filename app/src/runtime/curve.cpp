// runtime/curve — implementation. Ported line-by-line from TiXL Core/Animation/{Const,Linear,
// Bezier,Spline}Interpolator.cs + {Constant,Cycle,CycleWithOffset,Oscillate}CurveMapper.cs +
// Curve.cs:GetSampledValue. Comments cite the TiXL source line for each transcribed block so a
// future reader can diff against external/tixl directly.
#include "runtime/curve.h"

#include <cmath>

namespace sw {
namespace {

constexpr double kPi = 3.14159265358979323846;

// = TiXL Curve.cs Math.Round(u, TimePrecision) — banker's rounding to 4 decimals. std::round is
// round-half-away; TiXL's Math.Round is round-half-to-even (banker's). Match it: the difference
// only bites exactly on .xxxx5 ties, but D12 hole #4 demands the SAME quantization as TiXL.
double roundT(double u) {
  const double scale = 10000.0;  // 10^TimePrecision
  double scaled = u * scale;
  double r = std::nearbyint(scaled);  // honors the current rounding mode = round-half-even default
  return r / scale;
}

// = SplineInterpolator.SlopFromAngle (SplineInterpolator.cs:83-87 / Bezier:141-146): tan(angle)
// with a near-zero snap so flat curves don't wobble from tan(PI)≈-1.22e-16.
double slopeFromAngle(double angle) {
  double slope = std::tan(angle);
  return std::abs(slope) < 1e-10 ? 0.0 : slope;
}

// ---- interpolators (each segment between key a and key b, evaluated at time u) ----

// = ConstInterpolator.Interpolate (ConstInterpolator.cs:10-13).
double constInterp(const VDefinition& a, double aKey, const VDefinition& b, double bKey, double u) {
  return std::abs(u - bKey) < 0.0001 ? b.value : a.value;
}

// = LinearInterpolator.Interpolate (LinearInterpolator.cs:9-12).
double linearInterp(const VDefinition& a, double aKey, const VDefinition& b, double bKey, double u) {
  return a.value + (b.value - a.value) * ((u - aKey) / (bKey - aKey));
}

// = SplineInterpolator.Interpolate (SplineInterpolator.cs:63-76): cubic Hermite with tension.
double splineInterp(const VDefinition& a, double aKey, const VDefinition& b, double bKey, double u) {
  double t = (u - aKey) / (bKey - aKey);
  double tangentLength = bKey - aKey;
  double p0 = a.value;
  double m0 = slopeFromAngle(a.outTangentAngle) * tangentLength * a.tensionOut;
  double p1 = b.value;
  double m1 = slopeFromAngle(b.inTangentAngle) * tangentLength * b.tensionIn;
  double t2 = t * t;
  double t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 +
         (t3 - t2) * m1;
}

// ---- Bezier (D12 hole #3, the Weighted gate path) — BezierInterpolator.cs ----

// = BezierInterpolator.SegmentNeedsBezier (BezierInterpolator.cs:56-65).
bool segmentNeedsBezier(const VDefinition& a, const VDefinition& b) {
  return (a.weighted && a.outInterpolation == KeyInterpolation::Tangent && a.tensionOut != 1.0f) ||
         (b.weighted && b.inInterpolation == KeyInterpolation::Tangent && b.tensionIn != 1.0f);
}

double evalCubic(double t, double p0, double p1, double p2, double p3) {
  double u = 1.0 - t;
  return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}
double evalCubicDeriv(double t, double p0, double p1, double p2, double p3) {
  double u = 1.0 - t;
  return 3.0 * u * u * (p1 - p0) + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);
}

// = BezierInterpolator.FindParameterForTime + BisectionFallback (BezierInterpolator.cs:71-125).
double findBezierParam(double targetX, double p0x, double p1x, double p2x, double p3x) {
  constexpr int kMaxNewton = 8;
  constexpr int kMaxBisect = 30;
  constexpr double kTol = 1e-8;
  double t = (targetX - p0x) / (p3x - p0x);
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  for (int i = 0; i < kMaxNewton; ++i) {
    double x = evalCubic(t, p0x, p1x, p2x, p3x);
    double residual = x - targetX;
    if (std::abs(residual) < kTol) return t;
    double dx = evalCubicDeriv(t, p0x, p1x, p2x, p3x);
    if (std::abs(dx) < 1e-12) break;
    double newT = t - residual / dx;
    if (newT < 0.0) newT = 0.0;
    if (newT > 1.0) newT = 1.0;
    if (std::abs(newT - t) < kTol) return newT;
    t = newT;
  }
  double lo = 0, hi = 1;
  for (int i = 0; i < kMaxBisect; ++i) {
    double mid = (lo + hi) * 0.5;
    double x = evalCubic(mid, p0x, p1x, p2x, p3x);
    if (std::abs(x - targetX) < kTol) return mid;
    if (x < targetX) lo = mid; else hi = mid;
  }
  return (lo + hi) * 0.5;
}

// = BezierInterpolator.Interpolate (BezierInterpolator.cs:19-48).
double bezierInterp(const VDefinition& a, double aKey, const VDefinition& b, double bKey, double u) {
  double segmentWidth = bKey - aKey;
  if (segmentWidth <= 0) return a.value;
  double slopeA = slopeFromAngle(a.outTangentAngle);
  double slopeB = slopeFromAngle(b.inTangentAngle);
  double m0 = slopeA * segmentWidth * a.tensionOut;
  double m1 = slopeB * segmentWidth * b.tensionIn;
  double p0x = aKey, p0y = a.value;
  double p1x = aKey + segmentWidth * a.tensionOut / 3.0;
  double p1y = a.value + m0 / 3.0;
  double p2x = bKey - segmentWidth * b.tensionIn / 3.0;
  double p2y = b.value - m1 / 3.0;
  double p3x = bKey, p3y = b.value;
  double t = findBezierParam(u, p0x, p1x, p2x, p3x);
  return evalCubic(t, p0y, p1y, p2y, p3y);
}

// ---- outside-curve mappers (D12 hole #2) — *CurveMapper.cs. out: newU + offset ----

// = ConstantCurveMapper.Calc (ConstantCurveMapper.cs:7-11).
void mapConstant(double u, double, double, double, double, double& newU, double& offset) {
  newU = u;
  offset = 0.0;
}

// = CycleCurveMapper.Calc (CycleCurveMapper.cs:8-36).
void mapCycle(double u, double firstU, double lastU, double, double, double& newU, double& offset) {
  offset = 0.0;
  double span = lastU - firstU;
  if (u < firstU) {
    double delta = firstU - u;
    newU = lastU - std::fmod(delta, span);
  } else if (u > lastU) {
    double delta = u - lastU;
    newU = firstU + std::fmod(delta, span);
  } else {
    newU = u;
  }
}

// = CycleWithOffsetCurveMapper.Calc (CycleWithOffsetCurveMapper.cs:8-42). off = lastVal - firstVal.
void mapCycleOffset(double u, double firstU, double lastU, double firstVal, double lastVal,
                    double& newU, double& offset) {
  offset = 0.0;
  double span = lastU - firstU;
  double off = lastVal - firstVal;
  if (u < firstU) {
    double delta = firstU - u;
    newU = lastU - std::fmod(delta, span);
    offset = off * (-((int)(delta / span) + 1));
  } else if (u > lastU) {
    double delta = u - lastU;
    newU = firstU + std::fmod(delta, span);
    offset = off * ((int)(delta / span) + 1);
  } else {
    newU = u;
  }
}

// = OscillateCurveMapper.Calc (OscillateCurveMapper.cs:8-52). NB: the > lastU branch casts the
// cycle count to byte in TiXL (truncates mod 256) — replicated for exact parity at extreme u.
void mapOscillate(double u, double firstU, double lastU, double, double, double& newU,
                  double& offset) {
  offset = 0.0;
  double span = lastU - firstU;
  if (u < firstU) {
    double delta = firstU - u;
    int a = (int)(delta / span);
    if ((a & 1) != 0)
      newU = lastU - std::fmod(delta, span);
    else
      newU = firstU + std::fmod(delta, span);
  } else if (u > lastU) {
    double delta = u - lastU;
    unsigned char a = (unsigned char)(delta / span);  // TiXL casts to byte here (cs:37)
    if ((a & 1) != 0)
      newU = firstU + std::fmod(delta, span);
    else
      newU = lastU - std::fmod(delta, span);
  } else {
    newU = u;
  }
}

// Apply the chosen outside behavior. (Two-key minimum for cyclic mappers, matching the
// curveElements.Count < 2 guard in every *CurveMapper.Calc.)
void applyOutside(OutsideBehavior beh, double u, double firstU, double lastU, double firstVal,
                  double lastVal, size_t keyCount, double& newU, double& offset) {
  newU = u;
  offset = 0.0;
  if (beh == OutsideBehavior::Constant || keyCount < 2) return;
  switch (beh) {
    case OutsideBehavior::Cycle: mapCycle(u, firstU, lastU, firstVal, lastVal, newU, offset); break;
    case OutsideBehavior::CycleWithOffset:
      mapCycleOffset(u, firstU, lastU, firstVal, lastVal, newU, offset);
      break;
    case OutsideBehavior::Oscillate:
      mapOscillate(u, firstU, lastU, firstVal, lastVal, newU, offset);
      break;
    default: mapConstant(u, firstU, lastU, firstVal, lastVal, newU, offset); break;
  }
}

// ---- spline tangent computation (SplineInterpolator.UpdateTangents) ----
constexpr double kTangentClampRatio = 1.5;  // = SplineInterpolator.TANGENT_CLAMP_RATIO (cs:125)

bool needsTangentComputation(const VDefinition& d) {
  return d.inInterpolation != KeyInterpolation::Constant ||
         d.outInterpolation != KeyInterpolation::Constant;
}

double calcStartTangent(double aKey, const VDefinition& aDef, double bKey, const VDefinition& bDef) {
  switch (aDef.outInterpolation) {
    case KeyInterpolation::Tangent: return aDef.outTangentAngle;
    case KeyInterpolation::Linear:
    case KeyInterpolation::Smooth:
    case KeyInterpolation::Cubic:
      return kPi / 2 - std::atan2(aKey - bKey, aDef.value - bDef.value);
    case KeyInterpolation::Horizontal:
    default: return kPi;
  }
}

double calcEndTangent(double aKey, const VDefinition& aDef, double bKey, const VDefinition& bDef) {
  switch (bDef.inInterpolation) {
    case KeyInterpolation::Tangent: return bDef.inTangentAngle;
    case KeyInterpolation::Linear:
    case KeyInterpolation::Smooth:
    case KeyInterpolation::Cubic:
      return kPi / 2 - std::atan2(bKey - aKey, bDef.value - aDef.value);
    case KeyInterpolation::Horizontal:
    default: return 0;
  }
}

double calcInTangent(double prevKey, const VDefinition& prevDef, double curKey,
                     const VDefinition& curDef, double nextKey, const VDefinition& nextDef) {
  switch (curDef.inInterpolation) {
    case KeyInterpolation::Tangent: return curDef.inTangentAngle;
    case KeyInterpolation::Smooth: {
      double angle = kPi / 2 - std::atan2(nextKey - prevKey, nextDef.value - prevDef.value);
      double thirdToPrev = (prevKey - curKey) / kTangentClampRatio;
      double thirdToNext = (nextKey - curKey) / kTangentClampRatio;
      if (prevDef.value > nextDef.value &&
          (curDef.value + std::tan(angle) * thirdToNext) < nextDef.value)
        angle = kPi + kPi / 2 - std::atan2(-thirdToNext, std::max(0.0, curDef.value - nextDef.value));
      else if (prevDef.value < nextDef.value &&
               (curDef.value + std::tan(angle) * thirdToNext) > nextDef.value)
        angle = kPi + kPi / 2 - std::atan2(-thirdToNext, std::min(0.0, curDef.value - nextDef.value));
      else if (prevDef.value > nextDef.value &&
               (curDef.value + std::tan(angle) * thirdToPrev) > prevDef.value)
        angle = kPi + kPi / 2 - std::atan2(thirdToPrev, std::max(0.0, -curDef.value + prevDef.value));
      else if (prevDef.value < nextDef.value &&
               (curDef.value + std::tan(angle) * thirdToPrev) < prevDef.value)
        angle = kPi + kPi / 2 - std::atan2(thirdToPrev, std::min(0.0, -curDef.value + prevDef.value));
      return angle;
    }
    case KeyInterpolation::Cubic:
      return kPi / 2 - std::atan2(nextKey - prevKey, nextDef.value - prevDef.value);
    case KeyInterpolation::Linear:
      return kPi / 2 - std::atan2(curKey - prevKey, curDef.value - prevDef.value);
    case KeyInterpolation::Horizontal:
    default: return 0;
  }
}

double calcOutTangent(double prevKey, const VDefinition& prevDef, double curKey,
                      const VDefinition& curDef, double nextKey, const VDefinition& nextDef) {
  switch (curDef.outInterpolation) {
    case KeyInterpolation::Tangent: return curDef.outTangentAngle;
    case KeyInterpolation::Smooth: {
      double thirdToNext = (nextKey - curKey) / kTangentClampRatio;
      double thirdToPrev = (prevKey - curKey) / kTangentClampRatio;
      double angle = kPi / 2 - std::atan2(prevKey - nextKey, prevDef.value - nextDef.value);
      if (prevDef.value > nextDef.value &&
          (curDef.value + std::tan(angle) * thirdToNext) < nextDef.value)
        angle = kPi / 2 - std::atan2(-thirdToNext, std::max(0.0, curDef.value - nextDef.value));
      else if (prevDef.value < nextDef.value &&
               (curDef.value + std::tan(angle) * thirdToNext) > nextDef.value)
        angle = kPi / 2 - std::atan2(-thirdToNext, std::min(0.0, curDef.value - nextDef.value));
      else if (prevDef.value > nextDef.value &&
               (curDef.value + std::tan(angle) * thirdToPrev) > prevDef.value)
        angle = kPi / 2 - std::atan2(thirdToPrev, std::max(0.0, -curDef.value + prevDef.value));
      else if (prevDef.value < nextDef.value &&
               (curDef.value + std::tan(angle) * thirdToPrev) < prevDef.value)
        angle = kPi / 2 - std::atan2(thirdToPrev, std::min(0.0, -curDef.value + prevDef.value));
      return angle;
    }
    case KeyInterpolation::Cubic:
      return kPi / 2 - std::atan2(prevKey - nextKey, prevDef.value - nextDef.value);
    case KeyInterpolation::Linear:
      return kPi / 2 - std::atan2(curKey - nextKey, curDef.value - nextDef.value);
    case KeyInterpolation::Horizontal:
    default: return kPi;
  }
}

}  // namespace

void Curve::updateTangents() {
  // = SplineInterpolator.UpdateTangents (SplineInterpolator.cs:8-51). std::map preserves key order
  // (= SortedList), so a flat vector of (key, &VDef) indexes positionally like TiXL's table.
  size_t n = table_.size();
  if (n <= 1) return;
  std::vector<std::pair<double, VDefinition*>> v;
  v.reserve(n);
  for (auto& kv : table_) v.push_back({kv.first, &kv.second});

  // First key: start tangent.
  v[0].second->outTangentAngle = calcStartTangent(v[0].first, *v[0].second, v[1].first, *v[1].second);
  v[0].second->inTangentAngle = v[0].second->outTangentAngle - kPi;

  for (size_t i = 1; i + 1 < n; ++i) {
    if (needsTangentComputation(*v[i].second)) {
      v[i].second->inTangentAngle = calcInTangent(v[i - 1].first, *v[i - 1].second, v[i].first,
                                                  *v[i].second, v[i + 1].first, *v[i + 1].second);
      v[i].second->outTangentAngle = calcOutTangent(v[i - 1].first, *v[i - 1].second, v[i].first,
                                                    *v[i].second, v[i + 1].first, *v[i + 1].second);
    }
  }

  // Last key: end tangent.
  v[n - 1].second->inTangentAngle =
      calcEndTangent(v[n - 2].first, *v[n - 2].second, v[n - 1].first, *v[n - 1].second);
  v[n - 1].second->outTangentAngle = v[n - 1].second->inTangentAngle - kPi;
}

void Curve::addOrUpdate(double u, VDefinition key) {
  u = roundT(u);
  key.u = u;
  table_[u] = key;  // = Curve.AddOrUpdateV (cs:213-229): insert/replace then update tangents
  updateTangents();
}

void Curve::removeAt(double u) {
  u = roundT(u);
  table_.erase(u);
  updateTangents();
}

bool Curve::hasKeyAt(double u) const { return table_.count(roundT(u)) > 0; }

double Curve::sample(double u) const {
  // = Curve.GetSampledValue (Curve.cs:308-361).
  if (table_.empty() || std::isnan(u) || std::isinf(u)) return 0.0;
  double uRounded = roundT(u);
  double firstU = table_.begin()->first;
  auto lastIt = std::prev(table_.end());
  double lastU = lastIt->first;
  double firstVal = table_.begin()->second.value;
  double lastVal = lastIt->second.value;

  double mappedU = uRounded;
  double offset = 0.0;
  if (uRounded <= firstU)
    applyOutside(preCurveMapping, uRounded, firstU, lastU, firstVal, lastVal, table_.size(),
                 mappedU, offset);
  else if (uRounded >= lastU)
    applyOutside(postCurveMapping, uRounded, firstU, lastU, firstVal, lastVal, table_.size(),
                 mappedU, offset);

  if (mappedU <= firstU) return offset + firstVal;
  if (mappedU >= lastU) return offset + lastVal;

  // Interior: find the bracketing keys a (last key < mappedU... actually <=) and b. TiXL's
  // TryGetKeysForInterpolation returns the key BEFORE u (FindIndexBefore: largest key strictly <
  // u) and the next. std::map::upper_bound(mappedU) = first key > mappedU = b; its predecessor = a.
  auto bIt = table_.upper_bound(mappedU);
  if (bIt == table_.begin() || bIt == table_.end()) return offset + lastVal;  // defensive
  auto aIt = std::prev(bIt);
  const VDefinition& a = aIt->second;
  const VDefinition& b = bIt->second;
  double aKey = aIt->first, bKey = bIt->first;

  double resultValue;
  if (a.outInterpolation == KeyInterpolation::Constant) {
    resultValue = constInterp(a, aKey, b, bKey, mappedU);
  } else if (a.outInterpolation == KeyInterpolation::Linear &&
             b.inInterpolation == KeyInterpolation::Linear) {
    resultValue = linearInterp(a, aKey, b, bKey, mappedU);
  } else if (segmentNeedsBezier(a, b)) {
    resultValue = bezierInterp(a, aKey, b, bKey, mappedU);
  } else {
    resultValue = splineInterp(a, aKey, b, bKey, mappedU);
  }
  return offset + resultValue;
}

}  // namespace sw
