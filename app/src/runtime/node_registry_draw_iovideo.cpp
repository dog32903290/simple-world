// runtime/node_registry_draw_iovideo — NodeSpec rows for the io/video + io/ptz + io/dmx vision ops:
// PlayVideo / PlayVideoClip / VideoDeviceInput / VideoStreamInput / ViscaCamera / OnvifCamera /
// CameraCalibrator / Video2DPointScanner. Peeled into its own family leaf (parallel-lane peel) so this
// node-hang lane never touches a shared table.
//
// WHY these hang now (the PARTIAL → census-recognised completion): each op's device leg is now built and
// self-tested — PlayVideo/PlayVideoClip's frame decode (platform/video_decode, --selftest-io-video-decode)
// on top of the golden'd timing core (runtime/video_playback.h, --selftest-io-video-timing);
// VideoDeviceInput's live capture (platform/video_capture, --selftest-io-video-capture) on top of the
// golden'd affine transform (runtime/video_device_transform.h, --selftest-io-video-transform);
// ViscaCamera/OnvifCamera's send leg (io_ptz_send_golden, --selftest-io-ptz-send) on top of the golden'd
// VISCA byte protocol / ONVIF digest (visca_protocol.h / onvif_digest.h). What was missing to make census
// recognise the OPERATOR (not just its parts) was the GRAPH NODE. These rows register it — each outputs a
// Texture2D (the image-op form: PlayVideo.cs / VideoDeviceInput.cs / ViscaCamera.cs all Slot<Texture2D>).
//
// evaluate == nullptr: the node is a texture/command SOURCE — its output rides the Texture2D rail, not the
// float `evaluate` value rail (like RenderTarget, node_registry_draw_render.cpp:182, whose Texture2D
// output also carries evaluate=nullptr). The cook that fills the texture from the device leg lands when
// the io/video texture-source cook seam is wired (the platform decode/capture functions already exist);
// this row makes the node draggable/wireable/census-recognised now, the texture-fill activates on that
// seam without re-registration — the same "hang now, fill later" posture the pbr value-slice nodes use.
//
// Ports mirror each op's .cs InputSlots + .t3 DefaultValues (line-cited in comments). Bool inputs ride a
// Widget::Bool Float port (>0.5); String inputs ride the String channel (strDef). FORKS (named): the
// device-settings-only inputs that have no cross-platform meaning (OpenSettings/Reconnect/
// DeactivateWhenNotShowing/ResolutionFpsType — DirectShow/OpenCV device-dialog affordances) and the
// non-Texture secondary outputs (Duration/HasCompleted/UpdateCount/Resolution/IsConnected/CurrentPtz) are
// dropped for the first hang — the Texture2D output is the load-bearing one; secondary value outputs join
// when the cross-rail latch for io-video is wired (GetPosition precedent).
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawIoVideoSpecs() {
  static const std::vector<NodeSpec> specs = {
      // PlayVideo (TiXL Lib.io.video.PlayVideo): decode a video file to a Texture2D at the transport
      // time, seek/loop per the golden'd timing core. Texture2D out. Params mirror PlayVideo.t3:
      // Loop=true, Volume=1.0, ResyncThreshold=0.2, OverrideTimeInSecs=0.0, IsPreciseAtPlayback=false,
      // Path="Examples:videos/spray-1080p.mp4" (String). The Duration/HasCompleted/UpdateCount value
      // outputs are the deferred cross-rail latch half.
      {"PlayVideo", "PlayVideo",
       {{"out", "out", "Texture2D", false},
        {"Path", "Path", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
         "Examples:videos/spray-1080p.mp4"},
        {"Loop", "Loop", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Volume", "Volume", "Float", true, 1.0f, 0.0f, 1.0f},
        {"ResyncThreshold", "ResyncThreshold", "Float", true, 0.2f, 0.0f, 2.0f},
        {"OverrideTimeInSecs", "OverrideTimeInSecs", "Float", true, 0.0f, -10000.0f, 10000.0f},
        {"IsPreciseAtPlayback", "IsPreciseAtPlayback", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool,
         {}, true}},
       nullptr,
       "io.video"},
      // PlayVideoClip (TiXL Lib.io.video.PlayVideoClip): the timeline-TimeClip video player — maps a
      // source range across a timeline range (golden'd timing core). Command SubTree in (the timeclip
      // wrapper), Texture2D out. Params mirror PlayVideoClip.t3: Volume=1.0, ResyncThreshold=1.0,
      // Path="Examples:videos/spray-1080p.mp4".
      {"PlayVideoClip", "PlayVideoClip",
       {{"Command", "Command", "Command", true},
        {"out", "out", "Texture2D", false},
        {"Path", "Path", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
         "Examples:videos/spray-1080p.mp4"},
        {"Volume", "Volume", "Float", true, 1.0f, 0.0f, 1.0f},
        {"ResyncThreshold", "ResyncThreshold", "Float", true, 1.0f, 0.0f, 2.0f}},
       nullptr,
       "io.video"},
      // VideoDeviceInput (TiXL Lib.io.video.VideoDeviceInput): live camera capture → Texture2D, with the
      // golden'd center-pivot affine (rotate/scale/reposition). Texture2D out. Params mirror
      // VideoDeviceInput.t3: Active=false, ApplyRotationData=0.0, CustomFps=0, FlipH/V=false,
      // Scale=(1,1), Reposition=(0,0), InputDeviceName="" (String). FORK (named): OpenSettings/Reconnect/
      // DeactivateWhenNotShowing/ResolutionFpsType/CustomResolution are DirectShow/OpenCV device-dialog
      // affordances dropped for the first hang (no cross-platform meaning on AVCaptureSession).
      {"VideoDeviceInput", "VideoDeviceInput",
       {{"out", "out", "Texture2D", false},
        {"Active", "Active", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"InputDeviceName", "InputDeviceName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
         false, 1, false, ""},
        {"ApplyRotationData", "ApplyRotationData", "Float", true, 0.0f, -360.0f, 360.0f},
        {"Scale.x", "Scale", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, false, 2},
        {"Scale.y", "Scale.y", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Vec, {}, false, 1},
        {"Reposition.x", "Reposition", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 2},
        {"Reposition.y", "Reposition.y", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, false, 1},
        {"FlipHorizontally", "FlipHorizontally", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"FlipVertically", "FlipVertically", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"CustomFps", "CustomFps", "Float", true, 0.0f, 0.0f, 240.0f}},
       nullptr,
       "io.video"},
      // VideoStreamInput (TiXL Lib.io.video.VideoStreamInput): decode a live RTSP/http video stream (or a
      // file URL) to a Texture2D via OpenCV VideoCapture(FFMPEG) → Read → CvtColor(BGR2BGRA) (cs:148/163/
      // 173, golden'd decode+convert in runtime/cv_bridge, --selftest-io-video-stream). Texture2D out.
      // Params mirror VideoStreamInput.cs InputSlots + .t3 DefaultValues: Connect=false (cs:15/t3:6),
      // Url="rtsp://your_stream_url" (cs:30 — .t3:14 overrides to a demo mjpg URL; the .cs source default
      // is the honest clone anchor). FORK (named): Reconnect (cs:18) is a device-dialog re-open affordance
      // dropped for the first hang; the Resolution/Status value outputs (cs:24/27) are the deferred
      // cross-rail latch half (secondary value outputs join when io-video's cross-rail latch is wired,
      // GetPosition precedent). The live RTSP network fetch is deferred-hw-verify.
      {"VideoStreamInput", "VideoStreamInput",
       {{"out", "out", "Texture2D", false},
        {"Url", "Url", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
         "rtsp://your_stream_url"},
        {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "io.video"},
      // ViscaCamera (TiXL Lib.io.ptz.ViscaCamera): PTZ camera control (VISCA-over-IP, golden'd byte
      // protocol + send leg) + optional passthrough of an input texture. TextureInput in, Texture2D out.
      // Params mirror ViscaCamera inputs: Address(String), Port(int), Connect/Move(bool), Pan/Tilt/Zoom
      // (float, normalized), PanRange/TiltRange(int). The IsConnected/CurrentPtz value outputs are the
      // deferred cross-rail latch half; the real PTZ move is deferred-hw-verify (the send leg is proven
      // over UDP loopback in --selftest-io-ptz-send).
      {"ViscaCamera", "ViscaCamera",
       {{"TextureInput", "TextureInput", "Texture2D", true},
        {"out", "out", "Texture2D", false},
        {"Address", "Address", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
        {"Port", "Port", "Float", true, 52381.0f, 0.0f, 65535.0f},
        {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Pan", "Pan", "Float", true, 0.0f, -1.0f, 1.0f},
        {"Tilt", "Tilt", "Float", true, 0.0f, -1.0f, 1.0f},
        {"Zoom", "Zoom", "Float", true, 0.0f, 0.0f, 1.0f},
        {"Move", "Move", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"PanRange", "PanRange", "Float", true, 2448.0f, 0.0f, 65535.0f},
        {"TiltRange", "TiltRange", "Float", true, 1296.0f, 0.0f, 65535.0f}},
       nullptr,
       "io.ptz"},
      // OnvifCamera (TiXL Lib.io.ptz.OnvifCamera): ONVIF PTZ camera (SOAP/WS-Security, golden'd digest +
      // envelope assembly) — RTSP stream out as Texture2D. Texture2D out. Params mirror OnvifCamera
      // inputs: Address/Username/Password(String), Discover/Connect/Move(bool), Pan/Tilt/Zoom(float). The
      // IsConnected/CurrentPtz value outputs are the deferred cross-rail latch half; the real SOAP POST +
      // RTSP decode is deferred-hw-verify (the SOAP request assembly is proven in --selftest-io-ptz-send).
      {"OnvifCamera", "OnvifCamera",
       {{"out", "out", "Texture2D", false},
        {"Address", "Address", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
        {"Username", "Username", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
        {"Password", "Password", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
        {"Discover", "Discover", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Connect", "Connect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Pan", "Pan", "Float", true, 0.0f, -1.0f, 1.0f},
        {"Tilt", "Tilt", "Float", true, 0.0f, -1.0f, 1.0f},
        {"Zoom", "Zoom", "Float", true, 0.0f, 0.0f, 1.0f},
        {"Move", "Move", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "io.ptz"},
      // TiXL authority: CameraCalibrator.cs
      // CameraCalibrator (TiXL Lib.io.video.CameraCalibrator): chessboard camera calibration — OpenCV
      // findChessboardCorners + calibrateCamera(RationalModel, 50/1e-4) → intrinsics K + distortion, then
      // Undistort/Remap the feed. The genuine OpenCV numerics are golden'd in runtime/cv_bridge
      // (findChessboardCorners cs:270, calibrateCameraViews cs:309-319; --selftest-io-camera-calibrate);
      // the chessboard display geometry is golden'd in runtime/checkerboard_gen (--selftest-io-camera-
      // checkerboard). This row registers the GRAPH NODE so census recognises the OPERATOR. TextureIn in,
      // TextureOut (undistorted feed) + CheckerboardTexture (the display board) out — both Texture2D. The
      // cook that fills the textures from the device leg (DX11/Metal staging readback → cv::Mat) is
      // deferred-hw (ConvertTextureToMat, the hardware leg); the numeric calibrate cook rides the golden'd
      // cv_bridge once the texture-source cook seam is wired, no re-registration ("hang now, fill later").
      // Ports mirror CameraCalibrator.cs InputSlots (line-cited); Int2 ChessboardSize/DisplayResolution
      // ride Widget::Vec arity-2 (two consecutive Float ports .x/.y); bool inputs ride Widget::Bool; enum
      // Mode/UndistortMethod ride Widget::Enum. FORK (named): the IsCalibrated/StatusMessage value outputs
      // (cs:55/73) are the deferred cross-rail latch half (secondary value outputs join when io-video's
      // cross-rail latch is wired, GetPosition precedent).
      {"CameraCalibrator", "CameraCalibrator",
       {{"TextureIn", "TextureIn", "Texture2D", true},
        {"TextureOut", "TextureOut", "Texture2D", false},
        {"CheckerboardTexture", "CheckerboardTexture", "Texture2D", false},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"Passthrough", "Calibration", "LensCorrected"}, true},
        {"ChessboardSize.x", "ChessboardSize", "Float", true, 16.0f, 1.0f, 32.0f, Widget::Vec, {}, false, 2},
        {"ChessboardSize.y", "ChessboardSize.y", "Float", true, 9.0f, 1.0f, 32.0f, Widget::Vec, {}, false, 1},
        {"SquareInMm", "SquareInMm", "Float", true, 25.0f, 0.0f, 1000.0f},
        {"BorderInSquares", "BorderInSquares", "Float", true, 1.0f, 0.0f, 16.0f},
        {"DisplayResolution.x", "DisplayResolution", "Float", true, 1920.0f, 1.0f, 16384.0f, Widget::Vec, {}, false, 2},
        {"DisplayResolution.y", "DisplayResolution.y", "Float", true, 1080.0f, 1.0f, 16384.0f, Widget::Vec, {}, false, 1},
        {"Alpha", "Alpha", "Float", true, 1.0f, 0.0f, 1.0f},
        {"UndistortMethod", "UndistortMethod", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Undistort", "Remap"}, true},
        {"CaptureImage", "CaptureImage", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Calibrate", "Calibrate", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Reset", "Reset", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"FilePath", "FilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
         "calibration.dat"},
        {"Save", "Save", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Load", "Load", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "io.video"},
      // TiXL authority: Video2DPointScanner.cs
      // Video2DPointScanner (TiXL Lib.io.dmx.helpers.Video2DPointScanner): scan a camera feed for bright
      // spots → 2D point map (projector/LED calibration, structured-light). The genuine OpenCV blob detect
      // is golden'd in runtime/cv_bridge (FindBrightSpots cs:374-418: grayscale→threshold→findContours→
      // area>2 centroid → device-normalize [-1,1]; --selftest-io-video-pointscan). This row registers the
      // GRAPH NODE. VideoIn (Texture2D) in; ScannedPoints2D → the Points currency (TiXL BufferWithViews of
      // 2D points, cs:59 — the point-list rail our detected-spot centroids ride, cs:394-395 NDC mapping);
      // DebugTexture (Texture2D) out. The cook filling Points from a real feed rides the golden'd cv_bridge
      // once the texture-source readback cook seam is wired (deferred-hw, same as CameraCalibrator's device
      // leg), no re-registration. Ports mirror Video2DPointScanner.cs InputSlots (line-cited); bool inputs
      // ride Widget::Bool; String inputs ride the String channel. FORK (named): PixelOutput (cs:47, the
      // structured-light scan buffer, a second BufferWithViews) is dropped for the first hang — ScannedPoints2D
      // is the load-bearing point-list output; the DebugMode/TestFullMode/TestPixelMode device-scan
      // affordances hang as bool params but their scan-sequence behaviour is deferred-hw (needs the live
      // output→camera feedback loop).
      {"Video2DPointScanner", "Video2DPointScanner",
       {{"VideoIn", "VideoIn", "Texture2D", true},
        {"ScannedPoints2D", "ScannedPoints2D", "Points", false},
        {"DebugTexture", "DebugTexture", "Texture2D", false},
        {"Threshold", "Threshold", "Float", true, 0.0f, 0.0f, 1.0f},
        {"PixelBrightness", "PixelBrightness", "Float", true, 0.0f, 0.0f, 1.0f},
        {"PixelCount", "PixelCount", "Float", true, 0.0f, 0.0f, 65535.0f},
        {"ScanIntervallum", "ScanIntervallum", "Float", true, 0.0f, 0.0f, 60.0f},
        {"ScanTrigger", "ScanTrigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"ResetScan", "ResetScan", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"ApplyCorrection", "ApplyCorrection", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"LoadCalibration", "LoadCalibration", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"CalibrationPath", "CalibrationPath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
         1, false, ""},
        {"DebugMode", "DebugMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"TestFullMode", "TestFullMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"TestPixelMode", "TestPixelMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"FilePath", "FilePath", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
        {"Save", "Save", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Load", "Load", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "io.dmx"},
  };
  return specs;
}

}  // namespace sw
