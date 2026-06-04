# Repo Instructions: simple_world

## Bespoke Sound-In-Area Skill

This repo contains a local skill:

```text
skills/bespoke-sound-in-area/SKILL.md
```

Use it whenever the task mentions:

- Bespoke / BespokeSynth usage
- writing or modifying Bespoke nodes
- Bespoke script module code
- patching audio, notes, pulses, modulators, looper, recorder, sampleplayer, buffershuffler, notesinger
- `sound_in_area` buffer memory, relation control, continuity, arrangement modes, or previous 4-bar loop prototypes

Before answering or editing those topics, read:

```text
bespoke_ai_manual_v1.md
bespoke_ai_manual_v1.json
```

Rules:

- Do not invent Bespoke patch cable ports, control names, script methods, save-state fields, or native module requirements.
- Separate audio, notes, pulses, modulation/control, and module-specific special cables.
- Prefer Bespoke native audio modules before custom DSP.
- Put custom work first in analysis, relation control, memory metadata, continuity, and arrangement modes.
- If a needed node or method is missing from the v1 manual, inspect upstream docs/source before claiming behavior.

## TiXL to Vuo Node Port Skill

This repo contains a local skill:

```text
skills/tixl-vuo-node-port/SKILL.md
```

Use it whenever the task mentions:

- porting or naming TiXL nodes into My World / Vuo
- shader, SDF, raymarch, mesh, image, render, or renderTick Vuo prototypes
- comparing TiXL node function against our runtime fixtures
- debugging Vuo custom nodes that show `Not Installed`, black windows, or stale values

Rules:

- Creator-facing node names follow TiXL exactly with only `my_` in front: `my_<ExactTiXLNodeName>`, such as `SphereSDF` -> `my_SphereSDF`.
- Categories follow TiXL `Operators/Lib` families.
- Colors follow TiXL type color, not namespace color; e.g. `ShaderGraphNode` uses `ColorForShaderGraph`.
- Prove shader behavior in our runtime tools before treating the Vuo node as valid.
- Separate event flow from data flow: `renderTick` triggers cooking; data ports stay semantic values.
