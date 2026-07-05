// io_camera_checkerboard_golden — CameraCalibrator checkerboard-GEOMETRY golden
// (--selftest-io-camera-checkerboard).
//
// Layer (a) of the io/video CameraCalibrator: the OpenCV-free part. The corner-DETECTION and camera-
// CALIBRATION (Cv2.FindChessboardCorners / Cv2.CalibrateCamera / Undistort) are genuine OpenCV
// numerics with no closed form and no OpenCV dep in sw → DEFERRED (census io/video R3). This golden
// pins the deterministic pattern-generation geometry a MV tool can rely on regardless of the device.
//
// TiXL authority: external/tixl/Operators/Lib/io/video/CameraCalibrator.cs
//   GenerateCheckerboard (cs:347-373) — layout + which cells are black + cell pixel rects.
//   Create3DChessboardCorners (cs:430-437) — the Z=0 object-corner grid.
//
// injectBug drives runtime/checkerboard_gen.h's setCheckerBug (real cook-term corruption, NOT a
// want-flip). did-not-trip → return 0 (GOLDEN_STANDARD P1).
#include <cmath>
#include <cstdio>

#include "runtime/checkerboard_gen.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

namespace {
bool approx(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }
}  // namespace

int runIoCameraCheckerboardSelfTest(bool injectBug) {
  using namespace sw::calib;
  bool ok = true;

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G1. Layout (cs:355-358). Pattern 7×6 (the .t3 default ChessboardSize), image 1920×1080, border 1:
  //     totalX = 7+1+2 = 10 ; totalY = 6+1+2 = 9 ; sqW = 1920/10 = 192 ; sqH = 1080/9 = 120.
  CheckerLayout L = checkerLayout(/*pW=*/7, /*pH=*/6, /*imgW=*/1920, /*imgH=*/1080, /*border=*/1);
  ok = ok && L.totalSquaresX == 10 && L.totalSquaresY == 9;
  ok = ok && approx(L.squareWidth, 192.0) && approx(L.squareHeight, 120.0);
  //     border 0 → totalX = 8, totalY = 7 ; sqW = 240, sqH = 1080/7 (non-integer, keeps the float
  //     division honest — a probe that would go RED if squareWidth were computed as integer div).
  CheckerLayout L0 = checkerLayout(7, 6, 1920, 1080, 0);
  ok = ok && L0.totalSquaresX == 8 && L0.totalSquaresY == 7;
  ok = ok && approx(L0.squareWidth, 240.0) && approx(L0.squareHeight, 1080.0 / 7.0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G2. Black-cell parity (cs:364). (x+y) even → black. Corner (0,0) black; (1,0) white; (1,1) black.
  //     Bug 2 inverts the parity → every one of these flips → RED. Probes span both parities so the
  //     inversion cannot hide.
  setCheckerBug(injectBug ? 2 : 0);
  ok = ok && checkerCellIsBlackCooked(0, 0) == true;
  ok = ok && checkerCellIsBlackCooked(1, 0) == false;
  ok = ok && checkerCellIsBlackCooked(0, 1) == false;
  ok = ok && checkerCellIsBlackCooked(1, 1) == true;
  ok = ok && checkerCellIsBlackCooked(3, 2) == false;  // 3+2=5 odd → white (diverging middle)
  ok = ok && checkerCellIsBlackCooked(2, 2) == true;   // 2+2=4 even → black
  setCheckerBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G3. Cell pixel rect with border offset (cs:366). Using L (border 1, sqW 192, sqH 120):
  //     cell (2,3) → px = (2+1)*192 = 576 ; py = (3+1)*120 = 480 ; size 192×120. The MIDDLE cell is
  //     the probe: it depends on BOTH the border add and the square size. Bug 1 drops the border →
  //     px = 2*192 = 384 ≠ 576 → RED.
  setCheckerBug(injectBug ? 1 : 0);
  CellRect r = checkerCellRectCooked(/*x=*/2, /*y=*/3, /*border=*/1, L);
  ok = ok && approx(r.x, 576.0) && approx(r.y, 480.0);
  ok = ok && approx(r.w, 192.0) && approx(r.h, 120.0);
  //     Cell (0,0) still offset by the border → NOT the origin (px=192, py=120). Distinguishes the
  //     border-drop tooth from a no-op at the corner.
  CellRect r00 = checkerCellRectCooked(0, 0, 1, L);
  ok = ok && approx(r00.x, 192.0) && approx(r00.y, 120.0);
  setCheckerBug(0);

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G4. 3D object corners (cs:430-437). board 7×6, square 25mm → 42 corners, row-major.
  //     corner i = y*7 + x → (x*25, y*25, 0). Probe the interior corner (3,2) = index 2*7+3 = 17 →
  //     (75, 50, 0) — a diverging middle where a swapped x/y or a dropped *mm would show.
  auto corners = chessboardObjectCorners(/*boardW=*/7, /*boardH=*/6, /*mm=*/25.0f);
  ok = ok && corners.size() == 42u;
  ok = ok && approx(corners[0].x, 0.0) && approx(corners[0].y, 0.0) && approx(corners[0].z, 0.0);
  const int idx = 2 * 7 + 3;  // (x=3, y=2)
  ok = ok && approx(corners[idx].x, 75.0) && approx(corners[idx].y, 50.0) &&
       approx(corners[idx].z, 0.0);
  //     Last corner (6,5) = (150, 125, 0).
  ok = ok && approx(corners.back().x, 150.0) && approx(corners.back().y, 125.0);

  // ── verdict ────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-camera-checkerboard] injectBug did NOT trip (a geometry tooth is "
                  "dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-camera-checkerboard] injectBug correctly RED (corrupted checkerboard "
                "geometry)\n");
    return 1;
  }
  std::printf("[selftest-io-camera-checkerboard] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-camera-checkerboard (order 711, after io-video-timing at 710).
REGISTER_SELFTESTS(/*orderBase=*/711, {"io-camera-checkerboard", sw::runIoCameraCheckerboardSelfTest});
