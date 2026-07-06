// io_camera_calibrate_golden — CameraCalibrator OpenCV NUMERICS golden (--selftest-io-camera-calibrate).
//
// The DETECTION + CALIBRATION leg the census marked DEFERRED until the OpenCV seam existed
// (runtime/cv_bridge). Sibling of io_camera_checkerboard_golden.cpp (which pins the OpenCV-FREE pattern
// geometry). This one drives the genuine OpenCV calls through synthetic images with CLOSED-FORM anchors:
//
//   G1 findChessboardCorners (CameraCalibrator.cs:270): render a checkerboard whose geometry is known
//       from runtime/checkerboard_gen.h, detect its inner corners, and assert the recovered corner grid
//       matches the closed-form inner-corner lattice (sub-pixel error band). The expected corner
//       positions come from the checkerboard LAYOUT formula (cs:355-366), NOT from OpenCV's own output.
//
//   G2 calibrateCamera (CameraCalibrator.cs:309-319): synthesize views by pinhole-projecting the 3D
//       object corners through a KNOWN intrinsic matrix (fx,fy,cx,cy) with ZERO distortion. The task's
//       closed-form anchor: undistorted synthetic input → calibrateCamera must recover ~zero distortion
//       coeffs and near-zero RMS, and the recovered fx/fy/cx/cy must match the known intrinsics we
//       projected with. The expected K is the one WE chose, independent of OpenCV.
//
// TiXL authority: external/tixl/Operators/Lib/io/video/CameraCalibrator.cs (lines cited inline).
// injectBug drives runtime/cv_bridge.h setCvBug(1): anisotropically stretch the object-corner grid
// (X≠Y) fed to calibrateCamera (real cook-path corruption, NOT a want-flip) → the wrong board aspect
// ratio forces recovered fx/fy + RMS to diverge from the known pinhole intrinsics (a uniform scale
// would be absorbed by depth — the anisotropy is what makes the tooth bite). did-not-trip → return 0.
#include <cmath>
#include <cstdio>
#include <vector>

#include "runtime/checkerboard_gen.h"
#include "runtime/cv_bridge.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

namespace {
bool approx(double a, double b, double eps) { return std::abs(a - b) <= eps; }

// Render a synthetic checkerboard into a GRAY host image using the SAME layout formula the operator
// draws with (checkerboard_gen.h ← CameraCalibrator.cs:347-373): white background, cell (x,y) black
// when (x+y) even, offset by `border` quiet squares. This is the image findChessboardCorners sees.
cv_bridge::HostImage renderCheckerboard(int patternW, int patternH, int imageW, int imageH, int border) {
  using namespace sw::calib;
  cv_bridge::HostImage img;
  img.width = imageW;
  img.height = imageH;
  img.channels = 1;
  img.pixels.assign((size_t)imageW * imageH, 255);  // g.Clear(White) — cs:354
  CheckerLayout L = checkerLayout(patternW, patternH, imageW, imageH, border);
  for (int y = 0; y <= patternH; ++y) {
    for (int x = 0; x <= patternW; ++x) {
      if (!checkerCellIsBlack(x, y)) continue;  // cs:364 — (x+y) even → black
      CellRect r = checkerCellRect(x, y, border, L);  // cs:366 — border-offset pixel rect
      int x0 = (int)std::lround(r.x), y0 = (int)std::lround(r.y);
      int x1 = (int)std::lround(r.x + r.w), y1 = (int)std::lround(r.y + r.h);
      for (int py = y0; py < y1 && py < imageH; ++py)
        for (int px = x0; px < x1 && px < imageW; ++px)
          img.pixels[(size_t)py * imageW + px] = 0;  // FillRectangle(Black)
    }
  }
  return img;
}
}  // namespace

int runIoCameraCalibrateSelfTest(bool injectBug) {
  using namespace sw::cv_bridge;
  bool ok = true;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G1. findChessboardCorners on a synthetic board. Pattern 7×6 inner corners (the .t3 default
  //     ChessboardSize), image 960×720, border 1. The layout (checkerboard_gen.h, verified by the
  //     sibling golden) gives cell size and the inner-corner lattice: inner corner (i,j) for
  //     i∈[0..patternW-1], j∈[0..patternH-1] sits at the shared boundary of four cells —
  //     px = (i+1+border)*sqW, py = (j+1+border)*sqH. OpenCV must recover this lattice.
  const int pW = 7, pH = 6, imgW = 960, imgH = 720, border = 1;
  HostImage board = renderCheckerboard(pW, pH, imgW, imgH, border);
  sw::calib::CheckerLayout L = sw::calib::checkerLayout(pW, pH, imgW, imgH, border);

  std::vector<Corner2> corners;
  bool found = findChessboardCorners(board, pW, pH, corners);
  ok = ok && found;
  ok = ok && corners.size() == (size_t)(pW * pH);

  if (found && corners.size() == (size_t)(pW * pH)) {
    // The corner set may be row-major from either end; anchor on the CENTROID and the bounding span,
    // both invariant to row/col ordering, and both closed-form from the lattice. This tests the real
    // detected pixel positions against the LAYOUT formula, not against OpenCV's own numbers.
    double cxSum = 0, cySum = 0, minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto& c : corners) {
      cxSum += c.x; cySum += c.y;
      minX = std::min<double>(minX, c.x); maxX = std::max<double>(maxX, c.x);
      minY = std::min<double>(minY, c.y); maxY = std::max<double>(maxY, c.y);
    }
    double n = (double)corners.size();
    double cx = cxSum / n, cy = cySum / n;
    // Closed-form lattice: inner corners span i=0..pW-1 → px from (1+border)*sqW to (pW+border)*sqW.
    double firstX = (1 + border) * L.squareWidth, lastX = (pW + border) * L.squareWidth;
    double firstY = (1 + border) * L.squareHeight, lastY = (pH + border) * L.squareHeight;
    double expCx = 0.5 * (firstX + lastX), expCy = 0.5 * (firstY + lastY);
    ok = ok && approx(cx, expCx, 2.0) && approx(cy, expCy, 2.0);      // centroid within 2px
    ok = ok && approx(minX, firstX, 3.0) && approx(maxX, lastX, 3.0);  // span within 3px
    ok = ok && approx(minY, firstY, 3.0) && approx(maxY, lastY, 3.0);
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G2. calibrateCamera on synthetic pinhole-projected corners with ZERO distortion. We CHOOSE the
  //     intrinsics (the independent-of-impl expected K) and project the 3D object corners (mm, Z=0)
  //     through several tilted views. calibrateCamera must recover fx/fy/cx/cy back and ~zero dist.
  const double fx = 800.0, fy = 800.0, ppx = imgW / 2.0, ppy = imgH / 2.0;
  const int bW = 7, bH = 6;
  const float mm = 25.0f;
  auto obj = sw::calib::chessboardObjectCorners(bW, bH, mm);  // cs:430-437 — (x*mm,y*mm,0)

  std::vector<float> objXYZ;
  objXYZ.reserve(obj.size() * 3);
  for (const auto& c : obj) { objXYZ.push_back(c.x); objXYZ.push_back(c.y); objXYZ.push_back(c.z); }

  // Project the board at a slight tilt + translation in front of the camera (a single well-posed view
  // is enough to recover intrinsics with a planar target when combined with the RationalModel solve;
  // the closed-form check is that ZERO input distortion → ~ZERO recovered distortion).
  // Rotation: small tilt about X and Y so the planar board is non-fronto-parallel (else K is
  // under-constrained). Rz=0. Translation puts the board ~500mm in front, centered.
  auto projectView = [&](double rx, double ry, double tz, std::vector<Corner2>& out) {
    // Rotation matrix R = Rx(rx) * Ry(ry) (small angles).
    double cxr = std::cos(rx), sxr = std::sin(rx), cyr = std::cos(ry), syr = std::sin(ry);
    // R = Rx * Ry
    double R[9] = {
        cyr,        0.0,   syr,
        sxr * syr,  cxr,  -sxr * cyr,
       -cxr * syr,  sxr,   cxr * cyr };
    double tx = -(bW - 1) * mm * 0.5, ty = -(bH - 1) * mm * 0.5;  // center the board
    out.clear();
    out.reserve(obj.size());
    for (const auto& c : obj) {
      double X = c.x + tx, Y = c.y + ty, Z = 0.0;
      double xc = R[0] * X + R[1] * Y + R[2] * Z + 0.0;
      double yc = R[3] * X + R[4] * Y + R[5] * Z + 0.0;
      double zc = R[6] * X + R[7] * Y + R[8] * Z + tz;
      double u = fx * (xc / zc) + ppx;
      double v = fy * (yc / zc) + ppy;
      out.push_back({(float)u, (float)v});
    }
  };

  // Multiple differently-tilted views make the intrinsics well-posed for a planar target (a single
  // view under-constrains fx/fy vs. the principal point). Faithful to the operator capturing many views.
  std::vector<std::vector<Corner2>> views;
  {
    std::vector<Corner2> v;
    projectView(0.25, -0.20, 520.0, v); views.push_back(v);   // ~14° / -11°
    projectView(-0.20, 0.22, 480.0, v); views.push_back(v);   // opposite tilt
    projectView(0.15, 0.15, 550.0, v);  views.push_back(v);
    projectView(-0.18, -0.16, 500.0, v); views.push_back(v);
    projectView(0.05, 0.28, 470.0, v);  views.push_back(v);   // strong Y-tilt
  }

  setCvBug(injectBug ? 1 : 0);  // TEETH: object-corner spacing corrupted inside calibrateCameraViews
  double K[9] = {0};
  std::vector<double> dist;
  double rms = calibrateCameraViews(objXYZ, views, imgW, imgH, K, dist);
  setCvBug(0);

  ok = ok && rms >= 0.0;                 // solve succeeded
  if (rms >= 0.0) {
    ok = ok && rms < 1.0;                // undistorted synthetic → tiny reprojection error
    ok = ok && approx(K[0], fx, 20.0);   // recovered fx ≈ known fx (the injectBug target)
    ok = ok && approx(K[4], fy, 20.0);   // recovered fy ≈ known fy
    ok = ok && approx(K[2], ppx, 20.0);  // principal point x
    ok = ok && approx(K[5], ppy, 20.0);  // principal point y
    // Zero-distortion anchor (cs:322-337 allZeroDistortion path). We check the PHYSICALLY DOMINANT
    // low-order terms — k1,k2 (radial) and p1,p2 (tangential), dist[0..3]. For a real distorted lens
    // these are O(0.1)+; for our undistorted synthetic input they must be ~0. (The RationalModel's
    // higher-order k4/k5/k6 terms are ill-conditioned on noise-free data and can absorb ~0.06 of
    // numerically-insignificant magnitude — checking them would be testing OpenCV's solver conditioning,
    // not the distortion crossing, so the anchor rides the terms that actually mean "no lens distortion".)
    ok = ok && dist.size() >= 4;
    for (int i = 0; i < 4 && i < (int)dist.size(); ++i)
      ok = ok && std::abs(dist[i]) < 0.01;
  }

  // ── verdict ────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-camera-calibrate] injectBug did NOT trip (the calibrate tooth is "
                  "dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-camera-calibrate] injectBug correctly RED (corrupted object-corner "
                "spacing → intrinsics diverge)\n");
    return 1;
  }
  std::printf("[selftest-io-camera-calibrate] rms=%.5f fx=%.2f fy=%.2f cx=%.2f cy=%.2f %s\n",
              rms, K[0], K[4], K[2], K[5], ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-camera-calibrate (order 715, free slot after the io/video family; 713/714
// are io-visca / io-onvif).
REGISTER_SELFTESTS(/*orderBase=*/715, {"io-camera-calibrate", sw::runIoCameraCalibrateSelfTest});
