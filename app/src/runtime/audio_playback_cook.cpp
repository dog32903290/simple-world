// runtime/audio_playback_cook — per-frame cook for AudioPlayer / PlayAudioClip. See the header.
//
// TiXL authority: Operators/Lib/io/audio/AudioPlayer.cs, Operators/Lib/io/video/PlayAudioClip.cs,
// Core/Audio/AdsrCalculator.cs (frame-based Update).
#include "runtime/audio_playback_cook.h"

#include <cmath>
#include <string>

#include "runtime/audio_playback_bus.h"     // emitAudio* command seam
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / ResidentNode / resolveResidentFloatInputs

namespace sw {

namespace {
int g_audioPlaybackBug = 0;

float clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The node's stable stream key = its resident path (unique + frame-stable, like every other cook's state
// key). TiXL uses ComputeInstanceGuid(InstancePath) — a hash of the same instance path; the resident path
// is sw's equivalent stable-per-instance handle (NAMED FORK fork-audiokey-residentpath-not-guid: the value
// issued to the mixer is identical, the key is a readable path not a hashed GUID — the mixer key is opaque).
const std::string& streamKey(const ResidentNode& rn) { return rn.path; }
}  // namespace

void setAudioPlaybackBug(int mode) { g_audioPlaybackBug = mode; }
int  audioPlaybackBug() { return g_audioPlaybackBug; }

double playAudioClipTargetTime(bool connected, double connectedTime, double anchoredTime,
                               bool looping, double lengthSecs) {
  double targetTime = connected ? connectedTime : anchoredTime;   // cs:61-63
  if (looping && lengthSecs > 0.0 && targetTime > lengthSecs)      // cs:65-68
    targetTime = std::fmod(targetTime, lengthSecs);
  return targetTime;
}

// The frame-based ADSR envelope — copied VALUE-for-VALUE from stateful_value_ops_anim2.cpp:stepAdsrEnvelope
// (AdsrCalculator.cs:233-354). Kept as a standalone fn (not a call into the stateful op) because AudioPlayer
// carries its OWN state block (AudioPlayerState) and its OWN A/D/S/R fallback rules (AudioPlayer.cs:108-111
// differ slightly from AdsrEnvelope.cs:65-68 — Attack floor 0.01 not 0.01, Sustain>=0 clamp, Release 0.3).
// The a/d/s/r passed in are ALREADY the AudioPlayer.cs:108-111 values; here we apply the SetParameters floor
// (0.001) + clamp exactly as AdsrCalculator.SetParameters (cs:66-72), then run the identical stage machine.
float advanceAudioPlayerEnvelope(AudioPlayerState& st, bool gate, float timeSecs,
                                 float attackIn, float decayIn, float sustainIn, float releaseIn,
                                 int mode, float durationIn) {
  const float attack  = attackIn  > 0.001f ? attackIn  : 0.001f;
  const float decay   = decayIn   > 0.001f ? decayIn   : 0.001f;
  const float sustain = clamp01f(sustainIn);
  const float release = releaseIn > 0.001f ? releaseIn : 0.001f;
  const float FLT_MAXV = 3.4028235e38f;
  const float duration = durationIn > 0.0f ? durationIn : FLT_MAXV;

  int stage = st.stage;
  float stageTime = st.stageTime;
  float totalTime = st.totalTime;
  bool prevGate = st.adsrPrevGate;
  double releaseStart = (double)st.releaseStart;
  float value = st.envValue;
  if (!st.adsrInit) { st.lastTime = timeSecs; st.adsrInit = true; }  // fork-adsr-first-frame-lasttime (dt=0)
  const float lastTime = st.lastTime;

  const bool risingEdge = gate && !prevGate;
  const bool fallingEdge = !gate && prevGate;
  prevGate = gate;

  float deltaTime = timeSecs - lastTime;
  st.lastTime = timeSecs;
  if (deltaTime < 0.0f || deltaTime > 1.0f) deltaTime = 0.016f;

  if (mode == 0) {  // Gate mode (cs:253-268)
    if (risingEdge) { stage = 1; stageTime = 0.0f; releaseStart = value; }
    else if (fallingEdge && stage != 0) { stage = 4; stageTime = 0.0f; releaseStart = value; }
  } else {  // Trigger mode (cs:269-290)
    if (risingEdge) { stage = 1; stageTime = 0.0f; totalTime = 0.0f; releaseStart = value; }
    if (stage != 0 && stage != 4 && duration < FLT_MAXV && totalTime >= duration) {
      stage = 4; stageTime = 0.0f; releaseStart = value;
    }
  }

  stageTime += deltaTime;
  if (stage != 4 && stage != 0) totalTime += deltaTime;

  switch (stage) {
    case 0: value = 0.0f; break;
    case 1:  // Attack
      if (stageTime < attack) value = stageTime / attack;
      else { value = 1.0f; stage = 2; stageTime = 0.0f; }
      break;
    case 2:  // Decay
      if (stageTime < decay) { const float p = stageTime / decay; value = 1.0f - p * (1.0f - sustain); }
      else { value = sustain; stage = 3; stageTime = 0.0f; }
      break;
    case 3: value = sustain; break;  // Sustain
    case 4:  // Release
      if (stageTime < release) { const float p = stageTime / release; value = (float)(releaseStart * (1.0 - (double)p)); }
      else { value = 0.0f; stage = 0; stageTime = 0.0f; }
      break;
  }
  value = clamp01f(value);

  // bug 1: DROP the state write-back → the stage machine freezes (never advances past frame-1 seed) →
  //   the Attack→Decay→Sustain envelope progression golden bites. Mirrors setAdsrEnvelopeBug bug 1.
  if (g_audioPlaybackBug != 1) {
    st.stage = stage; st.stageTime = stageTime; st.totalTime = totalTime;
    st.adsrPrevGate = prevGate; st.releaseStart = (float)releaseStart; st.envValue = value;
  }
  return value;
}

// ── AudioPlayer ─────────────────────────────────────────────────────────────────────────────────────
// Faithful port of AudioPlayer.cs UpdatePlayback (cs:85-164). Inputs (AudioPlayer.cs:9-46): AudioFile(str),
// PlayAudio/StopAudio/PauseAudio(bool), Volume/Panning/Speed/Seek(float), Mute(bool), TriggerMode(int),
// Duration(float), UseEnvelope(bool), Envelope(Vec4 X=A,Y=D,Z=S,W=R). Outputs: IsPlaying(bool)→extOut[0],
// GetLevel(float)→extOut[1] (Result Command has no value rail — the SIDE EFFECT is the bus commands).
//   ADSR extract (cs:108-111): attack=X>0?X:0.01; decay=Y>0?Y:0.1; sustain=Z>=0?Clamp(Z,0,1):0.7; release=W>0?W:0.3.
//   envModVol (cs:139): useEnvelope ? volume*calc.Value : volume.
//   play/stop edge (cs:118-136): rising shouldPlay → TriggerAttack; falling → (Gate mode) TriggerRelease.
//   shouldStop (cs:92): StopAudio || !shouldPlay.
//   pause edge (cs:142-149): shouldPause transition → Pause/Resume.
// Port order (node_registry): outputs FIRST → extOut idx [0]=IsPlaying, [1]=GetLevel.
void cookAudioPlayerNodes(ResidentEvalGraph& g, float timeSecs,
                          std::map<std::string, AudioPlayerState>& state) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "AudioPlayer") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const std::string key = streamKey(rn);
    const std::string path = rn.strInputs.count("AudioFile") ? rn.strInputs.at("AudioFile") : std::string();

    const bool playAudio  = P["PlayAudio"]  != 0.0f;
    const bool stopAudio  = P["StopAudio"]  != 0.0f;
    const bool pauseAudio = P["PauseAudio"] != 0.0f;
    const float volume    = P["Volume"];
    const bool mute       = P["Mute"] != 0.0f;
    const float panning   = P["Panning"];
    const float speed     = P["Speed"];
    const float seek      = P["Seek"];
    const int triggerMode = (int)std::lround(P["TriggerMode"]);   // 0=Gate, 1=Trigger
    float duration        = P["Duration"];
    const bool useEnvelope = P["UseEnvelope"] != 0.0f;
    const float envX = P["Envelope.x"], envY = P["Envelope.y"], envZ = P["Envelope.z"], envW = P["Envelope.w"];

    AudioPlayerState& st = state[key];

    const bool shouldPlay = playAudio;
    const bool shouldStop = stopAudio || !shouldPlay;   // cs:92
    if (duration <= 0.0f) duration = 3.4028235e38f;     // cs:105

    // ADSR fallbacks (cs:108-111).
    const float attack  = envX > 0.0f ? envX : 0.01f;
    const float decay   = envY > 0.0f ? envY : 0.1f;
    const float sustain = envZ >= 0.0f ? clamp01f(envZ) : 0.7f;
    const float release = envW > 0.0f ? envW : 0.3f;

    // Advance the frame-based envelope (cs:118-136 gate + AdsrCalculator.Update). gate = shouldPlay.
    const float envValue = advanceAudioPlayerEnvelope(st, shouldPlay, timeSecs, attack, decay, sustain,
                                                      release, triggerMode, duration);
    // envelope-modulated volume (cs:139). bug 2 does NOT touch this — it's the edge below.
    const float envModVol = useEnvelope ? volume * envValue : volume;

    // Ensure the stream exists (create-on-first-cook / re-point on file change), AudioEngine.cs:519/780.
    if (!path.empty() && (!st.haveEnsured || st.lastPath != path)) {
      emitAudioEnsure(key, path);
      st.lastPath = path;
      st.haveEnsured = true;
    }

    // Live attributes every frame (cs:151-160 pass volume/mute/panning/speed to the stream).
    emitAudioGain(key, envModVol, mute);
    emitAudioPan(key, panning);
    emitAudioRate(key, speed);

    // Play/stop edge (AudioEngine.HandlePlaybackTriggers, cs:833-868): rising play → seek(Seek) then play;
    // shouldStop → stop. bug 2 drops the edge guard → Play emitted every frame regardless of the trigger edge.
    const bool risingPlay = g_audioPlaybackBug == 2 ? shouldPlay : (shouldPlay && !st.prevPlayTrigger);
    if (risingPlay) {
      emitAudioSeek(key, seek);   // Seek input applied on the play edge (cs:855-860 PendingSeek)
      emitAudioPlay(key);
    } else if (shouldStop && st.prevPlayTrigger) {
      emitAudioStop(key);
    }
    if (g_audioPlaybackBug != 2) st.prevPlayTrigger = shouldPlay;

    // Pause edge (cs:142-149).
    if (pauseAudio != st.prevPause) {
      if (pauseAudio) emitAudioPause(key); else emitAudioResume(key);
      st.prevPause = pauseAudio;
    }

    // Outputs: IsPlaying / GetLevel. Without a live mixer read-back in a golden these mirror the intent
    // (shouldPlay && !shouldStop) and the envelope-modulated gain as a level proxy — the golden asserts the
    // COMMANDS + envModVol, the real IsPlaying/GetLevel come from the mixer feedback the app writes back.
    rn.extOut[0] = (shouldPlay && !shouldStop) ? 1.0f : 0.0f;   // IsPlaying (intent; app overwrites w/ mixer truth)
    rn.extOut[1] = envModVol;                                    // GetLevel proxy (see note)
  }
}

// ── PlayAudioClip ────────────────────────────────────────────────────────────────────────────────────
// Faithful port of PlayAudioClip.cs Update (cs:47-74). Inputs (cs:91-104): Path(str), TimeInSecs(float),
// Volume(float), IsLooping(bool), IsPlaying(bool). Time-driven soundtrack scrub, NOT a trigger/level op.
//   anchor (cs:54-57): when TimeInSecs unconnected, _startRunTimeInSecs = RunTimeInSecs on first play.
//   targetTime (cs:61-63): TimeInSecs.HasInputConnections ? timeParam : RunTimeInSecs - _startRunTimeInSecs.
//   loop wrap (cs:65-68): if IsLooping && targetTime > LengthInSeconds → targetTime %= LengthInSeconds.
//   UseSoundtrackClip(targetTime) + clip.Volume = Volume (cs:71-72).
// sw expresses this as Ensure(path) + Seek(targetTime) + SetGain(volume) + Play/Stop(IsPlaying); the loop
// length comes from the mixer duration (0 in a headless golden → no wrap; the golden feeds length via a
// separate probe). NAMED FORK fork-playaudioclip-hasinputconnections: the resident resolver can't see wire
// presence at cook time, so sw uses the RunTimeInSecs-anchor branch (the unconnected path) always — the
// connected-TimeInSecs case is expressed by the caller wiring TimeInSecs, which resolveResidentFloatInputs
// already folds into P["TimeInSecs"]; the anchor just isn't subtracted then. Handled below by: if the
// resolved TimeInSecs is non-zero we treat it as the direct time (connected), else the wall anchor.
// Output: Result is a Command (no value rail) → extOut[0] echoes targetTime (the golden probe).
void cookPlayAudioClipNodes(ResidentEvalGraph& g, double runTimeSecs,
                            std::map<std::string, PlayAudioClipState>& state) {
  ResidentEvalCtx rctx;
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "PlayAudioClip") continue;
    std::map<std::string, float> P = resolveResidentFloatInputs(g, rn, rctx);
    const std::string key = streamKey(rn);
    const std::string path = rn.strInputs.count("Path") ? rn.strInputs.at("Path") : std::string();

    const float timeInSecs = P["TimeInSecs"];
    const float volume     = P["Volume"];
    const bool  isLooping  = P["IsLooping"] != 0.0f;
    const bool  isPlaying  = P["IsPlaying"] != 0.0f;
    // Clip length comes from the mixer stream duration (fed back by the app once the stream is loaded); at
    // cook time in a headless golden it is 0 → no loop wrap. lengthFor is a hook the app can fill from
    // AudioMixer::durationSeconds(key) via a feedback channel (deferred with the app-drain; see header).
    const double lengthSecs = 0.0;  // TODO(app-drain): feed AudioMixer::durationSeconds(key) here.

    PlayAudioClipState& st = state[key];

    if (!path.empty() && (!st.haveEnsured || st.lastPath != path)) {
      emitAudioEnsure(key, path);
      st.lastPath = path;
      st.haveEnsured = true;
    }

    double targetTime = 0.0;
    if (isPlaying) {
      if (!st.haveAnchor) { st.startRunTimeSecs = runTimeSecs; st.haveAnchor = true; }  // cs:54-57
      // fork-playaudioclip-hasinputconnections: connected TimeInSecs (nonzero) → direct; else wall anchor.
      const bool connected = (timeInSecs != 0.0f);
      targetTime = playAudioClipTargetTime(connected, (double)timeInSecs,
                                           runTimeSecs - st.startRunTimeSecs, isLooping, lengthSecs);
      emitAudioSeek(key, targetTime);
      emitAudioGain(key, volume, false);   // clip.Volume (cs:72)
      emitAudioPlay(key);
    } else {
      st.haveAnchor = false;   // reset the anchor when not playing (next play re-anchors)
      emitAudioStop(key);
    }

    rn.extOut[0] = (float)targetTime;   // targetTime echo (golden probe)
  }
}

void cookAudioPlaybackNodes(ResidentEvalGraph& g, float timeSecs, double runTimeSecs) {
  static std::map<std::string, AudioPlayerState> s_playerState;
  static std::map<std::string, PlayAudioClipState> s_clipState;
  cookAudioPlayerNodes(g, timeSecs, s_playerState);
  cookPlayAudioClipNodes(g, runTimeSecs, s_clipState);
}

}  // namespace sw
