// runtime/node_registry_math_audioplayback — self-registering NodeSpec leaf for the io/audio PLAYBACK
// operator family (AudioPlayer, PlayAudioClip). Split from node_registry_math_anim.cpp (ARCHITECTURE rule
// 4, ≤400 lines) — the anim leaf hit its cap when these two joined it. Both are STATEFUL, externally-cooked
// nodes (evaluate==nullptr), cooked by frame_cook's cookAudioPlaybackNodes (runtime/audio_playback_cook),
// exactly like AudioReaction. Adding a playback op here = drop a MathOp registrar (the globbed math-leaf
// self-registration sink; the central manifest is never touched).
#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget
#include "runtime/math_op_registry.h"  // MathOp / mathSpecSink

namespace sw {
namespace {

      // TiXL AudioPlayer (Operators/Lib/io/audio/AudioPlayer.cs) — per-operator trigger-play stream with
      // ADSR-gated volume + play/stop/pause edges + seek. STATEFUL (per-instance edge memory + envelope
      // stage machine across frames) → evaluate==nullptr; cooked by frame_cook's cookAudioPlaybackNodes
      // (runtime/audio_playback_cook), which emits stream commands to the AudioPlaybackBus (app drains to
      // platform/audio_mixer) and writes IsPlaying/GetLevel → extOut. Mirror of AudioReaction's no-evaluate
      // extOut readback. OUTPUTS FIRST → extOut idx: [0]=IsPlaying, [1]=GetLevel (Result Command has no
      // value rail — the audible product is the mixer side-effect). Params = TiXL InputSlots.
      // .t3 DEFAULTS (AudioPlayer.t3, re-read): AudioFile="", PlayAudio/StopAudio/PauseAudio=false, Mute=false,
      // UseEnvelope=false, TriggerMode=0(Gate), Seek=0, Volume=1, Speed=1, Panning=0, Duration=1,
      // Envelope=(0.1,0.1,1.0,0.1) = A/D/S/R. FORK fork-envelope-vec4-as-4-floats: TiXL's Vector4 Envelope
      // decomposes into 4 consecutive Float ports (the established scalar-pack fork). TriggerMode enum =
      // AdsrCalculator.TriggerMode {Gate,Trigger}.
static const MathOp _reg_AudioPlayer{
      {"AudioPlayer", "AudioPlayer",
       {{"IsPlaying", "IsPlaying", "Float", false},
        {"GetLevel", "GetLevel", "Float", false},
        {"AudioFile", "AudioFile", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true, 1, false, ""},
        {"PlayAudio", "PlayAudio", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"StopAudio", "StopAudio", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"PauseAudio", "PauseAudio", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Volume", "Volume", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"Mute", "Mute", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Panning", "Panning", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider, {}, true},
        {"Speed", "Speed", "Float", true, 1.0f, 0.25f, 4.0f, Widget::Slider, {}, true},
        {"Seek", "Seek", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true},
        {"TriggerMode", "TriggerMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Gate", "Trigger"}, true},
        {"Duration", "Duration", "Float", true, 1.0f, 0.0f, 60.0f, Widget::Slider, {}, true},
        {"UseEnvelope", "UseEnvelope", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"Envelope.x", "Envelope", "Float", true, 0.1f, 0.0f, 10.0f, Widget::Vec, {}, true, 4},
        {"Envelope.y", "Envelope.y", "Float", true, 0.1f, 0.0f, 10.0f, Widget::Vec, {}, true, 1},
        {"Envelope.z", "Envelope.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Envelope.w", "Envelope.w", "Float", true, 0.1f, 0.0f, 10.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "io.audio"}
};

      // TiXL PlayAudioClip (Operators/Lib/io/video/PlayAudioClip.cs) — time-driven soundtrack-clip scrub.
      // STATEFUL (RunTimeInSecs anchor across frames) → evaluate==nullptr; cooked by cookAudioPlaybackNodes,
      // which emits Ensure+Seek(targetTime)+SetGain(Volume)+Play/Stop and echoes targetTime → extOut[0]
      // (Result is a Command; targetTime is the golden probe). .t3 DEFAULTS (PlayAudioClip.t3, re-read):
      // TimeInSecs=0, IsPlaying=true, IsLooping=false, Volume=1, Path="Examples:...rp-katsumaki-v3.mp3".
      // Path default kept EMPTY here (sw asset paths are project-relative; the TiXL Examples: path is a
      // Windows-example artifact not shipped — NAMED FORK fork-playaudioclip-default-path-empty, avoids a
      // dead default reference). Output FIRST → extOut[0] = targetTime.
static const MathOp _reg_PlayAudioClip{
      {"PlayAudioClip", "PlayAudioClip",
       {{"TargetTime", "TargetTime", "Float", false},
        {"Path", "Path", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, true, 1, false, ""},
        {"TimeInSecs", "TimeInSecs", "Float", true, 0.0f, -1000.0f, 1000.0f, Widget::Slider, {}, true},
        {"Volume", "Volume", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Slider, {}, true},
        {"IsLooping", "IsLooping", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
        {"IsPlaying", "IsPlaying", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}},
       nullptr,
       "io.video"}
};

}  // namespace
}  // namespace sw
