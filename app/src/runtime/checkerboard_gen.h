// runtime/checkerboard_gen — the CLOSED-FORM geometry of CameraCalibrator, factored out of OpenCV so
// it verifies without a camera or OpenCvSharp.
//
// ZONE: runtime (pure computation). The CameraCalibrator operator (external/tixl/Operators/Lib/io/
// video/CameraCalibrator.cs) has THREE kinds of work:
//   (a) checkerboard PATTERN generation (cs:347-373) + 3D object-corner grid (cs:430-437) — PURE
//       GEOMETRY, no OpenCV. That is what lives here and is fully golden-able.
//   (b) chessboard-corner DETECTION + camera CALIBRATION (Cv2.FindChessboardCorners /
//       Cv2.CalibrateCamera) — genuine OpenCV numerics, no closed form, no OpenCV dep in sw yet →
//       DEFERRED (census io/video R3; a future OpenCV/AVFoundation seam).
//   (c) lens undistort (Cv2.Undistort / Remap) — same OpenCV dependency → DEFERRED.
// This file is honest layer (a): the deterministic part a MV tool can rely on regardless of the
// device side.
//
// runtime leaf: no hardware, no UI, no upward dep. Header-only.
#pragma once

#include <cstdint>
#include <vector>

namespace sw {
namespace calib {

// TiXL CameraCalibrator.cs:355-356 — the checkerboard is drawn on a grid of squares one wider/taller
// than the (W+1)×(H+1) inner pattern, plus a `border` of quiet squares on every side:
//   totalSquaresX = patternW + 1 + border*2
//   totalSquaresY = patternH + 1 + border*2
struct CheckerLayout {
  int totalSquaresX = 0;
  int totalSquaresY = 0;
  double squareWidth = 0.0;   // imageW / totalSquaresX   (cs:357, float division)
  double squareHeight = 0.0;  // imageH / totalSquaresY   (cs:358)
};

inline CheckerLayout checkerLayout(int patternW, int patternH, int imageW, int imageH, int border) {
  CheckerLayout L;
  L.totalSquaresX = patternW + 1 + border * 2;
  L.totalSquaresY = patternH + 1 + border * 2;
  if (L.totalSquaresX > 0) L.squareWidth = static_cast<double>(imageW) / L.totalSquaresX;
  if (L.totalSquaresY > 0) L.squareHeight = static_cast<double>(imageH) / L.totalSquaresY;
  return L;
}

// TiXL CameraCalibrator.cs:360-368 — which pattern cells are BLACK. The background is white
// (g.Clear(White)); a cell (x,y) for x in [0..patternW], y in [0..patternH] is filled black when
// (x+y) is EVEN. (The classic parity checker; corner cell (0,0) is black.)
inline bool checkerCellIsBlack(int x, int y) { return ((x + y) % 2) == 0; }

// TiXL CameraCalibrator.cs:366 — the top-left PIXEL of pattern cell (x,y), offset by the border:
//   px = (x + border) * squareWidth
//   py = (y + border) * squareHeight
struct CellRect { double x = 0, y = 0, w = 0, h = 0; };
inline CellRect checkerCellRect(int x, int y, int border, const CheckerLayout& L) {
  return {(x + border) * L.squareWidth, (y + border) * L.squareHeight, L.squareWidth, L.squareHeight};
}

// TiXL CameraCalibrator.cs:430-437 — the 3D object points of the inner corners of a board×board
// chessboard, on the Z=0 plane, spaced squareInMm apart. Row-major over the INNER grid
// (boardW × boardH), corner index i = y*boardW + x → (x*mm, y*mm, 0).
struct Corner3 { float x = 0, y = 0, z = 0; };
inline std::vector<Corner3> chessboardObjectCorners(int boardW, int boardH, float squareInMm) {
  std::vector<Corner3> out;
  out.reserve(static_cast<size_t>(boardW) * boardH);
  for (int y = 0; y < boardH; ++y)
    for (int x = 0; x < boardW; ++x)
      out.push_back({x * squareInMm, y * squareInMm, 0.0f});
  return out;
}

// ── Golden TEETH hook (--selftest-io-camera-checkerboard). Sticky module switch; golden restores 0.
// 0 = production. Corrupts ONE real geometry term so the closed-form assert goes RED (NOT a want-flip):
//   1 = drop the border offset in checkerCellRect (cells land at the wrong pixel)
//   2 = flip the black-cell parity (odd instead of even → the whole pattern inverts)
inline int& checkerBugRef() { static int mode = 0; return mode; }
inline void setCheckerBug(int mode) { checkerBugRef() = mode; }
inline int  checkerBug() { return checkerBugRef(); }

// Bug-aware variants used by the golden (production paths above stay clean; these are what the golden
// exercises so the teeth ride the real formula, not a duplicated one).
inline bool checkerCellIsBlackCooked(int x, int y) {
  if (checkerBug() == 2) return ((x + y) % 2) != 0;  // TEETH: inverted parity
  return checkerCellIsBlack(x, y);
}
inline CellRect checkerCellRectCooked(int x, int y, int border, const CheckerLayout& L) {
  const int b = (checkerBug() == 1) ? 0 : border;  // TEETH: forget the border offset
  return {(x + b) * L.squareWidth, (y + b) * L.squareHeight, L.squareWidth, L.squareHeight};
}

}  // namespace calib
}  // namespace sw
