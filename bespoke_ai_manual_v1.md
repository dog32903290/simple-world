# BespokeSynth Sound-In-Area Node Manual v1

> Purpose: make Codex answer and build Bespoke-based `sound_in_area` work from checked nodes, checked script calls, and explicit uncertainty. This is not a full Bespoke clone manual. It is the node/protocol sheet for this repo's current lane.

## Source Rule

- `HIGH`: checked against upstream source or official docs.
- `MEDIUM`: official docs checked, source class not deeply audited.
- `LOW`: name appears in a registry/list only; do not give wiring advice yet.

When answering Bespoke usage questions or writing nodes/scripts:

1. Read this manual first.
2. Use `bespoke_ai_manual_v1.json` for machine-readable node cards.
3. If a needed module is absent or `LOW`, inspect upstream docs/source before claiming details.
4. Do not invent patch cable ports, control names, script method names, or save-state fields.
5. Prefer Bespoke native audio modules for input, recording, looping, playback, modulation, routing, effects, MIDI, and OSC. Our custom work should sit in control/analysis/relation/continuity layers unless native nodes cannot carry the force.

## Upstream Sources Checked

- Official docs: <https://www.bespokesynth.com/docs/>
- GitHub repo: <https://github.com/BespokeSynth/BespokeSynth>
- Script wiki: <https://github.com/BespokeSynth/BespokeSynth/wiki/Scripting-Reference>
- Local upstream clone used for source inspection: `/tmp/BespokeSynth`

## Core Architecture Protocol

### Native Module Registration

Upstream `Source/ModuleFactory.cpp` registers modules with:

```cpp
REGISTER(ClassName, moduleid, kModuleCategory_...)
```

The macro stores:

- module id string
- creator function
- `CanCreate`
- category
- hidden / experimental flags
- `AcceptsAudio()`
- `AcceptsNotes()`
- `AcceptsPulses()`

So a module card must separate:

```text
doc behavior      what official docs say the node does
source identity   ClassName + ModuleFactory registration
accepts           audio / notes / pulses from class static methods
control names     only from official docs or source UI controls
script access     only from scripting reference / pybind source
```

### Main Interfaces

- `IDrawableModule`: visible canvas module, UI controls, patch cable sources, save/load, category, enabled/minimized/pinned state.
- `IAudioSource`: audio-producing side; key method `Process(double time)`.
- `IAudioReceiver`: audio-receiving side; owns/uses input buffer.
- `IAudioProcessor`: common audio effect/processor shape; combines source + receiver.
- `INoteReceiver`: receives note messages / MIDI-ish note events.
- `INoteSource`: sends notes through note outputs.
- `IPulseReceiver`: receives pulse events.
- `IPulseSource`: dispatches pulse events.
- `IModulator`: modulation value output; often used to drive sliders/control targets.

### Patch Language

Bespoke has separate cable families. Do not treat them as one generic wire.

- audio cable: cyan in docs; carries audio buffers.
- note cable: orange in docs; carries note events.
- pulse cable: yellow in docs; carries pulse triggers.
- modulation/control cable: modulator output to a control target.
- special grey/purple cables: module-specific control links, e.g. `snapshots` or `loopergranulator` to `looper`. Verify before describing.

## Script Module Protocol

The `script` module is Python livecoding for notes and module control.

### Script Inputs

```python
def on_pulse():
    pass

def on_note(pitch, velocity):
    pass

def on_grid_button(col, row, velocity):
    pass

def on_osc(message):
    pass

def on_midi(messageType, control, value, channel):
    pass
```

`on_osc` requires `me.connect_osc_input(port)`. `on_midi` requires a `midicontroller` listener via `add_script_listener(me.me())`.

### Bespoke Globals

Useful checked calls:

```python
bespoke.get_measure_time()
bespoke.get_measure()
bespoke.reset_transport()
bespoke.get_step(subdivision)
bespoke.count_per_measure(subdivision)
bespoke.time_until_subdivision(subdivision)
bespoke.get_time_sig_ratio()
bespoke.get_root()
bespoke.get_scale()
bespoke.get_scale_range(octave, count)
bespoke.tone_to_pitch(index)
bespoke.name_to_pitch(noteName)
bespoke.pitch_to_name(pitch)
bespoke.pitch_to_freq(pitch)
bespoke.get_tempo()
bespoke.random(seed, index)
bespoke.get_modules()
```

### Script-Relative Control

Use these for control scripts before native modules:

```python
me.play_note(pitch, velocity, length=1.0/16.0, pan=0, output_index=0)
me.schedule_note(delay, pitch, velocity, length=1.0/16.0, pan=0, output_index=0)
me.schedule_call(delay, "method_name()")
me.set(path, value)
me.schedule_set(delay, path, value)
me.get(path)
me.adjust(path, amount)
me.output(obj)
me.stop()
me.set_num_note_outputs(num)
me.connect_osc_input(port)
me.send_cc(control, value, output_index=0)
```

Path examples in upstream docs look like:

```python
me.set("oscillator~pw", 0.2)
pulsewidth = me.get("oscillator~pulsewidth")
```

Rule: use paths only after confirming target module/control names in a working patch or docs.

### Module Creation / Lookup

Checked scripting interface:

```python
import module

m = module.get("path")
m = module.create("moduleType", x, y)
m.set_position(x, y)
m.get_position_x()
m.get_position_y()
m.get_width()
m.get_height()
m.set_target(target)
m.set_target("targetPath")
m.get_target()
m.get_targets()
m.set_name(name)
m.delete()
m.set("control", value)
m.get("control")
m.adjust("control", amount)
```

Use this for proof scripts and patch scaffolding only after verifying control names.

### Script Access To Specific Modules

Checked examples from scripting reference:

```python
import sampleplayer
s = sampleplayer.get("sampleplayer")
s.set_cue_point(pitch, startSeconds, lengthSeconds, speed)
s.play_cue(cue, speedMult=1, startOffsetSeconds=0)
s.get_length_seconds()

import oscoutput
o = oscoutput.get("oscoutput")
o.send_float("/addr", val)
o.send_int("/addr", val)
o.send_string("/addr", val)
```

Other checked imports include `notesequencer`, `drumsequencer`, `grid`, `notecanvas`, `midicontroller`, `drumplayer`, `vstplugin`, `envelope`, `snapshots`.

## Sound-In-Area Design Boundary

Locked project line:

```text
buffer selection is not single-point picking; it is current sound position plus previous/next material continuity. Bad phrasing usually breaks in the continuity layer, not only in the shape detector.
```

Current engineering strategy:

```text
Bespoke fork/audio skeleton first.
Use existing Bespoke audio nodes when they can carry the job.
Add custom code mainly for analysis, relation control, memory metadata, continuity, and arrangement modes.
```

## Node Cards For This Repo

### input

- Category: audio
- Source: official docs
- Accepts: audio
- Role: get audio from microphone/input channel.
- Control: `ch` chooses channel or stereo channel pair.
- sound_in_area use: dry melodica/mic input source.
- Do not: use this to capture Ableton master/post-FX memory unless explicitly changing project boundary.

### audiometer

- Category: audio
- Source class: `AudioMeter`
- Registered id: `audiometer`
- Accepts: audio
- Role: reads input audio level and exposes/display a level slider.
- Controls: `level`.
- sound_in_area use: visual/debug loudness, MIDI display feedback, calibration proof.
- Better for control signal: `leveltocv`.

### leveltocv

- Category: modulator
- Source class: `AudioLevelToCV`
- Registered id: `leveltocv`
- Accepts: audio
- Role: converts incoming audio level to modulation value.
- Controls: `gain`, `attack`, `release`, `min`, `max`.
- sound_in_area use: loudness/pressure control source for same/opposite relation patches.
- Good first patch: `input -> leveltocv -> smoother/macroslider -> looper volume or granulator parameter`.

### audiotopulse

- Category: pulse
- Source class: `AudioToPulse`
- Registered id: `audiotopulse`
- Accepts: audio
- Role: emits a pulse when input level crosses threshold.
- Controls: `threshold`, `release`.
- sound_in_area use: attack-ish trigger, capture trigger, fragment trigger.
- Failure risk: threshold is amplitude-only; it is not a complete attack detector. Calibrate per mic/player.

### attackamount

- Category: modulator
- Source class: `AttackAmount`
- Registered id: `attackamount`
- Accepts: audio
- Role: Max-style attack amount from `envelope_now - envelope_window_ms_ago`, positive-only, thresholded, gained, and decay-shaped.
- Controls: `follow`, `window`, `threshold`, `gain`, `decay`, `min`, `max`.
- sound_in_area use: continuous attack strength for effect controls; not just a pulse trigger.
- Failure risk: if `threshold` is too low it follows breath/noise; if `window` is too long it starts reading phrase-level dynamics instead of onset.

### decayamount

- Category: modulator
- Source class: `DecayAmount`
- Registered id: `decayamount`
- Accepts: audio
- Role: Max-style decay amount from `envelope_window_ms_ago - envelope_now`, positive-only, thresholded, gained, and decay-shaped.
- Controls: `follow`, `window`, `threshold`, `gain`, `decay`, `min`, `max`.
- sound_in_area use: continuous falling-envelope strength for release/tail-sensitive effect controls.
- Failure risk: phrase-level fades and mic movement can look like decay if `window` is too long or `threshold` is too low.

### densityamount

- Category: modulator
- Source class: `DensityAmount`
- Registered id: `densityamount`
- Accepts: modulation via `input`; does not accept audio
- Role: counts attack input threshold crossings inside a time `window`, scaled by `full count`.
- Controls: `input`, `threshold`, `window`, `full count`, `min`, `max`.
- sound_in_area use: attack density lane: `attackamount -> densityamount~input -> effect/control target`.
- Failure risk: if attack threshold/gain upstream is unstable, density will count noise as events.

### notesinger

- Category: instrument
- Source class: `NoteSinger`
- Registered id: `notesinger`
- Accepts: audio
- Role: detects pitch and outputs notes.
- Control: `oct` adjusts output pitch.
- sound_in_area use: melodica pitch extraction, pitch-class relation, harmonic tension control.
- Risk: use for monophonic melodica lines first; do not assume robust polyphonic pitch detection.

### pitchtocv / pitchtovalue / notetofreq

- `pitchtocv`: note pitch to modulation value; controls `min`, `max`; accepts notes.
- `pitchtovalue`: outputs MIDI pitch value; accepts notes.
- `notetofreq`: note to frequency in Hz; accepts notes.
- sound_in_area use: pitch coordinate after `notesinger` or MIDI melodica input.
- Harmonic relation is not built in. We still need a control script/table for consonance/dissonance distance.

### looperrecorder

- Category: audio
- Source class: `LooperRecorder`
- Registered id: `looperrecorder`
- Accepts: audio
- Role: command center for recording into multiple loopers and retroactive capture.
- Important controls/buttons from docs: `1`, `2`, `4`, `8` retroactive capture buttons, `.5tempo`, `2xtempo`, `auto-advance`, `clear`, `free rec`, `length`, `mode`, `target`, `write*`.
- Recorder modes from docs: `record`, `overdub`, `loop`.
- sound_in_area use: first arrangement module, "previous 4 bars of myself".
- First playable pattern: `input -> looperrecorder`, connect recorder to one or more `looper`s, trigger the `4` capture into target looper.

### looper

- Category: audio
- Source class: `Looper`
- Registered id: `looper`
- Accepts: audio, notes
- Role: loop input audio; works with `looperrecorder`.
- Important controls/buttons: `num bars`, `volume`, `mute`, `passthrough`, `decay`, `offset`, `pitch`, `capture`, `commit`, `clear`, `write`, `.5x`, `2x`, `extend`, `undo`, `save`, `fourtet`, `fourtetslices`, `scr`, `scrspd`, `auto`.
- sound_in_area use: memory lane playback, previous-loop response, continuity test surface.
- Relation examples:
  - same loudness: map `leveltocv` upward to `volume`.
  - opposite loudness: invert/curve level before `volume`.
  - rupture: pulse-trigger `mute`, `clear`, or hard volume drop only after testing.
  - residue: increase `decay`, lower `volume`, preserve tail.

### loopergranulator

- Category: other
- Source class: `LooperGranulator`
- Registered id: `loopergranulator`
- Accepts: none by static accepts; it links to a `looper` through a module-specific cable.
- Role: granular playback of a connected looper.
- Controls: `on`, `freeze`, `loop pos`, `len ms`, `overlap`, `speed`, `pos rand`, `speed rand`, `spacing rand`, `octaves`, `width`.
- sound_in_area use: turn previous loop into fragment/residue/freeze material without replacing Bespoke playback engine.
- Do not describe it as normal audio input; it is attached to a looper.

### buffershuffler

- Category: audio
- Source class: `BufferShuffler`
- Registered id: `buffershuffler`
- Accepts: audio, notes
- Role: constantly records live input buffer; notes trigger slices; pitch maps to slice index.
- Controls: `num bars`, `interval`, `freeze input`, `playback style`, `fourtet`, `fourtetslices`, `grid`.
- sound_in_area use: short-attack / flicker / fragment module; a native alternative before building a custom slicer.
- Good first patch: `input -> buffershuffler`, route `notesinger`/script notes into it to play pitch-indexed slices.

### sampleplayer

- Category: synth
- Source class: `SamplePlayer`
- Registered id: `sampleplayer`
- Accepts: audio, notes, pulses
- Role: sample playback with cue points, clip extraction, recording, slicing.
- Important controls: `play`, `pause`, `stop`, `loop`, `record`, `attack`, `cuepoint`, `cue start`, `cue len`, `cue speed`, `cue stop`, `play cue`, `auto-slice 4/8/16/32`.
- Script API: `set_cue_point`, `play_cue`, `get_length_seconds`.
- sound_in_area use: curated memory buffers after the first looper prototype.

### seaofgrain

- Category: synth
- Registered id: `seaofgrain`
- Accepts: audio, notes
- Role: granular synth; can record input as live granular buffer.
- Controls include `record`, `volume`, `display length`, `keyboard base pitch`, `keyboard num pitches`, and per-voice grain controls (`pos`, `len ms`, `speed`, `overlap`, `pan`, `width`, randomization).
- sound_in_area use: overflow/residue texture response.

### macroslider

- Category: modulator
- Source class: `MacroSlider`
- Registered id: `macroslider`
- Role: one input value sends scaled versions to multiple destinations.
- Controls: `input`, `start*`, `end*`.
- sound_in_area use: controller lane for "same/opposite/mixed" relation maps.

### smoother / ramper / expression

- `smoother`: smooths a modulation input; controls `input`, `smooth`.
- `ramper`: blends a control to target over time; controls `length`, `target`, `start`; accepts pulses.
- `expression`: shapes modulation with expression; variables `a`-`e`, input as `x`, time as `t`.
- sound_in_area use: make control musical instead of twitchy; invert or curve loudness/pitch relation before it reaches playback.

### valuesetter

- Category: modulator
- Source class: `ValueSetter`
- Registered id: `valuesetter`
- Accepts: pulses
- Role: set a specified value on a targeted control when clicked/pulsed.
- Controls: `value` / `slider`, `set`.
- sound_in_area use: pulse-triggered mode changes, hard cuts, snapshot-like simple control changes.

### selector / groupcontrol / snapshots

- `selector`: choose one enabled value; accepts notes.
- `groupcontrol`: connect to several checkboxes and control them together.
- `snapshots`: store/recall sets of values; accepts notes; special grey/purple cable behavior.
- sound_in_area use: manual performance control states, response modes, same/opposite/mixed presets.
- Do not assume snapshot cable behavior without testing in patch; docs describe special connection semantics.

### oscoutput / midioutput

- `oscoutput`: send OSC when sliders change or notes arrive; accepts notes; script API supports `send_float`, `send_int`, `send_string`.
- `midioutput`: send MIDI notes to external hardware/software; accepts notes.
- sound_in_area use: external diagnostics/control after internal patch proves itself.

## First Patch Cookbooks

### Prototype A: Previous 4-Bar Loop Response

Goal:

```text
live melodica -> capture previous 4 bars -> play it back as response material
```

Native node skeleton:

```text
input
-> looperrecorder
-> looper
-> gain / effects / output
```

Control layer:

```text
input -> leveltocv -> smoother -> looper volume
input -> audiotopulse -> script/valuesetter -> trigger capture or fragment action
input -> notesinger -> pitch relation script/table
```

Do this first because it tests the real hard problem: whether "previous me" can become a musical counter-line.

### Prototype B: Fragment/Flicker Native Path

Goal:

```text
short attacks or notes trigger slices from a live rolling buffer
```

Skeleton:

```text
input -> buffershuffler -> output
notesinger or script notes -> buffershuffler note input
```

Parameters to play:

```text
num bars
interval
freeze input
playback style
fourtet
fourtetslices
```

### Prototype C: Residue / Freeze Granular Loop

Goal:

```text
previous loop becomes held residue, freeze, or scattered tail
```

Skeleton:

```text
input -> looperrecorder -> looper -> loopergranulator -> output path
leveltocv / expression / macroslider -> granulator on/freeze/pos/speed/width
```

Test manually before scripting the control paths.

## What We Still Need Custom

These should be custom control/data layers, not first-pass audio engine rewrites:

- `CurrentSoundVector`: loudness, attack-ish pulse rate, pitch, pitch confidence, density, sustain/residue estimates.
- `RelationProfile`: same/opposite/middle/ignore per dimension.
- `HarmonicTensionTable`: 12-TET interval table, hand-editable.
- `BufferMemoryMetadata`: entry/exit pitch, loudness, density, attack, tail/residue, source type.
- `ContinuityScorer`: current match plus previous-buffer transition score.
- `ArrangementMode`: previous-4-bar loop, fragment response, residue answer, gap/cut, call-response.

## Native Module Boundary

Do not add a native C++ module until a script/native-node patch proves the rule musically. If adding a native module later:

1. Inspect existing similar module class.
2. Add `.h/.cpp`.
3. Add build integration if required.
4. Register in `ModuleFactory.cpp`.
5. Implement `Create`, `CanCreate`, `AcceptsAudio/Notes/Pulses`, category, UI controls, process path, save/load.
6. Test in a minimal patch.

For now, the more correct first move is usually:

```text
Bespoke native audio nodes + script/control layer + explicit diagnostics
```
