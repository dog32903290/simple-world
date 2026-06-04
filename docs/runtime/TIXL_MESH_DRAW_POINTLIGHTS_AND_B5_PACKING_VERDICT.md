# TiXL Mesh Draw PointLights And B5 Packing Verdict

TixlMeshDrawPointLightsAndB5PackingVerdict answers:

```text
after the partial native constant-buffer packing proof for b0/b1/b2/b4, can
the remaining b3 PointLights layout be proven with a real Metal probe, and can
b5 duplicate Params be closed without inventing shadergraph fields?
```

Status: b3 PointLights native packing can be proven only when the shell
compiles and runs the generated Metal compute probe, binds host bytes at Metal
`buffer(5)`, and reads back sentinel values for `PointLight[8]` stride,
member offsets, and `ActiveLightCount`. b5 duplicate Params remains blocked
until shadergraph parameter expansion produces concrete source-backed fields.

## Boundary

This is a closure verdict for the remaining constant-buffer packing lane. It
consumes the prior native packing proof for b0/b1/b2/b4 and the b0-b5 layout
artifact. It is not texture/sampler mapping, not full PBR resource binding,
not backend replacement, not HLSL-to-MSL translation, not TiXL runtime parity,
and not PBR visual correctness.

The shell fails closed if the prior native packing artifact is missing, stale,
or did not prove b0/b1/b2/b4 with `actualMetalPackingProbeRan: true`. It also
fails closed if the layout artifact gives b5 invented fields or widens
textures, samplers, PBR, backend, parity, or translation claims.

## b3 PointLights

Source evidence:

```text
external/tixl/Operators/Lib/Assets/shaders/shared/point-light.hlsl
external/tixl/Core/Rendering/PointLightStack.cs
```

The proven adapter packing is:

```text
b3 PointLights -> Metal buffer(5), size 400
PointLight array length 8
PointLight array stride 48
Lights[0].position 0
Lights[0].intensity 12
Lights[0].color 16
Lights[0].range 32
Lights[0].decay 36
Lights[0].__padding 40
Lights[1].position 48
Lights[7].position 336
ActiveLightCount 384
```

If the Metal compile/run/readback cannot be performed, the verdict status is
`blocked_needs_pointlight_native_probe` and `b3PointLightsPackingProven` stays
`false`.

## b5 Duplicate Params

b5 is the duplicate shadergraph `Params` cbuffer:

```text
b5 Params
fields []
status b5_packing_blocked_until_shadergraph_param_expansion
```

This is not a failure of b3. It is the remaining boundary: the source audit and
layout artifact currently have no concrete b5 fields, so the adapter must not
invent native packing for b5.

The claim flags stay conservative:

```text
priorNativePackingArtifactConsumed: true
b3PointLightsPackingProven: true or false by actual probe result
b5DuplicateParamsPackingProven: false
b5RequiresShadergraphParamExpansion: true
constantBufferAdapterComplete: false
textureSamplerMapping: false
fullPbrResourceBinding: false
backendReplacementReady: false
hlslToMslTranslation: false
```

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_pointlights_and_b5_packing.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_pointlights_and_b5_packing_shell.py <tixl_mesh_draw_pointlights_and_b5_packing.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json
docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_trace.json
docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_errors.json
docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/generated_pointlights_b3_packing_probe.metal
```
