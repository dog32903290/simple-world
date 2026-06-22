// runtime/quat_host — PURE-MATH host quaternion helpers for point-op GOLDENS (no Metal, no platform).
//
// ZONE: runtime (pure computation). Header-only. The CPU twin of shaders/shared/quat.metal.h — used by
// rotation goldens that must compute an EXPECTED quaternion host-side to byte-compare against a GPU
// kernel's output (e.g. TransformPointsFromClipspace's camera-orientation Rotation leg).
//
// PARITY AUTHORITY: external/tixl/.../Assets/shaders/shared/quat-functions.hlsl qFromMatrix3Precise
// (Shepperd's method), byte-faithful to quat.metal.h's MSL port.
#pragma once

#include <cmath>

namespace sw {

// qFromMatrix3Precise (Shepperd's method) — transcription of TiXL quat-functions.hlsl / quat.metal.h.
// Input `a` is a 3×3 given as a[R][C] (row R, col C). Output q = (x,y,z,w), normalized.
inline void qFromMatrix3PreciseHost(const float a[3][3], float q[4]) {
  auto M = [&](int r, int c) { return a[r][c]; };
  float tr = M(0, 0) + M(1, 1) + M(2, 2);
  if (tr > 0.0f) {
    float S = std::sqrt(tr + 1.0f) * 2.0f;
    q[0] = (M(2, 1) - M(1, 2)) / S; q[1] = (M(0, 2) - M(2, 0)) / S;
    q[2] = (M(1, 0) - M(0, 1)) / S; q[3] = 0.25f * S;
  } else if ((M(0, 0) > M(1, 1)) && (M(0, 0) > M(2, 2))) {
    float S = std::sqrt(1.0f + M(0, 0) - M(1, 1) - M(2, 2)) * 2.0f;
    q[0] = 0.25f * S; q[1] = (M(0, 1) + M(1, 0)) / S;
    q[2] = (M(0, 2) + M(2, 0)) / S; q[3] = (M(2, 1) - M(1, 2)) / S;
  } else if (M(1, 1) > M(2, 2)) {
    float S = std::sqrt(1.0f + M(1, 1) - M(0, 0) - M(2, 2)) * 2.0f;
    q[0] = (M(0, 1) + M(1, 0)) / S; q[1] = 0.25f * S;
    q[2] = (M(1, 2) + M(2, 1)) / S; q[3] = (M(0, 2) - M(2, 0)) / S;
  } else {
    float S = std::sqrt(1.0f + M(2, 2) - M(0, 0) - M(1, 1)) * 2.0f;
    q[0] = (M(0, 2) + M(2, 0)) / S; q[1] = (M(1, 2) + M(2, 1)) / S;
    q[2] = 0.25f * S; q[3] = (M(1, 0) - M(0, 1)) / S;
  }
  float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (n > 0.0f) { q[0] /= n; q[1] /= n; q[2] /= n; q[3] /= n; }
}

}  // namespace sw
