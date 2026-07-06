// platform/video_capture — native macOS live-camera capture (the CAPTURE leg of VideoDeviceInput).
// Start an AVCaptureSession on the default video device (the built-in FaceTime camera / any connected
// UVC cam), deliver each captured frame's dimensions + a decoded RGBA/BGRA buffer to a callback on the
// capture thread. The frame-transform math (rotate/scale/reposition about the center) is already ported
// and golden'd in runtime/video_device_transform.h (--selftest-io-video-transform); this seam is the
// live-pixel-producing half it warps.
//
// ZONE: platform (native macOS interface). AVFoundation AVCaptureSession + AVCaptureVideoDataOutput
// (+CoreVideo CVPixelBuffer) — the macOS replacement for TiXL's DirectShow enumeration + OpenCV
// VideoCapture (VideoDeviceInput.cs is device-bound DirectShow/OpenCV, Windows-only; there is no
// portable clone). platform leaf: ObjC behind a C++ pimpl — callers include no ObjC, no runtime/app/ui.
//
// PARITY (named, honest): device capture is inherently non-deterministic (a live feed) — there is no
// closed-form frame to assert. The self-test proves the CAPTURE PATH is live: session starts, at least
// one frame arrives, its dimensions are non-zero. Frame CONTENT is deferred-content-verify (a real cam
// points at whatever is in front of it). No camera / no permission → graceful SKIP (not a false green).
//
// Compiled WITH ARC (AVFoundation, like audio_capture / video_decode).
#pragma once

#include <cstdint>

namespace sw {

class VideoCapture {
 public:
  VideoCapture();
  ~VideoCapture();
  VideoCapture(const VideoCapture&) = delete;
  VideoCapture& operator=(const VideoCapture&) = delete;

  // Per-frame callback on the CAPTURE THREAD. `bgra` points at tightly-packed BGRA8 bytes (stride =
  // width*4) valid only for the duration of the call (copy if retaining). Mirrors VideoDeviceInput's
  // per-frame arrival (VideoDeviceInput.cs capture thread) minus the WarpAffine (that is the golden'd
  // transform core, applied downstream). user = the opaque pointer passed to start().
  using FrameCallback = void (*)(void* user, const uint8_t* bgra, uint32_t width, uint32_t height);

  // Availability WITHOUT starting: is a default video device present at all? Distinguishes "no camera
  // on this machine" (SKIP) from "camera present but a frame never came" (a real failure). Does not
  // touch the permission prompt.
  static bool hasDefaultDevice();

  // Start the default-device capture session and deliver frames to `cb`. Returns false when no device
  // exists OR capture authorization is denied/restricted (the caller then reports SKIP, not FAIL) OR
  // the session could not be configured. On success frames begin arriving on the capture thread until
  // stop(). Non-fatal on every failure (no crash).
  bool start(FrameCallback cb, void* user);
  void stop();
  bool isRunning() const;

  // Authorization status for video capture (AVCaptureDevice authorizationStatusForMediaType). 0=not
  // determined, 1=restricted, 2=denied, 3=authorized — surfaced so the self-test can print SKIP with a
  // reason instead of a bare FAIL when the OS blocks the camera.
  static int authorizationStatus();

 private:
  struct Impl;
  Impl* impl_;
};

// ── Closed-form frame-packing core (the deterministic half the delegate runs on every frame). Copies a
// source BGRA buffer whose rows may carry alignment PADDING (srcStride ≥ width*4) into a tightly-packed
// width*4 destination — the exact repack the capture delegate does before handing bytes to the callback.
// Factored out so it verifies WITHOUT a camera (the live capture path is emergent/deferred-content; this
// stride math is closed form). Returns the packed byte count (= width*height*4), 0 on bad args.
// dst must hold width*height*4 bytes. TEETH: setVideoCapturePackBug(1) drops the per-row stride step so
// padded frames smear — a real corruption of the repack, biteable on a synthetic padded frame.
void setVideoCapturePackBug(int mode);
int  videoCapturePackBug();
uint32_t packBgraFrame(uint8_t* dst, const uint8_t* src, uint32_t width, uint32_t height,
                       uint32_t srcStride);

// --selftest-io-video-capture entry. TWO halves:
//   (1) CLOSED-FORM tooth (always runs, no hardware): feed a SYNTHETIC padded BGRA frame through
//       packBgraFrame and assert the tightly-packed output pixels + dimensions are exact. injectBug
//       (setVideoCapturePackBug) drops the row-stride step so the padded repack smears → RED. This is
//       the biteable half — the stride/pack math is the deterministic part of the delegate's per-frame
//       work, verifiable without a camera (the audiomonitor-synthetic-input precedent).
//   (2) LIVE liveness probe (opportunistic, emergent): if a device is present AND authorized, start the
//       session, wait ≤3s for a frame, assert dims>0 (content = deferred-content-verify). No device / no
//       permission → prints SKIP for THIS half only (not a false green; the closed-form half already
//       ran). A device present+authorized but no frame in 3s is a real FAIL.
// fn(bool injectBug) -> process exit code (0 PASS, 1 FAIL; did-not-trip under injectBug → 0 for the
// NO-BITE surfacing contract).
int runVideoCaptureSelfTest(bool injectBug);

}  // namespace sw
