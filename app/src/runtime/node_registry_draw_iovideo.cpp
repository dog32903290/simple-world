// runtime/node_registry_draw_iovideo — NodeSpec rows for the io/video + io/ptz Texture2D-producing ops:
// PlayVideo / PlayVideoClip / VideoDeviceInput / ViscaCamera / OnvifCamera. Peeled into its own family
// leaf (parallel-lane peel) so this node-hang lane never touches a shared table.
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
  };
  return specs;
}

}  // namespace sw
