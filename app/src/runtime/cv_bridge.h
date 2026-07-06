// runtime/cv_bridge — the thin OpenCV seam for the io/video vision operators.
//
// ZONE: runtime (pure computation leaf). This is the ONE place cv:: headers are allowed to enter the
// codebase — every `#include <opencv2/...>` lives in cv_bridge.cpp, NEVER in a business/UI file
// (ARCHITECTURE.md dependency hygiene: the OpenCV dependency is quarantined behind a plain-struct
// header so no other TU pulls in cv::Mat). Callers see only POD types below.
//
// What crosses the seam (the genuine OpenCV numerics that had no closed form, census io/video R3
// "DEFERRED" until this seam existed):
//   (a) CameraCalibrator  (external/tixl/Operators/Lib/io/video/CameraCalibrator.cs)
//       - findChessboardCorners  (cs:270)  — recover inner-corner pixel coords from a board image
//       - calibrateCamera        (cs:309-319, RationalModel + 50/1e-4 term criteria) — K + dist
//   (b) Video2DPointScanner (external/tixl/Operators/Lib/io/dmx/helpers/Video2DPointScanner.cs)
//       - FindBrightSpots        (cs:374-418) — grayscale→threshold(t*255)→findContours(External)→
//         contourArea>2→moments centroid, then device-normalize to [-1,1] (cs:394-395 / 410-411)
//
// The DEVICE-SIDE texture readback (cs ConvertTextureToMat: DX11 staging copy) is NOT here — that is
// the hardware leg (a real camera / Metal texture staging), deferred-hw-verify. This leaf takes an
// already-CPU 8-bit image (host pixel buffer) and does the vision math a MV tool can rely on offline.
//
// runtime leaf: no hardware, no UI, no upward dep.
#pragma once

#include <cstdint>
#include <vector>

namespace sw {
namespace cv_bridge {

// ── A host 8-bit image handed across the seam. Grayscale (channels==1) or BGR (channels==3), row-major,
// tightly packed (no stride padding). The seam converts to the cv::Mat the TiXL .cs used internally.
struct HostImage {
  int width = 0;
  int height = 0;
  int channels = 1;                 // 1 = GRAY, 3 = BGR (matches TiXL's Cv2.CvtColor(...BGR...) inputs)
  std::vector<uint8_t> pixels;      // size == width*height*channels
};

// ── (a) CameraCalibrator ─────────────────────────────────────────────────────────────────────────
// A detected inner-corner in image pixels (Point2f from findChessboardCorners, cs:270).
struct Corner2 { float x = 0, y = 0; };

// findChessboardCorners(gray, Size(boardW,boardH), corners) — cs:270. Returns true when the full
// boardW×boardH inner grid was found; `corners` is then filled row-major (boardW*boardH entries).
bool findChessboardCorners(const HostImage& img, int boardW, int boardH, std::vector<Corner2>& corners);

// calibrateCamera(objectPoints, imagePoints, imageSize, K, dist, ...) — cs:309-319. Faithful to the
// operator's MULTI-VIEW form: the SAME object grid (mm, Z=0) is paired with each captured view's image
// corners (cs `_calibrationObjectPoints` / `_calibrationImagePoints`, one entry per Capture). Runs the
// SAME flags (RationalModel) and term criteria (50 / 1e-4). A planar target needs ≥2 differently-tilted
// views to make the intrinsics well-posed — mirrors cs:MinCalibrationSamples=15 (we use fewer synthetic
// views). `outK` is row-major 3×3; `outDist` is the (up-to-8) rational coeffs. Returns RMS (cs `rms`),
// or a negative value on failure.
double calibrateCameraViews(const std::vector<float>& objXYZ,                 // 3*N: (x*mm,y*mm,0) per corner
                            const std::vector<std::vector<Corner2>>& views,   // per-view N image corners
                            int imageW, int imageH,
                            double outK[9], std::vector<double>& outDist);

// ── (b) Video2DPointScanner ─────────────────────────────────────────────────────────────────────
// A detected bright spot: normalized device position (cs:394-395 mapping, [-1,1], Y up) + pixel centroid.
struct BrightSpot {
  float ndcX = 0, ndcY = 0;   // ((cx/W - 0.5)*2, (cy/H - 0.5)*-2) — the currency coords TiXL emits
  float pixelX = 0, pixelY = 0;
  double area = 0;            // contourArea (for the >2 filter + largest-blob ordering)
};

// FindBrightSpots(frame, findAll, threshold) — cs:374-418. `threshold` in [0,1] (TiXL scales *255,
// cs:380). findAll=true → every contour with area>2 (cs:384-399); findAll=false → the single largest
// (cs:401-414). Returns spots in the SAME order TiXL yields them (contour order for all; largest first).
std::vector<BrightSpot> findBrightSpots(const HostImage& img, bool findAll, float threshold);

// ── Golden TEETH hook (sticky module switch; golden restores 0). Corrupts ONE real numeric term on the
// genuine OpenCV cook path so the closed-form assert goes RED (NOT a want-flip; GOLDEN_STANDARD 三特徵).
//   1 = calibrate: anisotropically stretch the object-corner grid (X≠Y) fed to calibrateCamera → the
//       board's true aspect ratio is wrong, which K+extrinsics cannot absorb (a uniform scale would be;
//       the anisotropy forces fx/fy + RMS to diverge from the closed-form pinhole intrinsics)
//   2 = bright-spot: drop the *2 span + Y-flip in the device-normalize (cs:394-395), so centroids land
//       at the wrong NDC — the coordinate-mapping seam the point-list currency depends on
void setCvBug(int mode);
int cvBug();

}  // namespace cv_bridge
}  // namespace sw
