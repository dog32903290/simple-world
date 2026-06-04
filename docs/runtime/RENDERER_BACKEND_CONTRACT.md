# RendererBackend Contract

RendererBackend answers:

```text
which host/backend can execute this render contract, and which capabilities are real?
```

This contract sits below the creator graph and above concrete hosts such as Vuo,
WebGL2, Metal, DX11, or the current software proof shell.

## Boundary

```text
CreatorGraph -> FrameScheduler -> RenderGraph -> RendererBackend -> FrameOutput
```

`RendererBackend` does not define node vocabulary. It defines the bottom layer
that node contracts can target without lying about host-specific capabilities.

Current backend roles:

```text
softwareProof := deterministic artifact backend for command-to-frame tests
vuoHost := visible body-layer host for connectable Vuo proofs
webgl2ShaderProbe := shader compile pressure only, not a full renderer
nativeInterfaceProof := headless native renderer interface proof
metalNative := future low-latency native GPU backend
```

## Capability Law

Every backend must publish capability truth as data:

```text
maxTextureSize
supports4kOutput
supportsWindowOutput
supportsOffscreenRenderTarget
supportsTextureViews
supportsCommandStream
supportsShaderCompile
shaderLanguages
resourceViewKinds
frameOutputKinds
```

If a capability is missing, the runtime must reject that path with diagnostics.
It must not invent a fake resource, silently degrade into another backend, or
claim TiXL parity from a host proof.

## RenderGraph Law

The backend consumes an explicit pass plan:

```text
passId
domain: frame
inputs
outputs
commands
clearPolicy
readWriteHazards
```

Shader commands enter this layer as `ShaderProgram` packages. A backend may
compile or reject a package, but it must not accept a raw shader string without
language, stage, binding, source-hash, and last-valid failure policy metadata.

The native interface boundary is defined separately:

```text
docs/runtime/NATIVE_RENDERER_BACKEND_INTERFACE.md
docs/runtime/scripts/native_renderer_backend_interface_shell.py
docs/runtime/artifacts/native_renderer_backend_interface/
```

The first pass plan is intentionally small:

```text
mainColorPass:
  clear mainRenderTarget.color/depth
  execute commandStream
  publish frameOutput.preview
```

The pass-level law is defined separately:

```text
docs/runtime/RENDER_GRAPH_CONTRACT.md
docs/runtime/scripts/render_graph_shell.py
docs/runtime/artifacts/render_graph/
```

`RendererBackend` selects and validates a capable host. `RenderGraph` owns pass
order, reads/writes, and resource hazard visibility.

## Resource Lifetime Law

Resources are backend-owned, but graph-visible identity is stable:

```text
resource id
kind
format
resolution
bind flags
owner pass
lifetime: frame | persistent | external
resize policy
dispose policy
```

Resize or format changes must emit a new allocation decision. A missing or
disposed resource invalidates its views instead of allowing downstream fake
success.

## FrameOutput Law

Frame output is not the same as a render target. It is a publish boundary:

```text
previewWindow
fileFrame
syphon
ndi
projection
recording
```

Backends must declare which output kinds they support. 4K output is a
capability check, not a UI wish.

## First Proof

Fixture:

```text
docs/runtime/fixtures/renderer_backend_contract.graph.json
```

Runner:

```text
docs/runtime/scripts/renderer_backend_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/renderer_backend/backend_capabilities.json
docs/runtime/artifacts/renderer_backend/backend_selection.json
docs/runtime/artifacts/renderer_backend/render_pass_plan.json
docs/runtime/artifacts/renderer_backend/resource_lifetime_plan.json
docs/runtime/artifacts/renderer_backend/frame_output_contract.json
docs/runtime/artifacts/renderer_backend/renderer_backend_errors.json
```

This proof is not a Metal backend. It is the contract that prevents Vuo,
WebGL2, software proof, and future native GPU backends from being confused with
each other.
