// runtime/video_device_transform — the CLOSED-FORM frame-transform math of VideoDeviceInput, factored
// out of OpenCV / the capture thread so it verifies without a camera.
//
// ZONE: runtime (pure computation). The VideoDeviceInput operator (external/tixl/Operators/Lib/io/
// video/VideoDeviceInput.cs) is mostly device-bound: DirectShow device enumeration + OpenCV
// VideoCapture on a background thread (macOS would replace both with AVCaptureSession) — that whole
// capture path is DEFERRED (census io/video R3, deferred-hw-verify). But it also builds a 2×3 affine
// WarpAffine matrix (rotation+scale+reposition about the frame center, cs:382-402) and gates it with
// an epsilon "is there any transform?" test (cs:485-490). BOTH are pure math, hardware-free, and are
// what a MV tool relies on to reposition a live feed deterministically.
//
// runtime leaf: no hardware, no UI, no upward dep. Header-only.
#pragma once

#include <cmath>

namespace sw {
namespace videodev {

// The 2×3 affine matrix, row-major (OpenCV WarpAffine layout):
//   [ m00 m01 m02 ]
//   [ m10 m11 m12 ]
struct Affine2x3 {
  double m00 = 0, m01 = 0, m02 = 0;
  double m10 = 0, m11 = 0, m12 = 0;
};

// TiXL VideoDeviceInput.cs:27 — the epsilon a transform must exceed to count as "present".
inline constexpr double kEpsilon = 1e-4;

// TiXL VideoDeviceInput.cs:485-490 (CaptureThreadSettings.HasTransformation) — skip the WarpAffine
// entirely unless rotation, either scale axis, or either reposition axis departs identity by > eps:
//   |rotation|>eps || |scaleX-1|>eps || |scaleY-1|>eps || |repositionX|>eps || |repositionY|>eps
inline bool hasTransformation(double rotationDeg, double scaleX, double scaleY,
                              double repositionX, double repositionY) {
  return std::abs(rotationDeg) > kEpsilon ||
         std::abs(scaleX - 1.0) > kEpsilon || std::abs(scaleY - 1.0) > kEpsilon ||
         std::abs(repositionX) > kEpsilon || std::abs(repositionY) > kEpsilon;
}

// TiXL VideoDeviceInput.cs:382-402 (UpdateTransformationMatrix) — rotate+scale about the frame CENTER,
// then translate by Reposition. Derived line-for-line:
//   center = (W/2, H/2)
//   a = rotationDeg * π/180 ; c = cos(a) ; s = sin(a)
//   m00 = c*Sx ; m01 = s*Sy ; m10 = -s*Sx ; m11 = c*Sy
//   tx = (1 - c*Sx)*cx - s*Sy*cy
//   ty = s*Sx*cx + (1 - c*Sy)*cy
//   m02 = repositionX + tx ; m12 = repositionY + ty
inline Affine2x3 transformationMatrix(int frameW, int frameH, double rotationDeg,
                                      double scaleX, double scaleY,
                                      double repositionX, double repositionY) {
  const double cx = frameW / 2.0;
  const double cy = frameH / 2.0;
  const double a = rotationDeg * M_PI / 180.0;
  const double c = std::cos(a);
  const double s = std::sin(a);

  Affine2x3 M;
  M.m00 = c * scaleX;
  M.m01 = s * scaleY;
  M.m10 = -s * scaleX;
  M.m11 = c * scaleY;

  const double tx = (1.0 - c * scaleX) * cx - s * scaleY * cy;
  const double ty = s * scaleX * cx + (1.0 - c * scaleY) * cy;
  M.m02 = repositionX + tx;
  M.m12 = repositionY + ty;
  return M;
}

// ── Golden TEETH hook (--selftest-io-video-transform). Sticky module switch; golden restores 0.
// 0 = production. Corrupts ONE real term so the closed-form assert goes RED (NOT a want-flip):
//   1 = negate the off-diagonal sin terms' sign convention (rotation direction flips)
//   2 = drop the center-pivot translate (tx/ty=0 → rotation pivots about origin, not center)
inline int& videoTransformBugRef() { static int mode = 0; return mode; }
inline void setVideoTransformBug(int mode) { videoTransformBugRef() = mode; }
inline int  videoTransformBug() { return videoTransformBugRef(); }

inline Affine2x3 transformationMatrixCooked(int frameW, int frameH, double rotationDeg,
                                            double scaleX, double scaleY,
                                            double repositionX, double repositionY) {
  const double cx = frameW / 2.0;
  const double cy = frameH / 2.0;
  const double a = rotationDeg * M_PI / 180.0;
  const double c = std::cos(a);
  double s = std::sin(a);
  if (videoTransformBug() == 1) s = -s;  // TEETH: flip rotation handedness

  Affine2x3 M;
  M.m00 = c * scaleX;
  M.m01 = s * scaleY;
  M.m10 = -s * scaleX;
  M.m11 = c * scaleY;

  double tx = (1.0 - c * scaleX) * cx - s * scaleY * cy;
  double ty = s * scaleX * cx + (1.0 - c * scaleY) * cy;
  if (videoTransformBug() == 2) { tx = 0.0; ty = 0.0; }  // TEETH: drop center pivot
  M.m02 = repositionX + tx;
  M.m12 = repositionY + ty;
  return M;
}

}  // namespace videodev
}  // namespace sw
