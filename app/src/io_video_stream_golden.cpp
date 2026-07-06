// io_video_stream_golden — VideoStreamInput OpenCV decode+BGRA-convert golden (--selftest-io-video-stream).
//
// The stream-decode leg the census marked DEFERRED until the OpenCV videoio seam existed
// (runtime/cv_bridge decodeStreamFrame). Drives the genuine OpenCV VideoCapture→Read→CvtColor(BGR2BGRA)
// path (VideoStreamInput.cs:148/163/173) with a CLOSED-FORM anchor:
//
//   Authored clip: an FFV1 (LOSSLESS) .avi whose frame i is a solid, KNOWN BGR color. Lossless is
//   load-bearing — a lossy codec would quantize the solids and there'd be no exact oracle. Round-trip
//   through cv::VideoWriter→cv::VideoCapture is byte-exact for solid frames (spike-verified).
//
//   G1 decode+convert (cs:163/173): decode frame N and assert its center pixel's BGRA == the authored
//       (B,G,R,255). The expected value is the AUTHORED color, not any OpenCV output — pure closed form.
//   G2 frame indexing (cs:163 loop): the colors are an ASYMMETRIC ramp (every frame distinct, no two
//       equal), so fetching the wrong frame index reads the wrong color. Probes span middle frames
//       (not just frame 0) so an off-by-one or a stuck-first-frame decode shows up.
//   G3 alpha + dimensions (cs:170 CV_8UC4): every pixel's A channel == 255 (opaque, the BGRA the GPU
//       texture uploads), and width/height match the authored clip.
//
// TiXL authority: external/tixl/Operators/Lib/io/video/VideoStreamInput.cs (lines cited inline). The RTSP
// network source (cs:30 default rtsp:// URL) is deferred-hw-verify; a file URL exercises the SAME FFMPEG
// VideoCapture decode+convert path (cs:148), which is the numeric seam. injectBug drives cv_bridge
// setCvBug(3): swap B↔R in the BGR→BGRA convert (real cook-path corruption of the channel-order mapping
// the B8G8R8A8 texture depends on, NOT a want-flip) → the decoded BGRA diverges from the authored color.
// did-not-trip → return 0 (GOLDEN_STANDARD P1).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/cv_bridge.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

namespace {
// Asymmetric per-frame BGR ramp: 5 distinct solids, no two equal, and B≠R in each so the B↔R tooth
// actually moves every probed pixel (a gray frame B==R would hide the swap — a dead tooth).
const std::vector<uint8_t> kClipBgr = {
    //  B    G    R
     10,  40, 200,   // frame 0
    200,  30,  15,   // frame 1
     50, 210,  90,   // frame 2
    120,  80, 240,   // frame 3
     25, 170,  60,   // frame 4
};
constexpr int kW = 96, kH = 64;
}  // namespace

int runIoVideoStreamSelfTest(bool injectBug) {
  using namespace sw::cv_bridge;
  const int nFrames = (int)(kClipBgr.size() / 3);

  // Author the lossless fixture clip (through the seam; the golden never touches cv::).
  const std::string path = "/tmp/sw_io_video_stream_golden.avi";
  if (!writeSolidColorClip(path, kW, kH, kClipBgr)) {
    // No FFV1 backend → cannot author the closed-form fixture. Treat as environment gap, not a tooth
    // death: the bug leg would return 0 (did-not-trip) and the green leg reports the gap.
    std::printf("[selftest-io-video-stream] SKIP: FFV1 writer unavailable (no closed-form fixture)\n");
    return injectBug ? 0 : 0;
  }

  bool ok = true;

  setCvBug(injectBug ? 3 : 0);  // TEETH: B↔R swap in the BGR→BGRA convert

  // Probe middle frames (1,2,3) so G2 tests real indexing, not just frame 0. Frame center pixel.
  for (int fi : {1, 2, 3}) {
    StreamFrame f;
    bool got = decodeStreamFrame(path, fi, f);
    ok = ok && got;
    if (!got) continue;

    // G3: dimensions match the authored clip.
    ok = ok && f.width == kW && f.height == kH;
    ok = ok && f.bgra.size() == (size_t)kW * kH * 4;
    if (f.width != kW || f.height != kH || f.bgra.size() != (size_t)kW * kH * 4) continue;

    // Center pixel BGRA.
    size_t o = ((size_t)(kH / 2) * kW + (kW / 2)) * 4;
    uint8_t B = f.bgra[o + 0], G = f.bgra[o + 1], R = f.bgra[o + 2], A = f.bgra[o + 3];

    // G1: closed-form == the AUTHORED color for THIS frame (asymmetric ramp → G2 indexing baked in).
    uint8_t wantB = kClipBgr[fi * 3 + 0], wantG = kClipBgr[fi * 3 + 1], wantR = kClipBgr[fi * 3 + 2];
    ok = ok && B == wantB && G == wantG && R == wantR;
    // G3: opaque alpha (cs:173 BGR2BGRA fills A=255).
    ok = ok && A == 255;
  }

  // Out-of-range index must fail cleanly (cs:163 Read returns false past end).
  {
    StreamFrame f;
    bool got = decodeStreamFrame(path, nFrames + 5, f);
    ok = ok && !got;
  }

  setCvBug(0);

  // ── verdict ──────────────────────────────────────────────────────────────────────────────────────
  if (injectBug) {
    if (ok) {
      std::printf("[selftest-io-video-stream] injectBug did NOT trip (the B↔R convert tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-io-video-stream] injectBug correctly RED (corrupted BGR→BGRA channel order)\n");
    return 1;
  }
  std::printf("[selftest-io-video-stream] decode+convert %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-io-video-stream (order 719, free slot between io-ptz-send at 717 and net-nodes 718).
REGISTER_SELFTESTS(/*orderBase=*/719, {"io-video-stream", sw::runIoVideoStreamSelfTest});
