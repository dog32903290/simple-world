# Melodica MIDI Analyzer for Bespoke

This is a BespokeSynth node patch kit for turning melodica audio gestures into MIDI.

It uses Bespoke native modules first:

```text
input -> audiosplitter
audiosplitter[0] -> leveltocv -> midicc(value) -> midioutput
audiosplitter[1] -> audiotopulse -> script -> midioutput
```

The newer native attack lane is:

```text
audiosplitter[1] -> attackamount -> effect/modulator target
```

`attackamount` follows the Max-style patch:

```text
audio -> envelope follower -> delayed envelope -> subtract -> positive only -> threshold/gain/decay
```

The matching native decay lane is:

```text
audiosplitter[1] -> decayamount -> effect/modulator target
```

The native density lane counts attack events over a time window:

```text
audiosplitter[1] -> attackamount -> densityamount~input -> effect/modulator target
```

## Output Map

```text
CC20 loudness  continuous, from leveltocv into midicc value
CC21 attack    pulse spike from script
CC22 density   recent pulse density from script
CC23 decay     post-attack falling envelope from script
```

## Build In Bespoke

1. Open BespokeSynth.
2. Create one `script` module.
3. Paste or load `scripts/melodica_midi_analyzer.py` into that script module.
4. Run:

```python
build_patch()
```

5. In the generated `melodica_midi_out` module, choose your MIDI output device, for example IAC Driver or a hardware MIDI port.
6. Route your melodica microphone into the generated `melodica_input`.

The builder deletes and recreates only these named modules:

```text
melodica_input
melodica_splitter
melodica_loudness
melodica_attack_pulse
midi_loudness_cc
melodica_midi_out
```

It does not delete the script module that runs it.

## Calibration

Start here:

```text
melodica_loudness gain: 35
melodica_loudness attack: 5
melodica_loudness release: 80
melodica_attack_pulse threshold: 0.18
melodica_attack_pulse release: 120
```

If quiet breath triggers too often, raise `melodica_attack_pulse~threshold`.
If strong notes never trigger, lower it.
If CC20 is always high, lower `melodica_loudness~gain`.

## Contract

`loudness` answers: how much input energy is present now.

`attack trigger` answers: did Bespoke see a threshold crossing.

`attack amount` answers: how strongly the input envelope rose over a short window.

`decay amount` answers: how strongly the input envelope fell over a short window.

`density` answers: how many attacks happened in the recent window.

`decay` answers: how far the current post-attack tail envelope has fallen.

These are intentionally separated. MIDI mapping is only the final output layer.
