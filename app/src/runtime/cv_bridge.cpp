// runtime/cv_bridge — OpenCV seam impl. The ONLY TU that includes <opencv2/...> (ARCHITECTURE.md:
// the OpenCV dependency is quarantined here; the header exposes only POD). Every call below mirrors a
// specific line of the TiXL .cs it clones (line numbers in comments).
#include "runtime/cv_bridge.h"

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>   // findChessboardCorners, calibrateCamera (CameraCalibrator.cs)
#include <opencv2/imgproc.hpp>   // cvtColor, threshold, findContours, moments (Video2DPointScanner.cs)
#include <opencv2/videoio.hpp>   // VideoCapture (VideoStreamInput.cs)

#include <algorithm>

namespace sw {
namespace cv_bridge {

namespace {
int g_bug = 0;

// Build the cv::Mat the TiXL .cs held internally from a host pixel buffer. TiXL's ConvertTextureToMat
// (cs) yields a BGR (3-channel) Mat; a 1-channel host image is treated as the already-gray Mat the
// detection paths CvtColor down to. No copy of ownership — cv::Mat wraps our buffer, valid for the call.
cv::Mat wrap(const HostImage& img) {
  int type = (img.channels == 1) ? CV_8UC1 : CV_8UC3;
  return cv::Mat(img.height, img.width, type,
                 const_cast<uint8_t*>(img.pixels.data()));
}

// The BGR2GRAY the two operators run before detection (CameraCalibrator.cs:266,
// Video2DPointScanner.cs:378). A 1-channel input is passed through.
cv::Mat toGray(const cv::Mat& m) {
  if (m.channels() == 1) return m;
  cv::Mat gray;
  cv::cvtColor(m, gray, cv::COLOR_BGR2GRAY);
  return gray;
}
}  // namespace

void setCvBug(int mode) { g_bug = mode; }
int cvBug() { return g_bug; }

// ── (a) CameraCalibrator ──────────────────────────────────────────────────────────────────────────
bool findChessboardCorners(const HostImage& img, int boardW, int boardH,
                           std::vector<Corner2>& corners) {
  corners.clear();
  cv::Mat gray = toGray(wrap(img));
  std::vector<cv::Point2f> found;
  // CameraCalibrator.cs:270 — Cv2.FindChessboardCorners(gray, boardSize, out corners).
  bool ok = cv::findChessboardCorners(gray, cv::Size(boardW, boardH), found);
  if (!ok) return false;
  corners.reserve(found.size());
  for (const auto& p : found) corners.push_back({p.x, p.y});
  return true;
}

double calibrateCameraViews(const std::vector<float>& objXYZ,
                            const std::vector<std::vector<Corner2>>& views, int imageW, int imageH,
                            double outK[9], std::vector<double>& outDist) {
  const size_t n = objXYZ.size() / 3;
  if (n == 0 || objXYZ.size() != n * 3 || views.empty()) return -1.0;
  for (const auto& v : views)
    if (v.size() != n) return -1.0;

  // TEETH 1: ANISOTROPICALLY perturb the object-corner grid fed to calibrateCamera — stretch the X
  // spacing 6% relative to Y. This corrupts the board's true aspect ratio, which the intrinsics +
  // extrinsics CANNOT absorb (a UNIFORM scale would be soaked up by the depth/tz monocular-scale
  // ambiguity and leave fx/fy intact — a dead tooth; the anisotropy is what forces fx≠fy and blows up
  // the reprojection RMS). NOT a want-flip: the actual numeric input to Cv2.CalibrateCamera is
  // corrupted on the genuine cook path; the expected fx/fy/RMS are unchanged.
  const float scaleX = (g_bug == 1) ? 1.06f : 1.0f;

  // The same object grid is reused for every view (cs Create3DChessboardCorners is called per Capture
  // with identical geometry). RationalModel with a planar target needs ≥2 tilted views to be well-posed.
  std::vector<cv::Point3f> grid;
  grid.reserve(n);
  for (size_t i = 0; i < n; ++i)
    grid.push_back(cv::Point3f(objXYZ[i * 3 + 0] * scaleX, objXYZ[i * 3 + 1], objXYZ[i * 3 + 2]));

  std::vector<std::vector<cv::Point3f>> objectPoints(views.size(), grid);
  std::vector<std::vector<cv::Point2f>> imagePoints(views.size());
  for (size_t vi = 0; vi < views.size(); ++vi) {
    imagePoints[vi].reserve(n);
    for (size_t i = 0; i < n; ++i)
      imagePoints[vi].push_back(cv::Point2f(views[vi][i].x, views[vi][i].y));
  }

  cv::Mat K, dist;
  std::vector<cv::Mat> rvecs, tvecs;
  // CameraCalibrator.cs:309-319 — same flags (RationalModel → up to 8 dist coeffs) and term criteria.
  double rms = cv::calibrateCamera(
      objectPoints, imagePoints, cv::Size(imageW, imageH), K, dist, rvecs, tvecs,
      cv::CALIB_RATIONAL_MODEL,
      cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 50, 0.0001));

  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) outK[r * 3 + c] = K.at<double>(r, c);
  outDist.assign((size_t)dist.total(), 0.0);
  for (size_t i = 0; i < outDist.size(); ++i) outDist[i] = dist.at<double>((int)i);
  return rms;
}

// ── (b) Video2DPointScanner ─────────────────────────────────────────────────────────────────────
std::vector<BrightSpot> findBrightSpots(const HostImage& img, bool findAll, float threshold) {
  std::vector<BrightSpot> out;
  cv::Mat gray = toGray(wrap(img));

  // Video2DPointScanner.cs:380 — Cv2.Threshold(gray, thresh, (int)(threshold*255), 255, Binary).
  cv::Mat thresh;
  cv::threshold(gray, thresh, (int)(threshold * 255), 255, cv::THRESH_BINARY);

  // cs:381 — FindContours(thresh, External, ApproxSimple).
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
  if (contours.empty()) return out;

  const float W = (float)img.width, H = (float)img.height;
  // TEETH 2: drop the *2 span + Y-flip in the device-normalize (cs:394-395 / 410-411). The centroid
  // then lands at raw (cx/W - 0.5, cy/H - 0.5) instead of the currency's [-1,1] Y-up NDC. This is the
  // real coordinate mapping the point-list output depends on — corrupting it moves every emitted point.
  const float span = (g_bug == 2) ? 1.0f : 2.0f;
  const float ysign = (g_bug == 2) ? 1.0f : -1.0f;

  auto emit = [&](const std::vector<cv::Point>& c) {
    double area = cv::contourArea(c);
    if (area <= 2) return;                    // cs:388 / 404 — contourArea>2 filter
    cv::Moments m = cv::moments(c);
    if (m.m00 == 0) return;                   // cs:391 / 407 — M00 != 0 guard
    float cx = (float)(m.m10 / m.m00);
    float cy = (float)(m.m01 / m.m00);
    BrightSpot s;
    s.pixelX = cx;
    s.pixelY = cy;
    s.ndcX = (cx / W - 0.5f) * span;          // cs:394 / 410
    s.ndcY = (cy / H - 0.5f) * span * ysign;  // cs:395 / 411 (Y flipped: * -2)
    s.area = area;
    out.push_back(s);
  };

  if (findAll) {
    for (const auto& c : contours) emit(c);   // cs:384-399 — every contour
  } else {
    // cs:401-403 — the single LARGEST contour by area.
    auto largest = std::max_element(contours.begin(), contours.end(),
        [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
          return cv::contourArea(a) < cv::contourArea(b);
        });
    if (largest != contours.end()) emit(*largest);
  }
  return out;
}

// ── (c) VideoStreamInput ────────────────────────────────────────────────────────────────────────
bool decodeStreamFrame(const std::string& url, int frameIndex, StreamFrame& out) {
  out = StreamFrame{};
  if (frameIndex < 0) return false;

  // VideoStreamInput.cs:148 — new VideoCapture(url, VideoCaptureAPIs.FFMPEG). The FFMPEG backend opens a
  // file path or a network stream identically (a file URL is the closed-form-checkable case).
  cv::VideoCapture cap(url, cv::CAP_FFMPEG);
  if (!cap.isOpened()) return false;                 // cs:150-155 — non-fatal "Error: Failed to open".

  // Advance to the requested frame. A live stream ignores index (next frame); a file decodes to N.
  cv::Mat frame;
  for (int i = 0; i <= frameIndex; ++i) {
    if (!cap.read(frame) || frame.empty()) return false;  // cs:163 _capture.Read(frame) && !frame.Empty()
  }

  // cs:170-173 — allocate a CV_8UC4 BGRA Mat and Cv2.CvtColor(frame, ..., BGR2BGRA). TEETH 3 swaps the
  // B and R channels of the SOURCE before the convert, so the emitted BGRA has B↔R transposed — a real
  // corruption of the channel-order mapping the B8G8R8A8 GPU texture depends on (not a want-flip).
  cv::Mat src = frame;
  if (g_bug == 3) {
    cv::Mat swapped;
    cv::cvtColor(frame, swapped, cv::COLOR_BGR2RGB);  // reinterpret BGR as if R and B were swapped
    src = swapped;
  }
  cv::Mat bgra;
  cv::cvtColor(src, bgra, cv::COLOR_BGR2BGRA);

  out.width = bgra.cols;
  out.height = bgra.rows;
  out.bgra.resize((size_t)bgra.cols * bgra.rows * 4);
  // Copy row-by-row to strip any cv::Mat stride padding (tightly packed, as the header contracts).
  for (int r = 0; r < bgra.rows; ++r)
    std::copy(bgra.ptr<uint8_t>(r), bgra.ptr<uint8_t>(r) + (size_t)bgra.cols * 4,
              out.bgra.data() + (size_t)r * bgra.cols * 4);
  return true;
}

bool writeSolidColorClip(const std::string& path, int width, int height,
                         const std::vector<uint8_t>& bgrColors) {
  const size_t nFrames = bgrColors.size() / 3;
  if (nFrames == 0 || width <= 0 || height <= 0) return false;
  // FFV1 = lossless intra codec in an AVI container. The golden asserts exact pixels, so lossy H.264 etc.
  // are disallowed (they'd quantize the solid colors and break the closed-form).
  cv::VideoWriter w(path, cv::VideoWriter::fourcc('F', 'F', 'V', '1'), 10.0,
                    cv::Size(width, height), /*isColor=*/true);
  if (!w.isOpened()) return false;
  for (size_t i = 0; i < nFrames; ++i) {
    cv::Mat frame(height, width,
                  CV_8UC3,
                  cv::Scalar(bgrColors[i * 3 + 0], bgrColors[i * 3 + 1], bgrColors[i * 3 + 2]));
    w.write(frame);
  }
  w.release();
  return true;
}

}  // namespace cv_bridge
}  // namespace sw
