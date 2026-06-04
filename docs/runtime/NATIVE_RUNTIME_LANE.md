# Native Runtime Lane

NativeRuntimeLane := graph fixture -> command stream -> render target resources -> texture views -> renderer artifact.

This is the first replayable native lane after the Vuo host proofs. It exists so
the system stops treating Vuo as the bottom of the graph.

## Boundary

This is not a Metal or DX11 backend. It does not allocate real GPU textures and
does not prove driver-level synchronization, native shader compilation, or
TiXL `BufferWithViews` lifetime parity.

Renderer backend capability truth is defined separately:

```text
docs/runtime/RENDERER_BACKEND_CONTRACT.md
```

The current lane consumes the `softwareProof` backend role. Vuo remains a
visible host proof, WebGL2 remains shader compile pressure, and future native
Metal/DX11 work must implement the same backend contract instead of changing
creator graph semantics.

It does prove the first useful spine:

```text
CommandStream
-> RenderTarget resource registry
-> TextureView identity
-> native_render_shell.py
-> frame artifact + diagnostics
```

## Runtime Entry

```text
docs/runtime/scripts/native_runtime_lane.py
```

Input fixture:

```text
docs/runtime/fixtures/native_runtime_lane.graph.json
```

Artifacts:

```text
docs/runtime/artifacts/native_runtime_lane/native_runtime_lane_trace.json
docs/runtime/artifacts/native_runtime_lane/native_runtime_lane_errors.json
docs/runtime/artifacts/native_runtime_lane/resource_registry.json
docs/runtime/artifacts/native_runtime_lane/texture_views.json
docs/runtime/artifacts/native_runtime_lane/material_pbr_scope/
docs/runtime/artifacts/native_runtime_lane/native_renderer/
```

Resource/view API:

```text
docs/runtime/scripts/native_resource_api.py
```

The lane uses this API for `Texture2DHandle`, `TextureResourceRegistry`, and
`TextureViewHandle` creation instead of embedding TextureView rules in the lane
runner.

Command stream API:

```text
docs/runtime/scripts/native_command_stream_api.py
```

The lane uses this API to bind `rt.color.rtv` and `rt.depth.dsv` through an
OutputMerger command, including default blend/depth state. It also binds
`topology`, vertex buffer identity, and index buffer identity through an
InputAssembler command, then binds
`vertexShaderEntry`, `pixelShaderEntry`, constant buffers, shader resources, and
sampler state through a ShaderStage command. It derives a viewport from the
RenderTarget resolution and binds it through a Rasterizer command before Draw
executes. Draw reads the command state, then the lane restores input-assembler,
shader, rasterizer, and render target state before the software frame artifact
is generated.

## Node Contracts

CommandStream answers: which ordered render command is accepted for this frame?

InputAssembler answers: which topology and mesh-buffer identities are currently
bound for Draw?

ShaderStage answers: which VS/PS entries, constant buffers, SRVs, and samplers
are currently bound for Draw?

Rasterizer answers: which viewport and rasterizer state are currently bound for
Draw?

OutputMerger answers: which render target, depth target, blend state, and depth
state are currently bound for Draw?

RenderTarget answers: which named Texture2D resources exist after the command
is rendered at the requested resolution?

TextureView identity answers: which access views are valid for each texture's
declared bind flags?

NativeRenderer answers: can the accepted command produce a nonblack frame
artifact with diagnostics?

## Failure Rule

No stage may fabricate downstream resources when an upstream stage fails. If
Material/PBR command generation fails, `resource_registry.json` remains empty
and the lane exits nonzero.
