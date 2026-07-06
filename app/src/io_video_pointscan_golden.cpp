// io_video_pointscan_golden — Video2DPointScanner OpenCV blob-detect golden
// (--selftest-io-video-pointscan).
//
// The FindBrightSpots leg the census marked DEFERRED until the OpenCV seam existed (runtime/cv_bridge).
// Drives the genuine OpenCV threshold→findContours→moments centroid path (Video2DPointScanner.cs:374-418)
// through a synthetic image with CLOSED-FORM anchors:
//
//   G1 (findAll=true, cs:384-399): draw filled bright disks at KNOWN pixel centers. Every disk with
//       area>2 must be recovered as one spot, and its centroid must land at the disk center (the
//       moments centroid of a symmetric disk IS its center — closed form) within a sub-pixel band.
//   G2 device-normalize (cs:394-395): the recovered NDC = ((cx/W - 0.5)*2, (cy/H - 0.5)*-2). The
//       expected NDC is computed from the KNOWN center + image size, NOT from OpenCV — this is the
//       exact point-list currency coordinate the operator emits (Position.xy of each Point).
//   G3 (findAll=false, cs:401-414): with disks of different radii, the single-spot path must select
//       the LARGEST-area disk (its center), not an arbitrary one.
//
// TiXL authority: external/tixl/Operators/Lib/io/dmx/helpers/Video2DPointScanner.cs (lines inline).
// injectBug drives runtime/cv_bridge.h setCvBug(2): drop the *2 span + Y-flip in the device-normalize
// (real cook-path corruption of the coordinate mapping the currency depends on, NOT a want-flip) → the
// recovered NDC diverges from the closed-form. did-not-trip → return 0 (GOLDEN_STANDARD P1).
#include <cmath>
#include <cstdio>
#include <vector>

#include "runtime/cv_bridge.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

namespace {
bool approx(double a, double b, double eps) { return std::abs(a - b) <= eps; }

struct Disk { float cx, cy, r; };

// Paint filled bright disks on a BLACK BGR host image (the FindBrightSpots input: a BGR frame,
// cs ConvertTextureToMat yields BGR). Disk pixels set to white (255,255,255) so the >threshold binary
// isolates them exactly. Closed form: a filled symmetric disk's intensity centroid is its geometric
// center → the expected moments centroid.
cv_bridge::HostImage renderDisks(int imgW, int imgH, const std::vector<Disk>& disks) {
  cv_bridge::HostImage img;
  img.width = imgW;
  img.height = imgH;
  img.channels = 3;
  img.pixels.assign((size_t)imgW * imgH * 3, 0);  // black background
  for (const auto& d : disks) {
    int x0 = (int)std::floor(d.cx - d.r), x1 = (int)std::ceil(d.cx + d.r);
    int y0 = (int)std::floor(d.cy - d.r), y1 = (int)std::ceil(d.cy + d.r);
    for (int py = std::max(0, y0); py <= y1 && py < imgH; ++py) {
      for (int px = std::max(0, x0); px <= x1 && px < imgW; ++px) {
        float dx = px - d.cx, dy = py - d.cy;
        if (dx * dx + dy * dy <= d.r * d.r) {
          size_t o = ((size_t)py * imgW + px) * 3;
          img.pixels[o + 0] = 255; img.pixels[o + 1] = 255; img.pixels[o + 2] = 255;
        }
      }
    }
  }
  return img;
}

// The closed-form NDC the operator emits for a pixel center (cs:394-395): ((cx/W-0.5)*2, (cy/H-0.5)*-2).
void expectedNdc(float cx, float cy, int W, int H, float& nx, float& ny) {
  nx = (cx / W - 0.5f) * 2.0f;
  ny = (cy / H - 0.5f) * -2.0f;
}
}  // namespace

int runIoVideoPointScanSelfTest(bool injectBug) {
  using namespace sw::cv_bridge;
  bool ok = true;
  const int W = 640, H = 480;
  const float threshold = 0.5f;  // cs:380 → binary at 0.5*255 = 127

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G1 + G2. Three disks at KNOWN, well-separated centers (all off-center so the *2 span and the
  //          Y-flip both bite — a probe at the exact center would be NDC (0,0) and hide the tooth).
  std::vector<Disk> disks = {
      {160.f, 120.f, 12.f},  // upper-left quadrant → NDC (-0.5, +0.5)
      {480.f, 360.f, 12.f},  // lower-right quadrant → NDC (+0.5, -0.5)
      {320.f, 120.f, 12.f},  // top-center → NDC (0.0, +0.5)  (x centered, y NOT → Y-flip still tested)
  };
  HostImage frame = renderDisks(W, H, disks);

  setCvBug(injectBug ? 2 : 0);  // TEETH: NDC mapping corruption
  std::vector<BrightSpot> spots = findBrightSpots(frame, /*findAll=*/true, threshold);
  setCvBug(0);

  ok = ok && spots.size() == disks.size();
  if (spots.size() == disks.size()) {
    // Match each recovered spot to its nearest known disk center by PIXEL centroid (order-independent).
    // Pixel centroid is bug-invariant (the tooth only touches NDC), so matching stays correct even in
    // the bug run; then we assert the NDC against the closed-form value → the tooth shows up there.
    for (const auto& d : disks) {
      const BrightSpot* best = nullptr;
      double bestDist = 1e18;
      for (const auto& s : spots) {
        double dd = (s.pixelX - d.cx) * (s.pixelX - d.cx) + (s.pixelY - d.cy) * (s.pixelY - d.cy);
        if (dd < bestDist) { bestDist = dd; best = &s; }
      }
      if (!best) { ok = false; continue; }
      // Closed form: symmetric disk centroid == its center (within a sub-pixel band from rasterization).
      ok = ok && approx(best->pixelX, d.cx, 1.0) && approx(best->pixelY, d.cy, 1.0);
      float enx, eny;
      expectedNdc(d.cx, d.cy, W, H, enx, eny);
      ok = ok && approx(best->ndcX, enx, 0.02) && approx(best->ndcY, eny, 0.02);
    }
  }

  // ─────────────────────────────────────────────────────────────────────────────────────────────
  // G3 (findAll=false, cs:401-414). Two disks of different radii; the largest-blob path must return
  //     exactly one spot at the BIGGER disk's center. Bug run leaves this leg's pixel selection intact
  //     (radius ordering is bug-invariant) but its NDC diverges — so G3 also carries a tooth.
  std::vector<Disk> two = {
      {200.f, 300.f, 8.f},    // smaller
      {450.f, 150.f, 18.f},   // LARGER → must win
  };
  HostImage frame2 = renderDisks(W, H, two);
  setCvBug(injectBug ? 2 : 0);
  std::vector<BrightSpot> largest = findBrightSpots(frame2, /*findAll=*/false, threshold);
  setCvBug(0);
  ok = ok && largest.size() == 1u;
  if (largest.size() == 1u) {
    ok = ok && approx(largest[0].pixelX, 450.0, 1.0) && approx(largest[0].pixelY, 150.0, 1.0);
    float enx, eny;
    expectedNdc(450.f, 150.f, W, H, enx, eny);  // NDC (+0.40625, +0.375)
    ok = ok && approx(largest[0].ndcX, enx, 0.02) && approx(largest[0].ndcY, eny, 0.02);
  }

  // ── verdict ────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-video-pointscan] injectBug did NOT trip (the NDC-mapping tooth is "
                  "dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-video-pointscan] injectBug correctly RED (corrupted device-normalize "
                "mapping)\n");
    return 1;
  }
  std::printf("[selftest-io-video-pointscan] found %zu spots %s\n", spots.size(),
              ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-video-pointscan (order 716, free slot after io-camera-calibrate at 715).
REGISTER_SELFTESTS(/*orderBase=*/716, {"io-video-pointscan", sw::runIoVideoPointScanSelfTest});
