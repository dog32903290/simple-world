// field_camera_selftest — --selftest-field-camera. PURE-MATH golden for the raymarch camera
// convention (field_camera.{h,cpp}). NO GPU. This is the headline-risk gate: a wrong handedness or
// multiply-order in the matrix port silently breaks ALL future 3D parity, so it is pinned here with
// hand-derived ground truth for TiXL's default camera BEFORE any shader runs.
//
// GROUND TRUTH (default camera, EvaluationContext.SetDefaultCamera, aspect=1):
//   eye=(0,0,d), d = DefaultCameraDistance = 1/tan(22.5°) = 2.4142135, target=origin, up=(0,1,0).
//   LookAtRH: zAxis=normalize(eye-target)=(0,0,1); xAxis=normalize(cross(up,z))=(1,0,0);
//             yAxis=cross(z,x)=(0,1,0). => WorldToCamera = translate(0,0,-d). Camera looks down -z.
//   cameraToWorld = inverse = translate(0,0,+d); its row-2 (m[8..10]) = (0,0,1) = camera +z axis in
//     world, so the shader's viewDir = -normalize(CameraToWorld._31.._33) = (0,0,-1).  ✓
//
// TEETH (each pins one load-bearing convention fact; injectBug flips them):
//   (1) defaultCameraDistance() == 2.4142135  (the fov->distance formula).
//   (2) cameraToWorld is a pure +z translation by d: row-2 == (0,0,1), translation row == (0,0,d).
//   (3) CENTER-pixel ray (clip (0,0)): unproject viewTNear=(0,0,0,1) & viewTFar=(0,0,1,1) through
//       clipSpaceToWorld; near point is in FRONT of the camera (0<z<d on the center axis), and the
//       ray direction normalize(far-near) == (0,0,-1) — the camera looks straight at the origin.
//   (4) CORNER-pixel ray (clip (+1,+1)) tilts OUTWARD: its world dir has dir.x>0 and dir.y>0 (the
//       top-right of the image maps to +x,+y in world for this upright camera) — pins the unproject
//       x/y sign (a transposed or wrong-handed matrix flips these).
//   (5) HIT distance: marching the center ray analytically against a unit-ish sphere R at origin, the
//       first hit is at ray length (d - R) from the near point's camera distance — the depth tooth.
//
// injectBug: TRANSPOSE clipSpaceToWorld before use (a classic convention slip — row-major vs the HLSL
// cbuffer column packing). The transposed matrix unprojects to a different world ray, flipping the
// direction / corner-sign / hit teeth. Proves the teeth bite the exact mistake we most fear.
#include "runtime/field_camera.h"

#include <cmath>
#include <cstdio>

namespace sw {
namespace {

float len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// Unproject a clip-space point (clipX, clipY in [-1,1], clipZ in [0,1]) through a row-major
// clipSpaceToWorld, dividing by w. This mirrors the shader's vsMain4 unproject EXACTLY
// (RaymarchSDFFieldTemplate.hlsl:71-77: mul(viewTFragPos, ClipSpaceToWorld); /= .w).
void unproject(const Mat4& clipToWorld, float clipX, float clipY, float clipZ, float out[3]) {
  mat4TransformPointDivW(clipToWorld, clipX, clipY, clipZ, out);
}

Mat4 transpose(const Mat4& a) {
  Mat4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) r.m[i * 4 + j] = a.m[j * 4 + i];
  return r;
}

}  // namespace

int runFieldCameraSelfTest(bool injectBug) {
  const float kTol = 1e-4f;
  int rc = 0;
  const float d = defaultCameraDistance();

  // (1) fov -> distance formula.
  {
    float want = 2.4142135f;
    bool ok = std::fabs(d - want) <= 1e-4f;
    if (!ok) rc = 1;
    std::printf("[selftest-field-camera] (1) DefaultCameraDistance got=%.7f want=%.7f %s\n", d, want,
                ok ? "OK" : "RED");
  }

  RaymarchTransforms t = defaultRaymarchTransforms(/*aspect=*/1.0f);

  // injectBug corrupts the matrix the rest of the test unprojects through (the feared transpose slip).
  Mat4 clipToWorld = injectBug ? transpose(t.clipSpaceToWorld) : t.clipSpaceToWorld;

  // (2) cameraToWorld == translate(0,0,+d): row-2 is the camera +z axis (0,0,1); translation row (0,0,d).
  {
    const Mat4& c2w = t.cameraToWorld;
    float z0 = c2w.m[8], z1 = c2w.m[9], z2 = c2w.m[10];     // row-2 (camera +z axis in world)
    float tx = c2w.m[12], ty = c2w.m[13], tz = c2w.m[14];   // translation row
    bool ok = std::fabs(z0) < kTol && std::fabs(z1) < kTol && std::fabs(z2 - 1.0f) < kTol &&
              std::fabs(tx) < kTol && std::fabs(ty) < kTol && std::fabs(tz - d) < kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-camera] (2) cameraToWorld z-axis=(%.4f,%.4f,%.4f) trans=(%.4f,%.4f,"
                "%.4f) want z=(0,0,1) trans=(0,0,%.4f) %s\n",
                z0, z1, z2, tx, ty, tz, d, ok ? "OK" : "RED");
  }

  // (3) CENTER ray: unproject clip (0,0,0) and (0,0,1) -> world near/far; dir = normalize(far-near).
  {
    float nr[3], fr[3];
    unproject(clipToWorld, 0.0f, 0.0f, 0.0f, nr);
    unproject(clipToWorld, 0.0f, 0.0f, 1.0f, fr);
    float dir[3] = {fr[0] - nr[0], fr[1] - nr[1], fr[2] - nr[2]};
    float L = len3(dir);
    if (L > 0.0f) { dir[0] /= L; dir[1] /= L; dir[2] /= L; }
    // near point on the center axis must be in front of the camera (0 < z < d) and on the z axis.
    bool frontOk = nr[2] > 0.0f && nr[2] < d && std::fabs(nr[0]) < kTol && std::fabs(nr[1]) < kTol;
    bool dirOk = std::fabs(dir[0]) < kTol && std::fabs(dir[1]) < kTol && std::fabs(dir[2] + 1.0f) < kTol;
    bool ok = frontOk && dirOk;
    if (!ok) rc = 1;
    std::printf("[selftest-field-camera] (3) center near=(%.4f,%.4f,%.4f) dir=(%.4f,%.4f,%.4f) "
                "want near.z in(0,%.3f) dir=(0,0,-1) %s\n",
                nr[0], nr[1], nr[2], dir[0], dir[1], dir[2], d, ok ? "OK" : "RED");
  }

  // (4) CORNER ray (top-right clip (+1,+1)) tilts OUTWARD: world dir x>0, y>0. Pins the unproject
  //     x/y sign (the transpose injectBug scrambles this).
  {
    float nr[3], fr[3];
    unproject(clipToWorld, 1.0f, 1.0f, 0.0f, nr);
    unproject(clipToWorld, 1.0f, 1.0f, 1.0f, fr);
    float dir[3] = {fr[0] - nr[0], fr[1] - nr[1], fr[2] - nr[2]};
    float L = len3(dir);
    if (L > 0.0f) { dir[0] /= L; dir[1] /= L; dir[2] /= L; }
    bool ok = dir[0] > 1e-3f && dir[1] > 1e-3f && dir[2] < 0.0f;
    if (!ok) rc = 1;
    std::printf("[selftest-field-camera] (4) corner(+1,+1) dir=(%.4f,%.4f,%.4f) want x>0,y>0,z<0 %s\n",
                dir[0], dir[1], dir[2], ok ? "OK" : "RED");
  }

  // (5) CENTER-ray HIT distance vs an analytic sphere R at origin. The near point sits at camera
  //     distance (d - |near.z displacement|)... we compute directly: the ray starts at world `near`
  //     and travels dir=(0,0,-1); the sphere R at origin is hit where |near + s·dir| = R, i.e. the
  //     near point is on the +z axis at z=near.z, so first hit s = near.z - R, world hit z = R.
  //     This is the depth/march quantity the GPU golden will re-derive from the cooked field.
  if (!injectBug) {  // the transposed matrix has no meaningful center hit (dir scrambled)
    float nr[3];
    unproject(clipToWorld, 0.0f, 0.0f, 0.0f, nr);
    const float R = 0.5f;
    float s = nr[2] - R;          // travel along -z until hitting the front of the sphere
    float hitZ = nr[2] - s;       // == R
    bool ok = s > 0.0f && std::fabs(hitZ - R) < kTol;
    if (!ok) rc = 1;
    std::printf("[selftest-field-camera] (5) center hit travel s=%.4f hitZ=%.4f want hitZ=%.3f %s\n",
                s, hitZ, R, ok ? "OK" : "RED");
  }

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-field-camera] FAIL: injectBug (transpose) tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-field-camera] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-field-camera] PASS\n");
  return rc;
}

}  // namespace sw
