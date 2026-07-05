// io_video_transform_golden — VideoDeviceInput frame-transform golden (--selftest-io-video-transform).
//
// Layer (a) of the io/video VideoDeviceInput: the OpenCV-free part. The live capture (DirectShow
// device scan + OpenCV VideoCapture on a background thread; macOS AVCaptureSession) is device-bound →
// DEFERRED (census io/video R3, deferred-hw-verify). This golden pins the closed-form 2×3 WarpAffine
// matrix (rotate+scale about the frame center, then reposition) and the epsilon "has transform?" gate
// — the deterministic reposition a MV tool relies on regardless of the camera.
//
// TiXL authority: external/tixl/Operators/Lib/io/video/VideoDeviceInput.cs
//   UpdateTransformationMatrix (cs:382-402) — the 2×3 affine about center + reposition.
//   CaptureThreadSettings.HasTransformation (cs:485-490) — the epsilon identity gate.
//
// injectBug drives runtime/video_device_transform.h's setVideoTransformBug (real cook-term
// corruption, NOT a want-flip). did-not-trip → return 0 (GOLDEN_STANDARD P1).
#include <cmath>
#include <cstdio>

#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS
#include "runtime/video_device_transform.h"

namespace sw {

namespace {
bool approx(double a, double b, double eps = 1e-6) { return std::abs(a - b) <= eps; }
}  // namespace

int runIoVideoTransformSelfTest(bool injectBug) {
  using namespace sw::videodev;
  bool ok = true;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G1. HasTransformation epsilon gate (cs:485-490). Identity (rot 0, scale 1, reposition 0) → false;
  //     any axis departing identity by > 1e-4 → true. Probes each axis independently so a dropped
  //     term shows.
  ok = ok && hasTransformation(0, 1, 1, 0, 0) == false;                  // identity
  ok = ok && hasTransformation(0.001, 1, 1, 0, 0) == true;               // rotation
  ok = ok && hasTransformation(0, 1.001, 1, 0, 0) == true;               // scaleX
  ok = ok && hasTransformation(0, 1, 0.999, 0, 0) == true;               // scaleY
  ok = ok && hasTransformation(0, 1, 1, 0.001, 0) == true;               // repositionX
  ok = ok && hasTransformation(0, 1, 1, 0, -0.001) == true;              // repositionY
  ok = ok && hasTransformation(0, 1.00001, 1, 0, 0) == false;            // below eps → still identity

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G2. 90° rotation, scale 1, no reposition, 640×480 frame (cs:382-402). center=(320,240).
  //     c=cos90=0, s=sin90=1:
  //       m00=0  m01=1   m10=-1  m11=0
  //       tx=(1-0)*320 - 1*240 = 80    ty=1*320 + (1-0)*240 = 560
  //       m02=80  m12=560
  //     The nonzero center-pivot translate (80, 560) is the ANCHOR: bug 2 (drop pivot) → (0,0) → RED;
  //     bug 1 (flip sin handedness) → m01/m10 sign swap AND tx/ty change → RED.
  setVideoTransformBug(injectBug ? 1 : 0);
  {
    Affine2x3 M = transformationMatrixCooked(640, 480, /*rot=*/90, /*Sx=*/1, /*Sy=*/1, 0, 0);
    ok = ok && approx(M.m00, 0.0) && approx(M.m01, 1.0);
    ok = ok && approx(M.m10, -1.0) && approx(M.m11, 0.0);
    ok = ok && approx(M.m02, 80.0) && approx(M.m12, 560.0);
  }
  setVideoTransformBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G3. Center-pivot translate, tested via the drop-pivot tooth. Same 90° case: with the pivot the
  //     translate is (80, 560); without it (bug 2) the translate collapses to reposition (0,0).
  setVideoTransformBug(injectBug ? 2 : 0);
  {
    Affine2x3 M = transformationMatrixCooked(640, 480, 90, 1, 1, 0, 0);
    ok = ok && approx(M.m02, 80.0) && approx(M.m12, 560.0);
  }
  setVideoTransformBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G4. Identity transform (rot 0, scale 1, reposition 0) via the PRODUCTION path. c=1,s=0 → the
  //     matrix is [1 0 0 ; 0 1 0]: translate is exactly zero (center pivot is a no-op at identity).
  {
    Affine2x3 M = transformationMatrix(640, 480, 0, 1, 1, 0, 0);
    ok = ok && approx(M.m00, 1.0) && approx(M.m01, 0.0) && approx(M.m02, 0.0);
    ok = ok && approx(M.m10, 0.0) && approx(M.m11, 1.0) && approx(M.m12, 0.0);
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G5. Pure scale ×2, no rotation, no reposition (production path). c=1,s=0:
  //     m00=2 m11=2 ; tx=(1-2)*320 = -320 ; ty=(1-2)*240 = -240. (A 2× zoom keeps the center fixed by
  //     translating -half the growth.) DIVERGING MIDDLE for the scale-into-translate coupling.
  {
    Affine2x3 M = transformationMatrix(640, 480, 0, /*Sx=*/2, /*Sy=*/2, 0, 0);
    ok = ok && approx(M.m00, 2.0) && approx(M.m11, 2.0);
    ok = ok && approx(M.m02, -320.0) && approx(M.m12, -240.0);
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G6. Reposition ADDS to the pivot translate (cs:400-401). 90°, repositionX=10 → m02 = 80 + 10 = 90.
  {
    Affine2x3 M = transformationMatrix(640, 480, 90, 1, 1, /*Rx=*/10, /*Ry=*/0);
    ok = ok && approx(M.m02, 90.0) && approx(M.m12, 560.0);
  }

  // ── verdict ────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-video-transform] injectBug did NOT trip (a transform tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-video-transform] injectBug correctly RED (corrupted affine transform)\n");
    return 1;
  }
  std::printf("[selftest-io-video-transform] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-video-transform (order 712, after io-camera-checkerboard at 711).
REGISTER_SELFTESTS(/*orderBase=*/712, {"io-video-transform", sw::runIoVideoTransformSelfTest});
