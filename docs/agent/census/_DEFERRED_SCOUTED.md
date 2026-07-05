
## transport-YELLOW consumers (scouted Cut86 session 2026-06-20, DEFER to 補縫/Phase-C per 柏為 6-block directive)
Seam already built (Cut85 TransportSnapshot on cookStatefulValueOp rail). 3 clean READ-only leaves, SEQUENTIAL (share stateful_value_ops.cpp + node_registry_math.cpp). Build order + blueprint:
1. ConvertTime (0 state) — bpm bars<->secs. step: Result=mode==0? Time*240/bpm : Time*bpm/240. NodeSpec Time(Float)+Mode(Widget::Enum BarsToSeconds/SecondsToBars). golden bpm=120: B2S(1)=2, S2B(2)=1; bpm=240 proves live bpm read.
2. RunTime (0 state) — out=tr.runTimeSecs. golden 3 frames dt=0.5 -> 0.5/1.0/1.5, independent of scrub/pause (R-1 origin fork visible here, name it).
3. DelayTriggerChange (6 state: lastTrue/lastFalse/lastChange/triggered/stateBeforeChange/delayedHeld) — DelayTriggerChange.cs:30-95, .t3 default TimeMode=6(AppRunTime_InSecs)/Mode=0(DelayTrue)/DelayDuration=1.0. TWO-edge change detector (NOT WasTriggered rising). 7 TimeModes map to snapshot (F-1: LocalTime==PlayTime==t.position both legs). DelayTrue first-second delayed=true is FAITHFUL (don't seed s0). golden legs A(delay rise)/B(DelayFalse)/C(bpm conv TimeMode=1)/D(held-state reconstruct DelayBoth).
EXCLUDED: DelayBoolean(Queue 500>s12)/WasTrigger(context-var seam)/GetFrameSpeedFactor(needs FrameSpeedFactor field not in POD)/Set*(write-back RED).

## io/audio family — the mixer-bound atoms (scouted lane/dev-audio 2026-07-05)
The whole `Lib/io/audio` playback family blocks on ONE missing subsystem: TiXL's per-operator BASS
mixer (`AudioMixerManager` + per-node `ProceduralToneStream`/clip stream + `AudioEngine.*OperatorStream`
+ `ChannelGetLevel` readback). sw's `platform/audio_playback` is a SINGLE-stream soundtrack player
(the SoundtrackClipStream port — one composition backing track), NOT a multi-node operator mixer, and
sw's `platform/audio_capture` is input-only. Until a native AVAudioEngine multi-node mixer exists (per
`AudioPlayerUtils.ComputeInstanceGuid` operator identity + a mixer graph), these atoms have NO golden-able
node output — every one of their outputs is hardware-driven.

- **AudioToneGenerator** — DONE (partial): synthesis CORE ported + golden'd (runtime/tone_synth,
  --selftest-tonesynth, commit 38448a3). NODE deferred-hw: its 3 outputs (Command/IsPlaying/GetLevel)
  are ALL operator-mixer driven. Noise waveforms (White/Pink) RNG-stateful → no closed-form, unpinned.
- **AudioPlayer** — DEFERRED-HW + needs-mixer. 3 outputs (Command/IsPlaying bool/GetLevel float), all
  from `AudioEngine.{Play,Pause,Resume,UnregisterOperator, IsOperatorStreamPlaying, GetOperatorLevel}`
  + `UpdateStereoOperatorPlayback` (AudioPlayer.cs:81-169). Distinct path from soundtrack (arbitrary
  per-node triggered sounds w/ ADSR, NOT the single backing track) — do NOT collapse into soundtrack.
  ADSR half is portable (sw has AdsrCalculator, --selftest-adsrenvelope); the stream half is the wall.
- **SpatialAudioPlayer** — DEFERRED-HW + needs-3D-mixer (heaviest). 4 outputs + ITransformable +
  ISpatialAudioPropertiesProvider; needs `AudioEngine.Set3DListenerPosition` + `UpdateSpatialOperator
  Playback` + a 3D-panned BASS mixer (SpatialAudioPlayer.cs:160-257). Blocks SpatialAudioPlayerGizmo +
  render/DrawSpatialAudioGizmos (both depend on it existing).
- **GetBeatTimingDetails** — BLOCKED on dict seam (other worker). Its ONLY output is `Slot<Dict<float>>`
  (reflects every static float field of BeatTimingDetails into a dict, GetBeatTimingDetails.cs:20-40).
  100% dict — there is NO non-dict part to do first; the MathOp Float rail can't express it. Not PARTIAL,
  fully blocked until the Dict<float> output rail lands. (sw already has beat_timing.cpp/detect_bpm as
  the FEEDING analysis; only the dict-packing output op is blocked.)
