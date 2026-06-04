---
name: bespoke-sound-in-area
description: Use when writing BespokeSynth nodes/scripts/patch plans in this repo, or when the user asks how to use Bespoke for sound_in_area. Always consult the repo Bespoke manual before answering or editing.
---

# Bespoke Sound-In-Area

## Trigger

Use this skill whenever the task mentions:

- Bespoke / BespokeSynth usage
- writing or modifying Bespoke nodes
- Bespoke script module code
- patching audio, notes, pulses, modulators, looper, recorder, sampleplayer, buffershuffler, notesinger
- `sound_in_area` arrangement, buffer memory, continuity, relation control, previous 4-bar loop

## First Move

Read these repo files before answering or editing:

```text
bespoke_ai_manual_v1.md
bespoke_ai_manual_v1.json
```

If v1 lacks the node or method you need, inspect upstream source/docs before claiming behavior:

```text
https://www.bespokesynth.com/docs/
https://github.com/BespokeSynth/BespokeSynth
https://github.com/BespokeSynth/BespokeSynth/wiki/Scripting-Reference
```

If an upstream clone exists, prefer local source inspection:

```text
/tmp/BespokeSynth
```

## Working Rules

- Do not invent patch cable ports, control names, script methods, save-state fields, or native module requirements.
- Separate audio, notes, pulses, modulation/control, and module-specific special cables.
- Prefer Bespoke native audio modules before custom DSP.
- Put custom work first in analysis, relation control, memory metadata, continuity, and arrangement modes.
- When unsure, say which line is unverified and what must be inspected.

## Project Lock

Current project sentence:

```text
buffer selection is not single-point picking; it is current sound position plus previous/next material continuity. Bad phrasing usually breaks in the continuity layer, not only in the shape detector.
```

Current first playable direction:

```text
Bespoke native audio nodes + script/control layer + previous 4-bar melodica loop response.
```

## Minimal Patch Bias

Start with these native-node prototypes before proposing native C++ modules:

```text
Previous loop:
input -> looperrecorder -> looper -> output
leveltocv / audiotopulse / notesinger -> script/control layer

Fragment:
input -> buffershuffler -> output
notesinger or script notes -> buffershuffler

Residue/freeze:
input -> looperrecorder -> looper -> loopergranulator
leveltocv/expression/macroslider -> granulator controls
```

## Native Module Gate

Only propose a new native C++ module after the native-node/script patch cannot carry the behavior. If native code is needed, inspect a similar upstream module and verify:

```text
.h/.cpp
ModuleFactory registration
AcceptsAudio/Notes/Pulses
category
UI controls
process path
save/load
minimal patch test
```
