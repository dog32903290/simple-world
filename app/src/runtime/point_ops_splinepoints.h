// runtime/point_ops_splinepoints — shared spline math for the SplinePoints leaf + its golden.
//
// SplinePoints is a PURE-CPU op (no kernel): the cook and the golden both need the same cardinal
// cubic-bezier + even-arc-length sampling math. Rather than duplicate it across the leaf .cpp and the
// _golden.cpp (or balloon one file past the ≤400 ratchet), the tiny pure-math helpers live here as
// `inline` (header-only, no TU), and the two .cpp files include them.
//
// Faithful to (BACKWARD-TRACED, Cut-58):
//   external/tixl/Core/Utils/Splines/BezierPointSpline.cs  (SampleCubicBezier / SampleLinearColors)
//   external/tixl/Core/Utils/Splines/Bezier.cs:22-31       (GetPoint 4-arg cubic)
//   external/tixl/Core/Utils/MathUtils.cs:617-674          (LookAt quaternion)
//
// runtime leaf (ARCHITECTURE.md): pure math, no upward deps, self-contained.
#pragma once
#include <cmath>
#include <vector>

namespace sw {
namespace splinepoints_detail {

struct V3 { float x, y, z; };
struct V4 { float x, y, z, w; };

inline V3 sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 add(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 scl(const V3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dist(const V3& a, const V3& b) {
  float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}
inline V3 normalize(const V3& a) {
  float L = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
  if (L <= 0.0f) return {0.0f, 0.0f, 0.0f};
  return {a.x / L, a.y / L, a.z / L};
}
inline V3 cross(const V3& a, const V3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Bezier.GetPoint (4-arg cubic), external/tixl/Core/Utils/Splines/Bezier.cs:22-31, verbatim.
inline V3 bezier4(const V3& p0, const V3& p1, const V3& p2, const V3& p3, float t) {
  t = clampf(t, 0.0f, 1.0f);
  float u = 1.0f - t;
  float a = u * u * u;
  float b = 3.0f * u * u * t;
  float c = 3.0f * u * t * t;
  float d = t * t * t;
  return {a * p0.x + b * p1.x + c * p2.x + d * p3.x,
          a * p0.y + b * p1.y + c * p2.y + d * p3.y,
          a * p0.z + b * p1.z + c * p2.z + d * p3.z};
}

// MathUtils.LookAt(forward, up) -> quaternion, external/tixl .../MathUtils.cs:617-674, verbatim.
inline V4 lookAt(V3 forward, V3 up) {
  V3 right = normalize(cross(forward, up));
  up = normalize(cross(forward, right));

  float m00 = right.x, m01 = right.y, m02 = right.z;
  float m10 = up.x, m11 = up.y, m12 = up.z;
  float m20 = forward.x, m21 = forward.y, m22 = forward.z;

  float trace = (m00 + m11) + m22;
  V4 q = {0.0f, 0.0f, 0.0f, 1.0f};
  if (trace > 0.0f) {
    float num = std::sqrt(trace + 1.0f);
    q.w = num * 0.5f;
    num = 0.5f / num;
    q.x = (m12 - m21) * num;
    q.y = (m20 - m02) * num;
    q.z = (m01 - m10) * num;
    return q;
  }
  if ((m00 >= m11) && (m00 >= m22)) {
    float num7 = std::sqrt(((1.0f + m00) - m11) - m22);
    float num4 = 0.5f / num7;
    q.x = 0.5f * num7;
    q.y = (m01 + m10) * num4;
    q.z = (m02 + m20) * num4;
    q.w = (m12 - m21) * num4;
    return q;
  }
  if (m11 > m22) {
    float num6 = std::sqrt(((1.0f + m11) - m00) - m22);
    float num3 = 0.5f / num6;
    q.x = (m10 + m01) * num3;
    q.y = 0.5f * num6;
    q.z = (m21 + m12) * num3;
    q.w = (m20 - m02) * num3;
    return q;
  }
  float num5 = std::sqrt(((1.0f + m22) - m00) - m11);
  float num2 = 0.5f / num5;
  q.x = (m20 + m02) * num2;
  q.y = (m21 + m12) * num2;
  q.z = 0.5f * num5;
  q.w = (m01 - m10) * num2;
  return q;
}

// BezierPointSpline.SampleCubicBezier(t, curvature, points), .../BezierPointSpline.cs:115-152.
// `pos`/`f1` are parallel arrays of the joined control polygon. N = number of control points.
inline V3 sampleCubicBezier(float t, float curvature, const std::vector<V3>& pos,
                            const std::vector<float>& f1) {
  const int N = (int)pos.size();
  int i;
  if (t >= 1.0f) {
    t = 1.0f;
    i = N - 2;
  } else {
    float tt = t * (float)(N - 2);
    i = (int)tt;
    t = tt - (float)i;
  }

  V3 pA = pos[i];
  V3 pB = pos[i + 1];
  V3 pNext = pB;
  V3 pLast = pA;
  if (i > 0) pLast = pos[i - 1];
  if (i < N - 2) pNext = pos[i + 2];

  // h0 = pA - (pLast - pB)/curvature * F1[i]
  V3 h0 = sub(pA, scl(sub(pLast, pB), f1[i] / curvature));
  // h1 = pB + (pA - pNext)/curvature * F1[i+1]
  V3 h1 = add(pB, scl(sub(pA, pNext), f1[i + 1] / curvature));
  return bezier4(pA, h0, h1, pB, t);
}

// BezierPointSpline.SampleLinearColors(t, points), .../BezierPointSpline.cs:88-112.
inline V4 sampleLinearColors(float t, const std::vector<V4>& col) {
  const int N = (int)col.size();
  int i;
  if (t >= 1.0f) {
    t = 1.0f;
    i = N - 2;  // .cs comment "Adjusted index"
  } else {
    float tt = t * (float)(N - 1);
    i = (int)tt;
    t = tt - (float)i;
  }
  V4 a = col[i];
  V4 b = col[i + 1];
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
          a.w + (b.w - a.w) * t};
}

}  // namespace splinepoints_detail
}  // namespace sw
