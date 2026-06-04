# TiXL Mesh Draw B5 Native Packing Proof

TixlMeshDrawB5NativePackingProof answers one bounded question:

Can the source-backed b5 shadergraph `Params` expansion be packed into the
candidate native Metal constant-buffer binding without widening the runtime
closure claims?

## Proven Layout

This lane consumes the b5 shadergraph params expansion artifact and proves only
this concrete layout:

```text
b5 Params -> Metal buffer(7)
SphereSDF_nG1CBDm_Center float3 byte offset 0 value [-1.4845504, 0, 0.54366434]
SphereSDF_nG1CBDm_Radius float byte offset 12 value 0.5
sizeBytes 16
```

The Metal probe binds host b5 bytes at `buffer(7)`, reads
`SphereSDF_nG1CBDm_Center.x/y/z` and `SphereSDF_nG1CBDm_Radius`, and compares
the returned bit patterns against the expected sentinel values.

## Input Gates

The shell fails before the probe unless:

- the b5 expansion artifact is `ok: true` with status
  `expanded_b5_shadergraph_params_source_backed`
- the expansion claims b5 is source-backed and expanded, but does not already
  claim native b5 packing, adapter completion, texture/sampler mapping, PBR
  resource binding, backend replacement, HLSL/MSL translation, TiXL parity, or
  visual correctness
- the expansion proofBoundary keeps `nativeB5PackingProven: false`
- the fields, names, types, offsets, float values, and size match the exact
  SphereSDF layout above
- the layout artifact still maps b5 to candidate Metal `buffer(7)` without
  claiming backend binding
- the PointLights/b5 artifact still proves b3 PointLights and leaves b5 as the
  shadergraph params lane boundary

## Claims

Success may set:

```text
actualMetalB5PackingProbeRan: true
b5NativePackingProven: true
```

Success must keep:

```text
constantBufferAdapterComplete: false
textureSamplerMapping: false
fullPbrResourceBinding: false
backendReplacementReady: false
hlslToMslTranslation: false
tixlRuntimeParity: false
pbrVisualCorrectness: false
```

This proof does not prove the combined b0-b5 adapter. It only closes the next
runtime closure lane
`prove_native_b5_packing_from_source_backed_shadergraph_params`.

## Artifacts

```text
docs/runtime/fixtures/tixl_mesh_draw_b5_native_packing.graph.json
docs/runtime/scripts/tixl_mesh_draw_b5_native_packing_shell.py <tixl_mesh_draw_b5_native_packing.graph.json> <out_dir>
docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_result.json
docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_trace.json
docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_errors.json
docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/generated_b5_shadergraph_params_packing_probe.metal
```
