# My World Runtime Contract

Version: `0.2`

Amended 2026-06-09 per `CONTRACT_ALIGNMENT_LEDGER.md` (L1–L14): recentered from a
"visual runtime" to an **interaction graph whose primary output is visual**; Vuo dropped
as host; Transport clock + `scoreGraph` + parameter value-resolution added; "never own
audio" downgraded to external-first.

This is the native runtime law for My World. It exists so TiXL/TD-like node vocabulary has
one place to land without inheriting any single host application's hidden assumptions. The
host surface is imgui-node-editor (skin) + a self-built Metal engine; TiXL is the semantic
donor (design authority), not runtime law.

First proof line:

```text
graph fixture -> runtime cook -> observable artifact + debug snapshot
```

First native render-resource lane:

```text
docs/runtime/fixtures/native_runtime_lane.graph.json
-> docs/runtime/scripts/native_runtime_lane.py
-> command stream + resource_registry.json + texture_views.json + frame.ppm
```

## Non-Negotiables

- This is an **interaction graph whose primary output is visual** — not a visual-only
  runtime. Visual is the main axis; audio, MIDI, OSC, and future protocols are first-class
  live sources on the same fabric (L4, L5).
- The graph is the source of truth.
- Authoring mutates the graph only through commands — **but live performance gestures are
  not commands**; they are an ephemeral override stream, persisted only when automation
  recording is armed (L2, L5, Port Contract).
- Runtime state must be inspectable from text, not trapped inside GUI objects.
- **Acceptance is three-tier (L6):** (1) the agent verifies first with its own eye+hand —
  drive the app, observe the real behavior; (2) tests/artifacts are the regression +
  handoff guardrail, *not* the acceptance gate; (3) 柏為's hand is final authority, invoked
  only for genuinely risky items. "Compiles" never means "accepted."
- TiXL is semantic donor and design authority, not runtime law.
- The host surface is imgui-node-editor + a self-built Metal engine. **Vuo is dropped** (L7).
- The runtime owns no synthesis/DSP/instrument engine, but **may own a playback transport
  clock for loaded media** (L11). "External-first" is the current default, not a permanent
  "never" — the I/O boundary is designed to absorb internal ownership later.

## Five Graphs

### editorGraph

Human-facing patch layout.

Owns:

- node position
- collapsed/expanded compound state
- selection and comments
- visible labels
- inspector UI state

Does not own:

- cooked values
- GPU resources
- audio device state
- command history truth

### runtimeGraph

Executable graph derived from `editorGraph`.

Owns:

- topological cook order
- typed port links
- runtime ops
- resource dependencies
- frame/audio/event domains

### commandGraph

Formal mutation language.

Owns:

- create node
- delete node
- connect/disconnect ports
- set parameter
- expose compound port
- expand/collapse compound
- import translated graph

No direct JSON surgery is allowed outside this path.

### collaborationLog

AI and user work trace.

Owns:

- natural-language task
- commands attempted
- failures
- repair attempts
- proof artifacts
- final evidence

### scoreGraph

Authored time layer (L12). The "搭" layer; TiXL's `Animator`, generalized + versioned.

Owns:

- transport config (length, loop, rate, play-state policy)
- per-`(node, parameter)` automation curves (the `Curve` primitive, L9)
- saved versions / duplicates (multiple scores over one node graph)

Does not own:

- node topology (that is `editorGraph` / `runtimeGraph`)
- live override state (ephemeral, resolved per-frame — see Port Contract)

`runtimeGraph` reads `scoreGraph` at the transport playhead each cooked frame.

## Runtime Type System

Core value types (the "Vuo candidate" column is **historical** — Vuo is dropped (L7); the
names remain only as rough native-type hints until the native type registry lands):

| type | meaning | Vuo candidate | TiXL notes |
|---|---|---|---|
| `Float` | scalar real | `VuoReal` | TiXL `float` / `Single` |
| `Int` | integer | `VuoInteger` | TiXL `int` / `Int32` |
| `Bool` | boolean | `VuoBoolean` | direct |
| `String` | UTF text | `VuoText` | direct-ish |
| `Vec2` | 2D vector | `VuoPoint2d` | not always position |
| `Vec3` | 3D vector | `VuoPoint3d` | not always position |
| `Vec4` | 4D vector | `VuoPoint4d` or `Color` | must be classified per node |
| `Color` | RGBA color | `VuoColor` | TiXL often stores color as `Vector4` |
| `RangeFloat` | min/max float | Vuo range type or struct | TiXL range ports need explicit schema |
| `Gradient` | color ramp | custom | no direct first-pass law |
| `Curve` | value-over-axis primitive (keyframes → value); used as wireable data **or** bound to (param × transport-time) as automation — L9/L10 | native (cf. TiXL `Curve`/`VDefinition`) | keyframe fields: value, in/out interpolation (Constant/Linear/Smooth/Cubic/Tangent), tangent, tension; must support live keyframe append (punch-in) |
| `Texture2D` | image/texture | `VuoImage` | host-specific resource boundary |
| `Scene` | renderable scene graph | `VuoSceneObject` candidate | needs My World law |
| `Mesh` | geometry | `VuoMesh` candidate | needs ownership/resource law |
| `PointBuffer` | point cloud / particle buffer | custom | TiXL `BufferWithViews` does not port directly |
| `ShaderGraph` | generated shader function graph | custom | TiXL `ShaderGraphNode` donor only |
| `Command` | scene/render command stream | custom | cannot be A/B direct |
| `AudioFrame` | audio buffer slice ingested from an external host | external source, not internally computed | see `AUDIO_INGEST_CONTRACT.md` |
| `Event` | discrete trigger | command/event domain | not value-only |

## Node Contract

Every visible node and hidden runtime op must answer one question.

```text
question -> inputs -> operation/state -> outputs -> failure behavior -> evidence
```

Required fields:

```yaml
id: stable node type id
title: visible node title
family: value | color | texture | scene | mesh | point | shader | audio | midi | io | flow | debug
domain: frame | event | audio | setup | async
conversion: value -> value
inputs: []
outputs: []
params: []
state: stateless | frameState | persistent | externalResource
failure: {}
diagnostics: {}
evidence: {}
```

## Port Contract

Ports are typed boundaries, not UI decorations.

Each port requires:

```yaml
id: stable id
name: visible name
direction: input | output
type: runtime type
eventPolicy: none | receivesEvent | emitsEvent | trigger
default: value or null
required: true | false
role: rawFact | transform | decision | statePolicy | outputShaping | uiOnly | debug
liveEditable: true | false
```

Rules:

- Defaults come from source evidence or are marked `Unknown`.
- `Vec4` ports must declare `semanticType: color | vector | quaternion | unknown`.
- `Texture2D`, `Mesh`, `Scene`, `PointBuffer`, and `ShaderGraph` ports must declare resource ownership.

### Parameter Value Resolution (L5)

A parameter's effective value each frame is produced by exactly **one active driver**,
resolved by priority:

```text
override   — a live source (hand/audio/MIDI/OSC) touching this param right now;
             sticky until re-enabled (Ableton, L13). Transient; not saved unless armed.
  else
binding    — the param's one persistent driver, exactly ONE of (mutually exclusive):
               · connection   (an upstream wired node — the value-spine mechanism)
               · automation   (a scoreGraph curve sampled at the playhead, L10)
               · live-source  (a live source persistently bound, e.g. Speed ← audio.kick)
  else
constant   — the port default (Node::params[id])
```

Rules (these close the connection-placement and multi-source ambiguities):

- **One binding per param.** Binding a new driver replaces the old. The value-spine's existing
  connection-drive *is* `binding = connection`; automation is `binding = automation`; a standing
  audio bind is `binding = live-source`. The three are mutually exclusive.
- **No mixing in the resolution layer.** To combine several sources into one param, wire them
  through a node (Add / Max / …) — that node becomes `binding = connection`. Mixing is a *graph*
  operation, not a resolution-stack operation (same as TiXL: one input, one upstream).
- **override** is the momentary top layer any live source can assert; with **arm on**, the
  override is written into a `binding = automation` curve, punch-in style — only touched params
  written, untouched curves stay (L2, L13).
- Mechanism per TiXL `Slot.OverrideWithAnimationAction` (swap the param's driver).

**Live sources are one citizen type** — a knob, an audio value, a MIDI CC, an OSC message share
one shape; each can serve as a `binding` or assert an `override`. The runtime exposes a single
**source-registration** table; adding a new source kind (or protocol) is one row, not a new
subsystem (constitution rule 7).

## Cook Contract

Cook means one runtime evaluation pass.

Minimum cook phases:

1. `validateGraph`
2. `resolveTypes`
3. `buildCookOrder`
4. `allocateResources`
5. `execute`
6. `collectDiagnostics`
7. `publishArtifacts`

Cook domains:

| domain | cadence | examples |
|---|---|---|
| `setup` | on load / graph change | shader compile, device open |
| `frame` | visual frame | image, scene, shader uniform |
| `audio` | external host (not an internal callback) | audio-derived facts ingested at a frame boundary; no internal audio output — see `AUDIO_INGEST_CONTRACT.md` |
| `event` | trigger/message | MIDI, OSC, button, command |
| `async` | background task | file/network/AI |

## Clock Contract — Two Clocks, Never Merged

There are **two** times and they must never be collapsed into one value (L8).

### FrameScheduler — wall pulse

`FrameScheduler` owns the real-time frame heartbeat. It supplies the shared frame context
every cooked node reads:

```text
frameIndex
time
deltaTime
```

- monotonic, forward, **never pauses** — runs even when the transport is stopped
- answers: "what is the real-time frame pulse, and how much wall time passed?"
- read by anything needing real elapsed time: particles, feedback, live smoothing

### Transport — playhead

`Transport` owns the composition playhead ("where am I in the piece", scrubable):

```text
position     (bars / seconds)
length       (music-derived | manual | open-ended "record until stop")
playState    (stopped | playing | paused | recording)
rate, loop
fxTime       (advances while paused, for idle-motion liveness)
```

- driven **by** `FrameScheduler.deltaTime` when playing (`position += deltaTime × rate`);
  paused = position frozen; scrub = position jumps
- read by: automation evaluation (`scoreGraph` curves, L10), audio playback position
- **`fxTime`** (modeled on TiXL `FxTimeInBars`): when the playhead is paused the piece must
  not freeze dead — particles/feedback keep breathing on `fxTime`. Idle-motion is a toggle.
- (parked) `LocalTime`: per-eval remappable time for compounds / TimeClips, modeled on TiXL
  `EvaluationContext.LocalTime`.

**Law: wall pulse vs playhead are two questions; never one value.**

Other clock sources (MIDI clock, Ableton Link, the sample clock of an owned playback engine)
reconcile into this model at their boundary, never by hijacking the frame pulse.

Cook output must include:

```json
{
  "frame": 12,
  "cookOrder": ["nodeA", "nodeB"],
  "nodeStats": {},
  "errors": [],
  "artifacts": []
}
```

## Audio Boundary — External-First, Two Worlds

The runtime owns **no synthesis / DSP / instrument / audio-output engine**. That rule
stays. What changed (L3, L11): "never owns audio" is downgraded from a permanent ban to
**external-first**, because two distinct audio worlds exist and only one is purely external:

- **World 2 — loaded playback (near-term core).** The runtime loads a media file and
  **plays it** → it owns that **playback transport + sample clock**. Because it owns the
  clock, automation, audio, and visual share one playhead → tight sync is a byproduct. This
  is *not* an instrument; it is a clocked player + analysis.
- **World 1 — live external (e.g. BlackHole / Bespoke).** Genuinely external; no shared
  clock. Analyzed on our side and reconciled at the ingest boundary; bounded by a latency
  budget (L14), never sample-accurate.

Both collapse into one canonical `AudioInput` shape, **source-agnostic** (external OSC /
external signal / internal playback / future synthesis all publish the same shape), so
future internal sound is an added producer, not an architecture rewrite (L11).

Canonical representation, clock crossing, latency budget, and source-absent policy live in
`AUDIO_INGEST_CONTRACT.md`.

## Failure Contract

Every node declares failure behavior:

| failure | required decision |
|---|---|
| invalid input | visible error + downstream policy |
| type mismatch | graph validation error |
| compile error | keep last valid output if possible |
| missing resource | error output + no crash |
| device unavailable | degraded state + diagnostic |
| stale async result | ignored or version-checked |

Shader rule:

```text
compile success -> replace live program
compile failure -> keep last valid program + show error
```

## Resource Contract

Resource types:

- texture
- framebuffer
- shader program
- mesh buffer
- point buffer
- audio device
- MIDI/OSC/socket device
- file handle

Each resource declares:

```yaml
ownerNode:
lifetime: frame | graph | app | external
canShare: true | false
teardown:
debugName:
```

TiXL danger types:

- `BufferWithViews`
- `Command`
- `ShaderGraphNode`
- DX11 / Direct3D resources
- HLSL compute resources

These require explicit My World wrappers before implementation.

## Shader Graph Contract

Creator-facing patch nodes are not raw shader code.

Shader graph node categories:

- `Source`
- `Field`
- `Transform`
- `Mapping`
- `Material`
- `Memory`
- `Render`
- `Post`
- `Output`

Shader graph compile output:

```json
{
  "language": "glsl|metal|wgsl|hlsl",
  "entry": "fragment|vertex|compute",
  "uniforms": [],
  "textures": [],
  "source": "...",
  "errors": []
}
```

First shader proof:

```text
Value nodes -> uniforms -> hand-written shader -> Output texture -> frame.png
```

## Command Contract

All **authoring** graph mutation goes through commands (the "搭" layer). **Live performance
gestures are not commands** — they are an ephemeral parameter-override stream resolved
per-frame (Port Contract / L5), and become persisted data only when automation recording is
armed (L2, L13).

Minimum commands:

```yaml
- CreateNode
- DeleteNode
- ConnectPorts
- DisconnectPorts
- SetParam
- MoveNode
- RenameNode
- CreateCompound
- ExposePort
- ImportTiXLNodeSpec
- ArmAutomation / DisarmAutomation
- WriteAutomationKeyframe (punch-in)
- ReEnableAutomation (clear a live override)
- DuplicateScore (new version over the same node graph)
```

Each command records:

```json
{
  "id": "cmd_001",
  "actor": "user|ai|importer|test",
  "time": "...",
  "op": "SetParam",
  "args": {},
  "beforeHash": "...",
  "afterHash": "...",
  "result": "ok|failed"
}
```

## Artifact Contract

A proof can emit:

- `frame.png`
- `cook_order.json`
- `node_stats.json`
- `errors.json`
- `graph.normalized.json`
- `shader_source.glsl`
- `command_log.jsonl`

The first acceptable visual runtime proof is not a pretty demo. It is:

```text
Float -> TiXL Remap -> ShaderUniform -> FragmentShader -> RenderOutput -> frame.png
```

## TiXL Porting Admission

Before building a TiXL-derived node:

1. Confirm TiXL full path.
2. Read C# slot declarations.
3. Read `.t3` defaults.
4. Read docs.
5. Trace helper/shader/runtime dependencies.
6. Assign runtime type mapping.
7. Assign porting grade.
8. Write semantic fixture.
9. Implement only if the runtime type exists.

Do not build:

- `Command` nodes before command stream law exists.
- `ShaderGraphNode` nodes before shader graph law exists.
- `BufferWithViews` nodes before point/mesh buffer law exists.
- DX11 / Direct3D nodes (Windows-only TiXL resources) before explicit Metal wrappers exist.

## First Three Runtime Fixtures

Dangerous-node stress tests are tracked in `DANGER_NODE_EXPERIMENTS.md`.

### V1: Visual Value To Shader

```text
graph.value_to_shader.json
-> runtime
-> frame.png + shader_source.glsl + cook_order.json
```

Nodes:

- ConstantFloat
- TiXLRemap
- ShaderUniformFloat
- FragmentShader
- RenderOutput

### G1: Geometry Shell

```text
graph.mesh_to_scene.json
-> runtime
-> frame.png + node_stats.json
```

Nodes:

- CubeMesh
- MaterialColor
- DrawMeshUnlit
- Camera
- RenderOutput

### P1: Point Buffer Shell

```text
graph.points_to_render.json
-> runtime
-> frame.png + point_stats.json
```

Nodes:

- GridPoints
- PointScale
- DrawPoints
- Camera
- RenderOutput

P1 may stay as a design fixture until point buffer ownership is implemented.

## Host & Proof Gate

The host surface is **imgui-node-editor (skin) + a self-built Metal engine** (L7). Vuo is
dropped; earlier Vuo proof gates are void. TiXL remains the semantic donor / design
authority — consult `external/tixl` for node semantics and time/animation design.

A contract is not practically useful until it has a **manipulable body in the native
canvas** plus the regression/handoff guardrail. The proof gate (L6, three-tier acceptance):

- the contract has a visible/manipulable body in the native app — a value changes, an image
  changes, a feedback path holds state — and **the agent verifies it first with its own
  eye+hand** (drive the app, observe the real behavior), not by tests alone;
- headless tests/artifacts remain the authority for command order, resource hazards, and
  failure semantics — as the **regression + handoff guardrail**, not the acceptance gate;
- 柏為's hand is final authority, invoked only for genuinely risky items.
