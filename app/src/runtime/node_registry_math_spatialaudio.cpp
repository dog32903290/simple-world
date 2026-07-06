// runtime/node_registry_math_spatialaudio — self-registering NodeSpec leaf for the io/audio
// SpatialAudioPlayer operator (TiXL Operators/Lib/io/audio/SpatialAudioPlayer.cs). Split from
// node_registry_math_audioplayback.cpp (ARCHITECTURE rule 4, ≤400 lines + one-responsibility): the
// spatial player is a distinct sub-family (3D position/orientation/cone, 20 inputs) with its own gain
// spine (runtime/spatial_audio). Adding it here = drop ONE MathOp registrar; the central manifest is
// never touched (globbed math-leaf sink).
//
// STATEFUL, externally-cooked (evaluate==nullptr): per-instance stream on the mixer + edge memory across
// frames, exactly like AudioPlayer. The audible product is the 3D sound-field side-effect (deferred-hw-
// verify: the true per-ear pan/HRTF lives in the platform AVAudioEnvironmentNode, not in this spec). The
// value-exact gain spine (linear distance attenuation + effective volume + Euler→direction) is golden'd
// as a MATH LIBRARY by runtime/spatial_audio's --selftest-spatialaudio — but NO cooker is wired to this
// spec yet, so nothing in production exercises that spine (2026-07-06 audit). The cook seam, when it
// lands, MUST call the golden'd spatial_audio functions rather than re-derive the math. OUTPUTS FIRST → extOut idx: [0]=IsPlaying, [1]=IsPaused,
// [2]=GetLevel (Result Command has no value rail). Params = TiXL InputSlots.
//
// .t3 DEFAULTS (SpatialAudioPlayer.t3, re-read): AudioFile="", PlayAudio/StopAudio/PauseAudio=false,
// Volume=1, Mute=false, Speed=1, Seek=0, SourcePosition=(0,0,0), SourceRotation=(0,0,0), MinDistance=1,
// MaxDistance=10, ListenerPosition=(0,0,0), ListenerRotation=(0,0,0), InnerConeAngle=90, OuterConeAngle=180,
// OuterConeVolume=0, Audio3DMode=0(Normal). FORK fork-vec3-as-3-floats: TiXL's Vector3 position/rotation
// inputs decompose into 3 consecutive Float ports (the established scalar-pack fork, same as AudioPlayer's
// Envelope vec4→4-floats). Audio3DMode enum = {Normal,Relative,Off} (SpatialAudioPlayer.cs:285-302).
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

static const MathOp _reg_SpatialAudioPlayer{
      {"SpatialAudioPlayer", "SpatialAudioPlayer",
       {{"IsPlaying", "IsPlaying", "Float", false},
        {"IsPaused", "IsPaused", "Float", false},
        {"GetLevel", "GetLevel", "Float", false},
        {"AudioFile", "AudioFile", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true, 1, false, ""},
        {"PlayAudio", "PlayAudio", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"StopAudio", "StopAudio", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"PauseAudio", "PauseAudio", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Volume", "Volume", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"Mute", "Mute", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Speed", "Speed", "Float", true, 1.0f, 0.25f, 4.0f, Widget::Slider, {}, true},
        {"Seek", "Seek", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        // SourcePosition (Vector3 → 3 floats). Default (0,0,0).
        {"SourcePosition.x", "SourcePosition", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, true, 3},
        {"SourcePosition.y", "SourcePosition.y", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, true, 1},
        {"SourcePosition.z", "SourcePosition.z", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, true, 1},
        // SourceRotation (Vector3 Euler degrees → 3 floats). Default (0,0,0).
        {"SourceRotation.x", "SourceRotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"SourceRotation.y", "SourceRotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"SourceRotation.z", "SourceRotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"MinDistance", "MinDistance", "Float", true, 1.0f, 0.0f, 1000.0f, Widget::Slider, {}, true},
        {"MaxDistance", "MaxDistance", "Float", true, 10.0f, 0.0f, 1000.0f, Widget::Slider, {}, true},
        // ListenerPosition (Vector3 → 3 floats). Default (0,0,0).
        {"ListenerPosition.x", "ListenerPosition", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, true, 3},
        {"ListenerPosition.y", "ListenerPosition.y", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, true, 1},
        {"ListenerPosition.z", "ListenerPosition.z", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Vec, {}, true, 1},
        // ListenerRotation (Vector3 Euler degrees → 3 floats). Default (0,0,0).
        {"ListenerRotation.x", "ListenerRotation", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"ListenerRotation.y", "ListenerRotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"ListenerRotation.z", "ListenerRotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"InnerConeAngle", "InnerConeAngle", "Float", true, 90.0f, 0.0f, 360.0f, Widget::Slider, {}, true},
        {"OuterConeAngle", "OuterConeAngle", "Float", true, 180.0f, 0.0f, 360.0f, Widget::Slider, {}, true},
        {"OuterConeVolume", "OuterConeVolume", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"Audio3DMode", "Audio3DMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
         {"Normal", "Relative", "Off"}, true}},
       nullptr,
       "io.audio"}
};

}  // namespace
}  // namespace sw
