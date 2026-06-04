# Renderer Proof Contract

This is the first command-to-frame proof for My World / TiXL ports.

## Boundary

For first-pass renderer proof:

```text
RendererProof := MeshPbrDrawCommand -> render trace + nonblack frame artifact
input artifact := docs/runtime/artifacts/material_pbr_scope/mesh_pbr_draw_command.json
support artifact := docs/runtime/artifacts/material_pbr_scope/pbr_binding_errors.json
proof shell := docs/runtime/scripts/native_render_shell.py
output artifacts := native_render_trace.json, native_render_errors.json, frame.ppm, frame_stats.json
```

This is not a GPU backend, not Metal, not DX11, and not TiXL visual parity. It
is the first pressure test that the command artifact has enough structured
information for a renderer lane to consume it without reaching back into GUI
state.

## Node Force

The proof shell answers one question:

```text
can a material-bound mesh command produce an observable frame artifact through an explicit render pipeline?
```

It does not answer:

```text
is the PBR lighting physically correct?
does BufferWithViews match TiXL / SharpDX lifetime semantics?
does InputAssemblerStage bind buffers exactly like TiXL?
does Metal/DX11 render the same pixels?
```

Those stay blocked until a real native GPU backend exists.

## Required Input Shape

`MeshPbrDrawCommand` must include:

```text
ok: true
meshId
selectedMaterialId
shaderSource
vertexShaderEntry: vsMain
pixelShaderEntry: psMain
constantBuffers:
  transforms
  context
  pointLights
  pbr:<material>
shaderResources:
  albedo
  emissive
  roughnessMetallicOcclusion
  normal
  brdfLookup
  prefilteredSpecular
commandOps:
  inputAssembler
  shaderStage
  rasterizer
  outputMerger
  draw
```

If any required field is missing, the proof must write `native_render_errors.json`
and refuse to emit a fake successful frame.

## Proof Pipeline

The shell maps the command into an inspectable software render trace:

```text
loadCommand
validateCommand
bindInputAssembler
bindShaderStage
bindRasterizer
bindOutputMerger
draw
writeFrame
measureFrame
```

The frame artifact is deliberately simple:

```text
frame.ppm
```

It is a deterministic PPM raster with a material-colored triangle/diamond-like
shape. It proves command-to-frame plumbing and nonblack output, not PBR visual
correctness.

## Failure Behavior

### Missing Command

```text
native_render.command_read_failed
```

### Invalid Command

```text
native_render.command_not_ok
native_render.missing_field
native_render.invalid_shader_stage
native_render.invalid_command_ops
native_render.incomplete_pbr_binding
```

### Black Frame

If generated frame stats show no nonblack pixels:

```text
native_render.black_frame
```

The shell must still write trace/errors/stats so the failure can be inspected.

## Risk Closure Matrix

```text
closed now:
  MeshPbrDrawCommand can be consumed by a render proof shell
  renderer trace is deterministic and inspectable
  frame artifact is generated from command data
  black frame is rejected by stats

blocked until native GPU renderer:
  TiXL MeshBuffers / BufferWithViews parity
  exact PbrMaterial constant-buffer layout parity
  real shader compilation and GPU draw call parity
  lighting, shadow, cubemap prefilter, and DDS environment visual parity
```

## Evidence

Current proof:

```text
docs/runtime/RENDERER_PROOF_CONTRACT.md
docs/runtime/scripts/native_render_shell.py
docs/runtime/artifacts/native_renderer/native_render_trace.json
docs/runtime/artifacts/native_renderer/native_render_errors.json
docs/runtime/artifacts/native_renderer/frame.ppm
docs/runtime/artifacts/native_renderer/frame_stats.json
tests/native_render_contract.test.js
```

This proof is now consumed by the wider native lane:

```text
docs/runtime/NATIVE_RUNTIME_LANE.md
docs/runtime/fixtures/native_runtime_lane.graph.json
docs/runtime/scripts/native_runtime_lane.py
docs/runtime/artifacts/native_runtime_lane/
tests/native_runtime_lane.test.js
```

Renderer backend law:

```text
docs/runtime/RENDERER_BACKEND_CONTRACT.md
docs/runtime/scripts/renderer_backend_shell.py
docs/runtime/artifacts/renderer_backend/
tests/renderer_backend_contract.test.js
```

`native_render_shell.py` is the current `softwareProof` backend role. It must
not be confused with Vuo, WebGL2 shader compile pressure, or the future native
Metal/DX11 backend.
