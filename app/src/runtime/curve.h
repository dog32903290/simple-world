// runtime/curve — the animation CURVE primitive (S3). A faithful C++ port of TiXL's
// Core/DataTypes/Curve.cs + Core/Animation/VDefinition.cs + the four interpolators +
// the four outside-curve mappers. Pure CPU, runtime leaf (no deps beyond <map>/<cmath>).
//
// TiXL fidelity (抄源碼, 拍板原話): time unit = bars (P3). The sampler GetSampledValue(u)
// reproduces Curve.cs:308-361 exactly — round u to TimePrecision(4), apply Pre/PostCurveMapping
// outside [first,last], then dispatch the interior segment to Const / Linear / Bezier / Spline
// by the SAME predicate ladder (Curve.cs:341-357). The D12 four holes the ledger missed are all
// here: ① six interpolation modes incl. Horizontal (VDefinition.cs:13-21 + SplineInterpolator
// tangent calc) ② Pre/PostCurveMapping outside-curve extrapolation (CurveUtils.OutsideCurveBehavior)
// ③ double-sided tension TensionIn/Out + the Weighted Bezier gate (BezierInterpolator.SegmentNeedsBezier)
// ④ TimePrecision=4 rounding on every time-in.
//
// Definition-layer authority (contract C3/P2, S4 scoreGraph 作廢): the Animator (curve_animator.h)
// owns per-(childId,inputId) Curve arrays on the Symbol DEFINITION; this header is just the math
// primitive those curves are made of.
#pragma once
#include <cstdint>
#include <map>
#include <vector>

namespace sw {

// = TiXL VDefinition.KeyInterpolation (VDefinition.cs:13-21). Order/values pinned to TiXL so the
// savev2 enum-int matches (Constant=0 .. Tangent=5). Horizontal is the D12 hole #1 the ledger dropped.
enum class KeyInterpolation : int {
  Constant = 0,
  Linear = 1,
  Smooth = 2,
  Cubic = 3,
  Horizontal = 4,
  Tangent = 5,
};

// = TiXL CurveUtils.OutsideCurveBehavior (CurveUtils.cs:5-11). D12 hole #2: extrapolation outside
// [firstKey,lastKey]. Values pinned to TiXL (Constant=0 .. Oscillate=3) for savev2 enum-int parity.
enum class OutsideBehavior : int {
  Constant = 0,
  Cycle = 1,
  CycleWithOffset = 2,
  Oscillate = 3,
};

// = TiXL VDefinition (Core/Animation/VDefinition.cs). One keyframe. `u` (the time key) is stored
// rounded to TimePrecision by the owning Curve; the fields mirror VDefinition's serialized state
// 1:1 (value, in/out interpolation, in/out tangent angle, tension, weighted, brokenTangents).
struct VDefinition {
  double value = 0.0;
  double u = 0.0;  // time key (bars), rounded to TimePrecision when inserted into a Curve
  KeyInterpolation inInterpolation = KeyInterpolation::Linear;
  KeyInterpolation outInterpolation = KeyInterpolation::Linear;
  double inTangentAngle = 0.0;   // radians; computed by Curve::updateTangents for spline modes
  double outTangentAngle = 0.0;
  float tensionIn = 1.0f;   // D12 hole #3: influence multiplier for the incoming tangent (1.0 = full)
  float tensionOut = 1.0f;
  bool weighted = false;        // D12 hole #3: Weighted gate -> Bezier root-find path
  bool brokenTangents = false;
};

// = TiXL Curve (Core/DataTypes/Curve.cs). Owns a sorted time->keyframe table + the two outside
// mappings. The SortedList<double,VDefinition> becomes std::map<double,VDefinition> (ordered keys).
class Curve {
 public:
  static constexpr int kTimePrecision = 4;  // = TiXL Curve.TimePrecision (D12 hole #4)

  OutsideBehavior preCurveMapping = OutsideBehavior::Constant;
  OutsideBehavior postCurveMapping = OutsideBehavior::Constant;

  // Insert or replace the key at time u (rounded). Recomputes spline tangents after the edit
  // (= Curve.AddOrUpdateV -> SplineInterpolator.UpdateTangents, Curve.cs:213-229). This is also
  // the LIVE-append entry point (Animator records playhead values through it).
  void addOrUpdate(double u, VDefinition key);
  void removeAt(double u);
  bool hasKeyAt(double u) const;
  size_t count() const { return table_.size(); }
  bool empty() const { return table_.empty(); }

  // Direct table access (Animator iterates it for save; the sampler reads it). Mutating values in
  // place requires a manual updateTangents() (= TiXL NotifyChanged + UpdateTangents).
  std::map<double, VDefinition>& table() { return table_; }
  const std::map<double, VDefinition>& table() const { return table_; }

  // Recompute spline tangent angles across the whole table (= SplineInterpolator.UpdateTangents,
  // SplineInterpolator.cs:8-51). Called automatically by addOrUpdate/removeAt; exposed for the
  // loader (sets keys then computes once).
  void updateTangents();

  // = TiXL Curve.GetSampledValue(u) (Curve.cs:308-361). THE sampler. Rounds u to TimePrecision,
  // applies Pre/PostCurveMapping outside the key range, dispatches the interior to
  // Const/Linear/Bezier/Spline by TiXL's exact predicate ladder. Empty/NaN/Inf -> 0.0.
  double sample(double u) const;

 private:
  std::map<double, VDefinition> table_;
};

// Headless RED->GREEN proof of the sampling math (golden hand-computed against TiXL semantics):
// six interpolation modes incl. Horizontal, Pre/PostCurveMapping each mode, TensionIn/Out + the
// Weighted Bezier gate, TimePrecision=4 rounding, live-append, empty/single-key edges. injectBug
// corrupts one sampled expectation -> the assertion FAILS (teeth).
int runCurveSelfTest(bool injectBug);

}  // namespace sw
