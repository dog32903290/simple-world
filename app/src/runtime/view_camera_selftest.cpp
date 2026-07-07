// view_camera_selftest — --selftest-view-camera. PURE-MATH golden for the mutable output camera
// (view_camera.{h,cpp}). NO GPU, NO UI. Three teeth, each pinning one load-bearing fact of the
// orbit/zoom interaction + the parity floor the whole downstream lane rests on:
//
//   (1) DEFAULT PARITY (the geodesic tooth): toForward(makeDefaultViewCamera(), aspect) must equal
//       defaultLayerCameraForward(aspect) BIT-FOR-BIT (both matrices, memcmp). This is why an
//       override-absent output stays byte-identical to the hard-wired default camera — the 9 downstream
//       injection points swap defaultLayerCameraForward for toForward and MUST see no pixel change until
//       the camera is actually dragged. injectBug perturbs the default eye → the matrices diverge → RED.
//   (2) ORBIT azimuth: from the default (+z) camera, an orbit drag of +90° about Up lands eye in the
//       +X quadrant (x>0, |z|≈0), target does NOT move, and |eye-target| is CONSERVED (distance-
//       preserving orbit). injectBug: skip the length rescale → distance drifts → RED.
//   (3) ZOOM: one wheel notch "in" (wheel>0) shrinks |eye-target| by exactly 1/(1+ZoomSpeed·frame),
//       target unchanged. injectBug: reverse the compare so "in" grows the distance → RED.
//
// injectBug is a SINGLE boolean threaded to the exact mistake each tooth fears (see per-tooth notes).
#include "runtime/view_camera.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kOrbitSensitivity = -0.1f / 60.0f;  // mirror view_camera.cpp (drives the 90° drag)

float len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// |eye - target| for a camera (the orbit/zoom conserved quantity).
float eyeTargetDist(const ViewCamera& cam) {
  float d[3] = {cam.eye[0] - cam.target[0], cam.eye[1] - cam.target[1], cam.eye[2] - cam.target[2]};
  return len3(d);
}

bool matEq(const Mat4& a, const Mat4& b) { return std::memcmp(a.m, b.m, 16 * sizeof(float)) == 0; }

}  // namespace

int runViewCameraSelfTest(bool injectBug) {
  const float kTol = 1e-4f;
  int rc = 0;
  const float aspect = 16.0f / 9.0f;

  // (1) DEFAULT PARITY — the load-bearing tooth. toForward(default) == defaultLayerCameraForward, both
  //     matrices, BIT-FOR-BIT. injectBug nudges the default eye by a hair so the WorldToCamera diverges.
  {
    ViewCamera cam = makeDefaultViewCamera();
    if (injectBug) cam.eye[2] += 0.01f;  // corrupt the default pose → parity must break
    LayerCameraForward got = toForward(cam, aspect);
    LayerCameraForward want = defaultLayerCameraForward(aspect);
    bool w2c = matEq(got.worldToCamera, want.worldToCamera);
    bool c2c = matEq(got.cameraToClipSpace, want.cameraToClipSpace);
    bool ok = w2c && c2c;
    if (!ok) rc = 1;
    std::printf("[selftest-view-camera] (1) default parity worldToCamera=%s cameraToClipSpace=%s "
                "(want BOTH byte-identical to defaultLayerCameraForward) %s\n",
                w2c ? "eq" : "DIFF", c2c ? "eq" : "DIFF", ok ? "OK" : "RED");
  }

  // (2) ORBIT azimuth 90° about Up: eye default (0,0,d) → +X quadrant; target fixed; distance held.
  //     drag dx s.t. orbitX = dx·kOrbitSensitivity = +π/2 (rotate view dir around Up by +90°).
  {
    ViewCamera cam = makeDefaultViewCamera();
    float startDist = eyeTargetDist(cam);
    float tgt0[3] = {cam.target[0], cam.target[1], cam.target[2]};
    float dx = (kPi * 0.5f) / kOrbitSensitivity;  // exactly +90° about Up
    applyOrbitDrag(cam, dx, 0.0f);
    if (injectBug) {                              // injected distance-drift: stretch eye off the sphere
      cam.eye[0] *= 1.5f; cam.eye[1] *= 1.5f; cam.eye[2] *= 1.5f;
    }
    float endDist = eyeTargetDist(cam);
    bool xQuadrant = cam.eye[0] > 1e-2f && std::fabs(cam.eye[2]) < 1e-2f;  // eye swung to +X, off +z
    bool targetFixed = std::fabs(cam.target[0] - tgt0[0]) < kTol &&
                       std::fabs(cam.target[1] - tgt0[1]) < kTol &&
                       std::fabs(cam.target[2] - tgt0[2]) < kTol;
    bool distHeld = std::fabs(endDist - startDist) < 1e-3f;
    bool ok = xQuadrant && targetFixed && distHeld;
    if (!ok) rc = 1;
    std::printf("[selftest-view-camera] (2) orbit90 eye=(%.4f,%.4f,%.4f) dist %.4f->%.4f target-fixed=%d "
                "want eye.x>0,eye.z~0,dist-held %s\n",
                cam.eye[0], cam.eye[1], cam.eye[2], startDist, endDist, targetFixed ? 1 : 0,
                ok ? "OK" : "RED");
  }

  // (3) ZOOM one notch in (wheel>0) → distance shrinks by 1/(1+ZoomSpeed·frame). frame=1/60 → factor.
  {
    ViewCamera cam = makeDefaultViewCamera();
    float d0 = eyeTargetDist(cam);
    float frame = 1.0f / 60.0f;
    float wheel = injectBug ? -1.0f : 1.0f;  // injectBug flips the notch sign: "in" now zooms OUT → RED
    applyZoom(cam, wheel, frame);
    float d1 = eyeTargetDist(cam);
    float factor = 1.0f + (10.0f /*ZoomSpeed*/ * frame);
    float wantD1 = d0 / factor;                        // wheel>0 → closer (divide)
    bool ratioOk = std::fabs(d1 - wantD1) < 1e-4f;     // expected ratio for the +1 notch
    bool closer = d1 < d0;                             // "in" must reduce the distance
    bool ok = ratioOk && closer;
    if (!ok) rc = 1;
    std::printf("[selftest-view-camera] (3) zoom-in dist %.5f->%.5f want %.5f (÷%.4f) %s\n",
                d0, d1, wantD1, factor, ok ? "OK" : "RED");
  }

  if (injectBug) {
    if (rc == 0) {
      std::printf("[selftest-view-camera] FAIL: injectBug tripped no tooth\n");
      return 1;
    }
    std::printf("[selftest-view-camera] injectBug correctly RED\n");
    return 1;
  }
  if (rc == 0) std::printf("[selftest-view-camera] PASS\n");
  return rc;
}

}  // namespace sw
