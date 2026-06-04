# My World Runtime Contract

Version: `0.1`

This is the first native runtime law for My World. It exists so TiXL/Vuo/TD-like node vocabulary has one place to land without inheriting any single host application's hidden assumptions.

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

- The graph is the source of truth.
- UI, AI workers, importers, and scripts mutate the graph only through commands.
- Runtime state must be inspectable from text, not trapped inside GUI objects.
- A node is not accepted because it compiles. It must produce evidence.
- TiXL is semantic donor, not runtime law.
- Vuo is first host/prototype surface, not final runtime law.

## Four Graphs

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

## Runtime Type System

Core value types:

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
| `Curve` | value curve | custom | no direct first-pass law |
| `Texture2D` | image/texture | `VuoImage` | host-specific resource boundary |
| `Scene` | renderable scene graph | `VuoSceneObject` candidate | needs My World law |
| `Mesh` | geometry | `VuoMesh` candidate | needs ownership/resource law |
| `PointBuffer` | point cloud / particle buffer | custom | TiXL `BufferWithViews` does not port directly |
| `ShaderGraph` | generated shader function graph | custom | TiXL `ShaderGraphNode` donor only |
| `Command` | scene/render command stream | custom | cannot be A/B direct |
| `AudioFrame` | audio buffer slice | native audio type | later low-latency law |
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
| `audio` | audio callback | analyzer facts, audio output |
| `event` | trigger/message | MIDI, OSC, button, command |
| `async` | background task | file/network/AI |

## Main Clock Contract

`FrameScheduler` is the runtime owner of visual time. It supplies the shared
frame context that visual nodes read during a cook:

```text
frameIndex
time
deltaTime
```

`my_MainClock` answers:

```text
what is the graph-level frame pulse for this visual runtime?
```

In Vuo proofs, it is a host adapter:

```text
Fire on Display Refresh -> my_MainClock -> renderTick
```

Rules:

- user-facing proof graphs should route live visual cooking through
  `my_MainClock`, not direct display-refresh cables scattered across the graph
- `renderTick` carries frame pressure only
- `FrameIndex`, `Time`, and `DeltaTime` are semantic clock data
- audio clock, MIDI clock, Ableton Link, and final native scheduler remain
  separate future clock sources

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

All graph mutation goes through commands.

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
- DX11 nodes in Vuo first pass.

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

## Current Decision

Use Vuo to prototype A/B grade value and simple image/control vocabulary.

Use My World runtime contract for C/D grade TiXL semantics:

- shader graph
- SDF/raymarch
- particle buffers
- render command stream
- mesh/point scene graph

This avoids wasting time building Vuo nodes that compile but cannot carry TiXL's real runtime force.

## Contract-To-Vuo Proof Gate

New runtime contracts should not stay as headless text unless Vuo truly cannot
expose the force being tested.

For each new contract, require one of:

- an exact `my_<ExactTiXLNodeName>` Vuo custom node,
- a small Vuo proof composition that wires several related nodes together,
- or an explicit boundary note explaining why this contract cannot be exposed in
  Vuo yet.

The Vuo proof does not need to prove native GPU parity. It must prove that the
contract has a visible or manipulable body in the current Vuo canvas: a value
changes, an image changes, a feedback path holds state, a material adapter
affects the render, or a bounded proof node makes the contract's result visible.

Headless tests remain the authority for command order, resource hazards, and
failure semantics. Vuo remains the current canvas/body-layer trial. A contract is
not considered practically useful until those two evidence layers are linked or
the missing Vuo link is named.
