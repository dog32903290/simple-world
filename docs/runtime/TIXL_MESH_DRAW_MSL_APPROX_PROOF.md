# TiXL Mesh Draw MSL Approximation Proof

TixlMeshDrawMslApproxProof answers:

```text
can the fixed PbrVertex/FaceIndices layout feed an explicit MSL approximation
through real Metal compile, offscreen mesh draw, and RGBA readback?
```

Runtime path:

```text
tixl_mesh_draw_msl_approx.graph.json -> tixl_mesh_draw_msl_approx_shell.py -> ObjC++ Metal probe -> result/trace/errors/frame stats/MSL artifacts
```

## Boundary

This is an approximation proof for one tiny mesh. It consumes the previous
layout artifact, creates packed Metal buffers, compiles explicit MSL, draws with
`vertex_id` plus `FaceIndices`, and reads back pixels.

It is not TiXL PBR parity, not TiXL runtime parity, not HLSL-to-MSL translation,
not native DrawMesh runtime, and not renderer/backend replacement. The explicit
MSL source is handwritten for this lane and intentionally uses `packed_float3`,
`packed_float2`, and `packed_int3` to keep the 80-byte `PbrVertex` and 12-byte
`FaceIndices` path honest.

The shell reports:

```text
actualCompilerRan: true only after the Metal compiler ran
actualMetalRan: true only after render/readback ran
mslApproximationRendered: true only on successful frame readback
layoutArtifactConsumed: true only after the layout artifact was validated
mslApproxBufferPackingObserved: true only for this approximation probe
tixlRuntimeParity: false
hlslToMslTranslation: false
pbrVisualCorrectness: false
drawMeshRuntime: false
```

## Inputs

The fixture declares:

```text
docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json
3 PbrVertex records with position, normal, tangent, bitangent, texCoord,
texCoord2, selected, colorRGB
1 FaceIndices record
```

The layout artifact must be:

```text
kind: TixlMeshDrawBufferLayoutProof
ok: true
status: summarized_tixl_mesh_draw_buffer_layout
PbrVertex stride: 80 bytes
FaceIndices stride: 12 bytes
claims.metalBufferPackingParity: false
```

## Blocked Cases

If the layout artifact is missing or invalid, the shell exits `1`, writes stable
result/trace/errors artifacts, and does not emit a fake frame.

If the fixture has missing mesh fields or invalid MSL source, the shell exits
`1` with stable validation or compiler diagnostics and no frame success.

If Metal is unavailable, the shell exits `1` with
`blocked_metal_device_unavailable` and no fake frame artifact.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_msl_approx.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_msl_approx_shell.py <tixl_mesh_draw_msl_approx.graph.json> <out_dir>
```

Probe source:

```text
docs/runtime/native/tixl_mesh_draw_msl_approx_probe.mm
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_msl_approx/generated_explicit_msl_approx.metal
docs/runtime/artifacts/tixl_mesh_draw_msl_approx/mesh_payload.json
docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json
docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_trace.json
docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_errors.json
docs/runtime/artifacts/tixl_mesh_draw_msl_approx/frame_stats.json
```
