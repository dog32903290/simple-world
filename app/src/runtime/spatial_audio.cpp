// runtime/spatial_audio — see spatial_audio.h for the contract + TiXL authority. The closed-form gain
// spine of SpatialAudioPlayer: distance attenuation, effective volume, Euler→direction vectors.
#include "runtime/spatial_audio.h"

#include <cmath>

namespace sw {

namespace {

// Golden teeth latch: when set, the REAL math is corrupted (drop the operator/stream distance clamp so a
// degenerate min/max no longer floors the attenuation the golden asserts). Production leaves it false.
bool g_spatialBug = false;

constexpr float kPi = 3.14159265358979323846f;

// .NET Quaternion.CreateFromYawPitchRoll → Matrix4x4.CreateFromQuaternion (row-vector), verbatim from the
// point_ops_polartransformpoints port. Yaw=Y, Pitch=X, Roll=Z (radians). Returns a 3x3 rotation applied as
// (v)*R (row-vector), matching System.Numerics Vector3.Transform(v, matrix).
struct Quat { float x, y, z, w; };

Quat cfypr(float yaw, float pitch, float roll) {
  float sr = std::sin(roll * 0.5f),  cr = std::cos(roll * 0.5f);
  float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
  float sy = std::sin(yaw * 0.5f),   cy = std::cos(yaw * 0.5f);
  return {cy * sp * cr + sy * cp * sr,
          sy * cp * cr - cy * sp * sr,
          cy * cp * sr - sy * sp * cr,
          cy * cp * cr + sy * sp * sr};
}

// Vector3.Transform(v, CreateFromQuaternion(q)) — the row-vector product v*R. Expanded from the row-major
// Matrix4x4.CreateFromQuaternion entries (same layout as pCfq in point_ops_polartransformpoints).
simd::float3 transformByQuat(simd::float3 v, Quat q) {
  float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, wz = q.z * q.w,
        xz = q.z * q.x, wy = q.y * q.w, yz = q.y * q.z, wx = q.x * q.w;
  // Row-major matrix rows (r[i][*]); v*R uses columns → group by output component.
  float m00 = 1 - 2 * (yy + zz), m01 = 2 * (xy + wz),     m02 = 2 * (xz - wy);
  float m10 = 2 * (xy - wz),     m11 = 1 - 2 * (zz + xx), m12 = 2 * (yz + wx);
  float m20 = 2 * (xz + wy),     m21 = 2 * (yz - wx),     m22 = 1 - 2 * (yy + xx);
  return simd::make_float3(v.x * m00 + v.y * m10 + v.z * m20,
                           v.x * m01 + v.y * m11 + v.z * m21,
                           v.x * m02 + v.y * m12 + v.z * m22);
}

simd::float3 rotateBasis(simd::float3 eulerDegrees, simd::float3 basis) {
  // SpatialAudioPlayer.cs:185-188/202-205: CreateFromYawPitchRoll(Y·(pi/180), X·(pi/180), Z·(pi/180)).
  const float d2r = kPi / 180.0f;
  Quat q = cfypr(eulerDegrees.y * d2r, eulerDegrees.x * d2r, eulerDegrees.z * d2r);
  return transformByQuat(basis, q);
}

}  // namespace

simd::float3 spatialForwardFromEuler(simd::float3 eulerDegrees) {
  return rotateBasis(eulerDegrees, simd::make_float3(0, 0, 1));  // cs:189/206 Transform((0,0,1), R)
}
simd::float3 spatialUpFromEuler(simd::float3 eulerDegrees) {
  return rotateBasis(eulerDegrees, simd::make_float3(0, 1, 0));  // cs:190 Transform((0,1,0), R)
}

float spatialDistanceAttenuation(float distanceToListener, float minDistanceIn, float maxDistanceIn) {
  // OPERATOR clamp (SpatialAudioPlayer.cs:192-195).
  float minD = minDistanceIn;
  if (minD <= 0.0f) minD = 1.0f;
  float maxD = maxDistanceIn;
  if (maxD <= minD) maxD = minD + 10.0f;
  // STREAM clamp (SpatialOperatorAudioStream.cs:247-248) — the floor that guarantees a non-degenerate range.
  minD = std::fmax(0.1f, minD);
  maxD = std::fmax(minD + 0.1f, maxD);
  // Piecewise-linear falloff (cs:257-269).
  if (distanceToListener <= minD) return 1.0f;
  if (distanceToListener >= maxD) return 0.0f;
  float range = maxD - minD;
  float t = (distanceToListener - minD) / range;  // 0 at min, 1 at max
  // Test-only: corrupt the REAL falloff (drop the 1.0- inversion → the near/far attenuation swaps) so the
  // golden's diverging-middle probe (0.5 at the midpoint) goes RED on the actual math. Off in production.
  if (g_spatialBug) return t;
  return 1.0f - t;  // cs:269
}

float spatialEffectiveVolume(float currentVolume, float attenuation, bool mute) {
  // cs:451-459: muted OR beyond max (attenuation==0) → 0; else volume * attenuation.
  if (mute) return 0.0f;
  if (attenuation <= 0.0f) return 0.0f;
  return currentVolume * attenuation;
}

// Test-only latch (goldens): rot the REAL clamp path so a degenerate min/max no longer floors attenuation.
void setSpatialAudioBug(bool on) { g_spatialBug = on; }
bool spatialAudioBug() { return g_spatialBug; }

}  // namespace sw
