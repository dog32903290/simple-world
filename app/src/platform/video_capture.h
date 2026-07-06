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

// --selftest-io-video-capture entry. Starts the default-device session, waits up to a few seconds for
// one frame, and asserts width>0 && height>0 (frame CONTENT deferred-content-verify). Prints SKIP (and
// returns 0) when no camera is present or authorization is not granted — a graceful non-green skip, NOT
// a false pass. There is no injectBug tooth: a live feed has no closed-form frame to corrupt, so the
// test is a SMOKE-level liveness check (honestly labelled, per GOLDEN_STANDARD's stateful/emergent
// carve-out) — injectBug is accepted and ignored. fn(bool) -> process exit code (0 PASS/SKIP, 1 FAIL).
int runVideoCaptureSelfTest(bool injectBug);

}  // namespace sw
