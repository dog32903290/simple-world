# ShaderUniformBinding Contract

ShaderUniformBinding answers:

```text
which live control value is allowed to become which shader uniform for this frame?
```

It sits between value/control nodes and `NativeRendererBackend` frame input.

## Boundary

```text
ValueSource -> ShaderUniformBinding -> RenderFrameInput -> NativeRendererBackend
```

This contract borrows the useful part of my-world's
`ShaderPreviewInputBridge`: a uniform update is proof-visible evidence, not a
private UI field.

## Donor Evidence

```text
/Users/chenbaiwei/Projects/my-world/source/render/ShaderPreviewInputBridge.h
/Users/chenbaiwei/Projects/my-world/source/render/ShaderPreviewInputBridge.cpp
```

The donor behavior we keep:

```text
bindingId is required
uniformName is required
sampleCounter is recorded
u_loudness may become RenderFrameInput.loudness
invalid snapshots do not mutate frame input
```

## Binding Law

Each uniform update declares:

```text
bindingId
uniformName
value
sampleCounter
sourceNodeId
sourcePort
targetProgramId
```

The binding id is stable graph identity. The uniform name is backend-facing
shader identity. They are not the same thing.

## Frame Input Law

Only explicitly recognized uniforms may affect special frame fields:

```text
u_loudness -> RenderFrameInput.loudness
```

Other uniforms remain shader bindings. They must not silently become scheduler
time, frame index, resolution, or render state.

## Failure Law

Invalid uniform evidence fails before it reaches the backend:

```text
missing bindingId -> error
missing uniformName -> error
missing source value -> error
invalid numeric value -> error
```

When a snapshot is invalid, `RenderFrameInput` uses its fallback values.

## First Proof

Fixture:

```text
docs/runtime/fixtures/shader_uniform_binding.graph.json
```

Runner:

```text
docs/runtime/scripts/shader_uniform_binding_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/shader_uniform_binding/shader_uniform_snapshot.json
docs/runtime/artifacts/shader_uniform_binding/shader_uniform_bindings.json
docs/runtime/artifacts/shader_uniform_binding/render_frame_input.json
docs/runtime/artifacts/shader_uniform_binding/uniform_binding_trace.json
docs/runtime/artifacts/shader_uniform_binding/shader_uniform_binding_errors.json
```

This proof does not implement audio analysis. It proves the narrow live-control
bridge that analyzer, MIDI, OSC, sliders, and AI worker updates can later feed.
