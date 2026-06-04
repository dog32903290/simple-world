# Dangerous Node Experiments

Version: `0.1`

Purpose: pressure-test whether `MY_WORLD_RUNTIME_CONTRACT.md` can carry TiXL C/D grade nodes before we attempt broad implementation.

The dangerous TiXL classes are:

- `ShaderGraphNode`
- `Command`
- `BufferWithViews`
- Direct3D / DX11 resource nodes

These should not be ported as ordinary Vuo C nodes first. Each needs a small experiment that can fail loudly.

## Pass / Fail Rule

An experiment passes only if it produces:

- a graph fixture
- a runtime interpretation rule
- an observable artifact or diagnostic
- a failure-mode record

It fails usefully if it proves the contract cannot express a required concept.

## E0: Contract Shape Check

Question:

Can the runtime graph express a dangerous node without hiding its true resource/state boundary?

Fixture:

```text
docs/runtime/fixtures/value_to_shader.graph.json
```

Pass criteria:

- nodes have `family`, `domain`, `params`, `statePolicy`
- dangerous resource nodes declare `externalResource`
- `sourceEvidence` points back to TiXL paths when applicable
- output artifact list is explicit

Failure meaning:

If this fixture needs ad hoc fields immediately, the schema is too narrow.

## E1: ShaderGraphNode Shell

TiXL donor examples:

- `Lib.field.generate.sdf.SphereSDF`
- `Lib.field.generate.sdf.BoxSDF`
- `Lib.field.combine.CombineSDF`
- `Lib.field.render.RaymarchField`

Question:

Can My World represent shader graph fragments as composable nodes before choosing GLSL/Metal/WGSL/HLSL backend?

Minimal graph:

```text
SphereSDF -> TransformField -> RaymarchField -> RenderOutput
```

Required runtime types:

- `ShaderGraph`
- `Texture2D`
- `Float`
- `Vec3`
- `Color`

Node shell contract:

```yaml
type: tixl.field.generate.sdf.SphereSDF
family: shader
domain: frame
statePolicy: stateless
outputs:
  field: ShaderGraph
```

Pass criteria:

- runtime can build a shader graph AST or intermediate representation
- generated shader source can be dumped as `shader_source.glsl`
- compile failure keeps last valid shader
- missing backend produces structured error, not crash

First artifact:

```text
shader_source.glsl
errors.json
```

Current proof:

```text
docs/runtime/fixtures/sphere_sdf_raymarch.graph.json
docs/runtime/scripts/compile_shadergraph_shell.py
docs/runtime/artifacts/e1_shadergraph/shader_source.glsl
docs/runtime/artifacts/e1_shadergraph/errors.json
docs/runtime/artifacts/e1_shadergraph/cook_order.json
docs/runtime/artifacts/e1_shadergraph/webgl_compile.json
docs/runtime/artifacts/e1_shadergraph/webgl_compile_batch.json
docs/runtime/artifacts/e1_shadergraph/webgl_server_smoke.json
tests/runtime_shadergraph_shell.test.js
tests/runtime_shadergraph_webgl_compile.test.js
tests/runtime_shadergraph_webgl_batch.test.js
tests/runtime_shadergraph_webgl_server.test.js
```

Result as of 2026-06-04:

- `SphereSDF -> RaymarchField` can be expressed as a `ShaderGraph` fixture.
- The shell compiler writes deterministic shader source and cook order.
- The generated shader shell compiles as a WebGL2 fragment shader in headless Chrome.
- WebGL compile caught one useful portability bug: GLSL ES float comparisons need float literals such as `300.0`, not integer literals such as `300`.
- Reusing one Chrome/WebGL context separates cold-start cost from compile cost. The first measured compile was about 325ms; the second compile in the same context was about 3ms on 2026-06-04.
- `serve_shader_webgl.js` keeps the WebGL context alive behind a JSONL protocol. In the smoke artifact, two compile requests took about 419ms total; the second compile took about 2.2ms.
- Unsupported or malformed shader graph input is routed to `errors.json` instead of hidden runtime failure.
- This does not prove visual correctness, camera/depth, PBR, or TiXL DX11 resource parity yet.

Then:

```text
frame.png
```

Failure meaning:

If `ShaderGraph` becomes just a string blob, the contract is too weak. It must preserve ports, uniforms, source evidence, and dependency order.

## E2: Command Stream Shell

TiXL donor examples:

- `Lib.render.basic.Layer2d`
- `Lib.render.transform.Group`
- `Lib.render.camera.Camera`
- `Lib.mesh.draw.DrawMeshUnlit`
- `Lib.image.fx.feedback.AfterGlow`

Question:

Can My World express TiXL `Command` as a render command stream without copying TiXL's internal app runtime?

Minimal graph:

```text
CubeMesh -> MaterialColor -> DrawMeshUnlit -> Camera -> RenderOutput
```

Required runtime types:

- `Command`
- `Mesh`
- `Color`
- `Scene`
- `Texture2D`

Command shell contract:

```yaml
type: my.render.command
family: scene
domain: frame
statePolicy: frameState
outputs:
  command: Command
```

Pass criteria:

- commands can be ordered deterministically
- camera/render target can be represented outside node-local state
- `cook_order.json` shows command composition order
- failed command marks downstream render invalid or last-valid

First artifact:

```text
cook_order.json
command_stream.json
errors.json
```

Current proof:

```text
docs/runtime/COMMAND_STREAM_CONTRACT.md
docs/runtime/MESH_DRAW_CONTRACT.md
docs/runtime/MATERIAL_PBR_CONTRACT.md
docs/runtime/fixtures/material_pbr_scope.graph.json
docs/runtime/scripts/material_pbr_scope_shell.py
docs/runtime/artifacts/material_pbr_scope/material_scope_trace.json
docs/runtime/artifacts/material_pbr_scope/mesh_pbr_draw_command.json
docs/runtime/artifacts/material_pbr_scope/pbr_binding_errors.json
docs/runtime/RENDERER_PROOF_CONTRACT.md
docs/runtime/scripts/native_render_shell.py
docs/runtime/artifacts/native_renderer/native_render_trace.json
docs/runtime/artifacts/native_renderer/native_render_errors.json
docs/runtime/artifacts/native_renderer/frame.ppm
docs/runtime/artifacts/native_renderer/frame_stats.json
docs/runtime/NATIVE_RUNTIME_LANE.md
docs/runtime/fixtures/native_runtime_lane.graph.json
docs/runtime/scripts/native_runtime_lane.py
docs/runtime/scripts/native_command_stream_api.py
docs/runtime/scripts/native_resource_api.py
docs/runtime/artifacts/native_runtime_lane/native_runtime_lane_trace.json
docs/runtime/artifacts/native_runtime_lane/resource_registry.json
docs/runtime/artifacts/native_runtime_lane/texture_views.json
tests/command_stream_contract.test.js
tests/mesh_draw_contract.test.js
tests/material_pbr_contract.test.js
tests/native_render_contract.test.js
tests/native_runtime_lane.test.js
tests/native_command_stream_api.test.js
tests/native_resource_api.test.js
```

Result as of 2026-06-04:

- TiXL `Command` is represented as an ordered frame operation with
  `prepare -> update -> restore` phases.
- `Lib.flow.Execute` is the anchor for command ordering.
- Render stage nodes such as `OutputMergerStage`, `Rasterizer`, and
  `SetPixelAndVertexShaderStage` are state-mutating commands, not ordinary
  values.
- `Draw` must fail loudly when vertex or pixel shader state is missing.
- `ComputeShaderStage` is included even though it lives under `Types.Gfx`,
  because it outputs `Command` and participates in the same command stream.
- `DrawMeshUnlit` is the first mesh-to-command compound contract. Its child
  route exposes mesh buffer validation, input assembler, shader stage,
  rasterizer/output merger, draw, and execute instead of hiding them inside one
  opaque node.
- `Material / SetMaterial / PBR binding` is now the next command lane. It
  models scoped `context.PbrMaterial`, `context.Materials`, and
  `PrefilteredSpecular` context texture mutation instead of treating material as
  a plain color input.
- `material_pbr_scope_shell.py` turns the material graph fixture into three
  inspectable artifacts: material scope trace, mesh PBR draw command, and
  nonfatal PBR binding diagnostics.
- `native_render_shell.py` consumes the mesh PBR draw command and emits the
  first deterministic command-to-frame proof: render trace, errors, PPM frame,
  and frame stats. This is still a software proof shell, not GPU parity.
- Known TiXL restore risks are marked instead of copied blindly: UAV restore
  coverage, scissor handling, and asymmetric shader-stage restore details need
  native backend proof.

Then:

```text
frame.png
```

Failure meaning:

If every render node needs hidden global state, the contract must add explicit render context/resource slots.

## E3: BufferWithViews / PointBuffer Shell

TiXL donor examples:

- `Lib.point.generate.GridPoints`
- `Lib.point.modify.MapPointAttributes`
- `Lib.point.draw.DrawPoints`
- `Lib.mesh.modify.MeshVerticesToPoints`

Question:

Can My World represent point/attribute buffers without inheriting DX11 view objects?

Minimal graph:

```text
GridPoints -> MapPointAttributes -> DrawPoints -> RenderOutput
```

Required runtime types:

- `PointBuffer`
- `Float`
- `Vec3`
- `Color`
- `Command` or `Scene`

PointBuffer shell contract:

```yaml
type: my.point.buffer
family: point
domain: frame
statePolicy: externalResource
attributes:
  position: Vec3
  color: Color
  scale: Float
  f1: Float
  f2: Float
```

Pass criteria:

- point count and attributes appear in `point_stats.json`
- CPU fallback can validate first 10 points
- GPU backend can be absent and still produce diagnostic
- draw node consumes `PointBuffer` without knowing TiXL DX11 view types

First artifact:

```text
point_stats.json
buffer_layout.json
errors.json
```

Then:

```text
frame.png
```

Failure meaning:

If attributes are not explicit, later point nodes will become incompatible black boxes.

## E4: DX11 Adapter Refusal Test

TiXL donor examples:

- `Lib.render._dx11.api.*`
- `_ExecuteBloomPasses`
- `_ExecuteFastBlurPasses`
- DXGI / SRV / UAV / RTV / SamplerState nodes

Question:

Can the contract reject nonportable DX11 nodes cleanly while preserving their documentation?

Minimal graph:

```text
DX11ResourceNode -> RenderOutput
```

Pass criteria:

- importer marks node `unsupported`
- graph still loads
- UI can show missing backend reason
- no fake output is produced
- suggested replacement path is recorded

Artifact:

```text
errors.json
unsupported_nodes.json
```

Failure meaning:

If unsupported DX11 nodes silently degrade into fake placeholders, the importer is unsafe.

## E5: Image Shader Middle Ground

TiXL donor examples:

- `Lib.image.generate.basic.LinearGradient`
- `Lib.image.generate.basic.RadialGradient`
- `Lib.image.use.BlendImages`
- `Lib.image.fx.blur.Blur`
- `Lib.image.transform.Crop`

Question:

Can Vuo-hosted image nodes and My World shader contract coexist?

Minimal graph:

```text
LinearGradient -> Blur -> BlendImages -> RenderOutput
```

Pass criteria:

- simple image nodes may run in Vuo as prototype
- shader-heavy image nodes still declare `Texture2D` resource ownership
- generated artifact names match My World artifact contract

Failure meaning:

If Vuo-specific image behavior leaks into My World graph schema, the host/prototype boundary is wrong.

## Experiment Order

Do this order:

1. E0 contract shape check
2. E1 ShaderGraphNode shell
3. E3 PointBuffer shell
4. E2 Command stream shell
5. E4 DX11 refusal
6. E5 image middle ground

Reason:

Shader graph and point buffer define the missing runtime types. Command stream depends on those. DX11 refusal protects the importer. Image middle ground checks that Vuo remains a useful host without becoming the law.

## Minimal Acceptance Matrix

| experiment | contract under test | first artifact | pass meaning |
|---|---|---|---|
| E0 | graph schema | schema check | graph can express dangerous shells |
| E1 | `ShaderGraph` | `shader_source.glsl` | shader nodes can compose before backend |
| E2 | `Command` | `command_stream.json` | render chain is explicit |
| E3 | `PointBuffer` | `buffer_layout.json` | point attributes are real schema |
| E4 | unsupported DX11 | `unsupported_nodes.json` | importer can refuse safely |
| E5 | host boundary | `frame.png` | Vuo prototype stays bounded |

## Current Verdict

The runtime contract is plausible, but not proven for dangerous nodes yet.

The fastest meaningful test is E1:

```text
SphereSDF -> RaymarchField -> shader_source.glsl/errors.json
```

Do not start with full render output. Start with generated shader source and structured diagnostics. If that shape holds, move to frame rendering.
