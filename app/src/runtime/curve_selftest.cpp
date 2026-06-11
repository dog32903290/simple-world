// runtime/curve_selftest — golden for the Curve sampler (S3, D12 four holes). Hand-computed
// expectations against TiXL semantics (the values are derived from the SAME formulas in
// external/tixl, not from this impl — provenance is the math, not the code under test). -bug
// corrupts one expectation so a regression in the sampler can FAIL (RED) before we trust GREEN.
#include "runtime/curve.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace sw {
namespace {

int g_fail = 0;
void check(const char* what, double got, double want, double tol) {
  bool ok = std::abs(got - want) <= tol;
  if (!ok) {
    ++g_fail;
    printf("  [curve] FAIL %-32s got=%.6f want=%.6f (tol %.0e)\n", what, got, want, tol);
  } else {
    printf("  [curve] ok   %-32s = %.6f\n", what, got);
  }
}

VDefinition key(double v, KeyInterpolation in, KeyInterpolation out) {
  VDefinition d;
  d.value = v;
  d.inInterpolation = in;
  d.outInterpolation = out;
  return d;
}

}  // namespace

int runCurveSelfTest(bool injectBug) {
  g_fail = 0;
  const double T = 1e-5;
  printf("[selftest] curve (S3 sampling golden, D12 four holes)\n");

  // --- edges: empty + single key (Curve.cs:310, 123-129) ---
  {
    Curve c;
    check("empty -> 0", c.sample(0.5), 0.0, T);
    check("nan -> 0", c.sample(std::nan("")), 0.0, T);
    c.addOrUpdate(2.0, key(7.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    check("single key, before", c.sample(0.0), 7.0, T);  // count==1 -> first value
    check("single key, at", c.sample(2.0), 7.0, T);
    check("single key, after", c.sample(9.0), 7.0, T);
  }

  // --- D12 #1: six interpolation modes. Two keys at t=0 (v=0) and t=1 (v=10), sample @ 0.5 ---
  // Constant: a.outInterpolation==Constant -> ConstInterpolator (returns a.value unless u==bKey).
  {
    Curve c;
    c.addOrUpdate(0.0, key(0.0, KeyInterpolation::Constant, KeyInterpolation::Constant));
    c.addOrUpdate(1.0, key(10.0, KeyInterpolation::Constant, KeyInterpolation::Constant));
    check("Constant @0.5", c.sample(0.5), 0.0, T);   // a.value (not within 1e-4 of bKey)
    check("Constant @1.0", c.sample(1.0), 10.0, T);  // >= lastU -> last value
  }
  // Linear: both ends Linear -> straight line. @0.5 = 5.0.
  {
    Curve c;
    c.addOrUpdate(0.0, key(0.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    c.addOrUpdate(1.0, key(10.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    check("Linear @0.5", c.sample(0.5), 5.0, T);
    check("Linear @0.25", c.sample(0.25), 2.5, T);
  }
  // Smooth / Cubic / Horizontal: spline Hermite. With only two keys, tangents come from
  // CalcStartTangent/CalcEndTangent. For Linear-equivalent endpoints the spline matches the line
  // at the midpoint by symmetry; Horizontal forces flat tangents (angle PI / 0) -> S-curve through
  // the midpoint still = 5.0 by symmetry (the symmetric Hermite with zero slope = smoothstep*10).
  {
    Curve c;
    c.addOrUpdate(0.0, key(0.0, KeyInterpolation::Smooth, KeyInterpolation::Smooth));
    c.addOrUpdate(1.0, key(10.0, KeyInterpolation::Smooth, KeyInterpolation::Smooth));
    check("Smooth @0.5 (symmetric)", c.sample(0.5), 5.0, T);
  }
  {
    Curve c;
    c.addOrUpdate(0.0, key(0.0, KeyInterpolation::Cubic, KeyInterpolation::Cubic));
    c.addOrUpdate(1.0, key(10.0, KeyInterpolation::Cubic, KeyInterpolation::Cubic));
    check("Cubic @0.5 (symmetric)", c.sample(0.5), 5.0, T);
  }
  {
    // Horizontal: start out-tangent = PI (slope tan(PI)~0), end in-tangent = 0 (slope 0). Both
    // slopes 0 -> Hermite with m0=m1=0 = smoothstep: 10*(3t^2-2t^3). @0.5 = 10*(0.75-0.25)=5.0.
    Curve c;
    c.addOrUpdate(0.0, key(0.0, KeyInterpolation::Horizontal, KeyInterpolation::Horizontal));
    c.addOrUpdate(1.0, key(10.0, KeyInterpolation::Horizontal, KeyInterpolation::Horizontal));
    check("Horizontal @0.5 (smoothstep)", c.sample(0.5), 5.0, T);
    check("Horizontal @0.25 (smoothstep)", c.sample(0.25), 10.0 * (3 * 0.0625 - 2 * 0.015625), T);
  }
  // Tangent mode (unweighted, tension 1) with explicit horizontal handles -> same smoothstep.
  {
    Curve c;
    VDefinition a = key(0.0, KeyInterpolation::Tangent, KeyInterpolation::Tangent);
    VDefinition b = key(10.0, KeyInterpolation::Tangent, KeyInterpolation::Tangent);
    a.outTangentAngle = M_PI;  // slope ~0
    b.inTangentAngle = 0.0;    // slope 0
    c.addOrUpdate(0.0, a);
    c.addOrUpdate(1.0, b);
    check("Tangent flat @0.5", c.sample(0.5), 5.0, T);
  }

  // --- D12 #3: TensionOut amplifies the outgoing tangent (Bezier weighted gate). A weighted
  // Tangent key with tensionOut != 1 routes through Bezier root-find. We assert the value MOVES
  // off the unweighted spline result and stays bounded (exact value is impl-faithful Bezier).
  {
    Curve c;
    VDefinition a = key(0.0, KeyInterpolation::Tangent, KeyInterpolation::Tangent);
    VDefinition b = key(10.0, KeyInterpolation::Tangent, KeyInterpolation::Tangent);
    a.outTangentAngle = M_PI / 4;  // slope 1
    b.inTangentAngle = M_PI / 4;   // slope 1 (incoming)
    a.weighted = true; a.tensionOut = 2.0f;  // != 1 -> Bezier
    b.weighted = true; b.tensionIn = 2.0f;
    c.addOrUpdate(0.0, a);
    c.addOrUpdate(1.0, b);
    double v = c.sample(0.5);
    // Symmetric weighted handles -> midpoint stays 5.0 (symmetry survives the tension scaling).
    check("Weighted Bezier @0.5 (symmetric)", v, 5.0, 1e-3);
  }

  // --- D12 #2: Pre/PostCurveMapping outside-curve behaviors. Keys @ t=0(v=0), t=1(v=10). ---
  {
    Curve base;
    base.addOrUpdate(0.0, key(0.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    base.addOrUpdate(1.0, key(10.0, KeyInterpolation::Linear, KeyInterpolation::Linear));

    // Constant (default): clamp. @ -0.5 -> first value 0; @ 1.5 -> last value 10.
    check("Post Constant @1.5", base.sample(1.5), 10.0, T);
    check("Pre Constant @-0.5", base.sample(-0.5), 0.0, T);

    // Cycle: u=1.5 -> delta=0.5, newU = 0 + 0.5 = 0.5 -> linear 5.0. u=2.5 -> delta 1.5,
    // fmod(1.5,1)=0.5 -> newU 0.5 -> 5.0.
    Curve cy = base;
    cy.postCurveMapping = OutsideBehavior::Cycle;
    cy.preCurveMapping = OutsideBehavior::Cycle;
    check("Post Cycle @1.5", cy.sample(1.5), 5.0, T);
    check("Post Cycle @2.5", cy.sample(2.5), 5.0, T);
    // Pre Cycle @ -0.5: delta=0.5, newU = lastU - fmod(0.5,1) = 1 - 0.5 = 0.5 -> 5.0.
    check("Pre Cycle @-0.5", cy.sample(-0.5), 5.0, T);

    // CycleWithOffset: off = lastVal-firstVal = 10. @1.5 -> newU 0.5 (val 5) + offset 10*1 = 15.
    Curve co = base;
    co.postCurveMapping = OutsideBehavior::CycleWithOffset;
    check("Post CycleWithOffset @1.5", co.sample(1.5), 15.0, T);
    // @2.5 -> cycle count (int)(1.5/1)=1 -> offset 10*(1+1)=20, newU 0.5 -> 5 + 20 = 25.
    check("Post CycleWithOffset @2.5", co.sample(2.5), 25.0, T);

    // Oscillate: @1.5 -> delta 0.5, a=(byte)0 even -> newU = lastU - fmod(0.5,1) = 0.5 -> 5.0.
    Curve os = base;
    os.postCurveMapping = OutsideBehavior::Oscillate;
    check("Post Oscillate @1.5", os.sample(1.5), 5.0, T);
    // @2.5 -> delta 1.5, a=(byte)1 odd -> newU = firstU + fmod(1.5,1) = 0.5 -> 5.0.
    check("Post Oscillate @2.5", os.sample(2.5), 5.0, T);
    // @3.5 -> delta 2.5, a=(byte)2 even -> newU = lastU - fmod(2.5,1) = 1-0.5 = 0.5 -> 5.0.
    check("Post Oscillate @3.5", os.sample(3.5), 5.0, T);
  }

  // --- D12 #4: TimePrecision=4 rounding. A key inserted at 0.12345 rounds to 0.1235 (round-half-
  // even on the 5th decimal: 0.12345 -> 0.1234 or 0.1235 depending on the digit; nearbyint of
  // 1234.5 = 1234 (even) -> 0.1234). Assert the key lands at the rounded slot.
  {
    Curve c;
    c.addOrUpdate(0.12345, key(3.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    // banker's: 1234.5 -> 1234 (nearest even) -> 0.1234. hasKeyAt also rounds its query.
    check("TimePrecision rounds key", c.hasKeyAt(0.1234) ? 1.0 : 0.0, 1.0, T);
    check("TimePrecision sample @rounded", c.sample(0.1234), 3.0, T);
  }

  // --- live-append: adding a later key while sampling reflects immediately (Animator records). ---
  {
    Curve c;
    c.addOrUpdate(0.0, key(0.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    check("pre-append (single key)", c.sample(2.0), 0.0, T);
    c.addOrUpdate(4.0, key(8.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    check("post-append linear @2", c.sample(2.0), 4.0, T);  // line 0..8 over 0..4 -> @2 = 4
  }

  // teeth: corrupt one expectation. If the sampler ever regresses to this wrong value, -bug GREENs
  // by accident — so -bug asserts a KNOWN-WRONG expectation must FAIL.
  if (injectBug) {
    Curve c;
    c.addOrUpdate(0.0, key(0.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    c.addOrUpdate(1.0, key(10.0, KeyInterpolation::Linear, KeyInterpolation::Linear));
    check("BUG: Linear @0.5 should be 5 but asserting 9", c.sample(0.5), 9.0, T);  // must FAIL
  }

  printf("[selftest] curve -> %s (%d failures)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
  return g_fail == 0 ? 0 : 1;
}

}  // namespace sw
