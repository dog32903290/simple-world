# TiXL Mesh Draw Full PBR Resource Binding Proof

This proof establishes one positive, source-backed resource binding ledger for
the TiXL Mesh Draw / PBR lane:

```text
tixl_mesh_draw_full_pbr_resource_binding.graph.json
-> tixl_mesh_draw_full_pbr_resource_binding_shell.py
-> deterministic Metal sentinel probe
-> result/trace/errors artifacts
```

## Positive Claim

`fullPbrResourceBinding: true`

The proof consumes the existing source audit and partial proof artifacts, then
runs a generated Metal compute probe that binds:

```text
t0 PbrVertices -> Metal buffer(0)
t1 FaceIndices -> Metal buffer(1)
b0 Transforms -> Metal buffer(2)
b1 Params -> Metal buffer(3)
b2 FogParams -> Metal buffer(4)
b3 PointLights -> Metal buffer(5)
b4 PbrParams -> Metal buffer(6)
b5 Params shadergraph duplicate params -> Metal buffer(7)
t2 BaseColorMap -> Metal texture(2)
t3 EmissiveColorMap -> Metal texture(3)
t4 RSMOMap -> Metal texture(4)
t5 NormalMap -> Metal texture(5)
t6 PrefilteredSpecular TextureCube -> Metal texture(6)
t7 BRDFLookup -> Metal texture(7)
s0 WrappedSampler -> Metal sampler(0)
s1 ClampedSampler -> Metal sampler(1)
```

The older texture/sampler proof remains a narrow subset for only t2, t7, s0,
and s1. This proof is where t3, t4, t5, and t6 become bound.

The current fixture's shadergraph resource expansion remains empty at t8:

```text
t8ShadergraphResources.status: proven_empty_for_current_fixture
```

## Consumed Artifacts

The shell validates these checked-in artifacts before running Metal:

```text
docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json
docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_result.json
docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json
docs/runtime/artifacts/tixl_mesh_draw_b5_native_packing/tixl_mesh_draw_b5_native_packing_result.json
docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json
docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json
```

It rejects widened input claims before the probe.

## Boundary

This proof is not backend replacement:

```text
backendReplacementReady: false
```

It is not the explicit adapter parity proof:

```text
explicitAdapterProof: false
```

It is not HLSL-to-MSL translation:

```text
hlslToMslTranslation: false
```

It is not TiXL runtime parity:

```text
tixlRuntimeParity: false
```

It does not complete native GPU parity:

```text
nativeGpuParityComplete: false
```

It is not PBR visual correctness:

```text
pbrVisualCorrectness: false
```

## Artifacts

```text
docs/runtime/fixtures/tixl_mesh_draw_full_pbr_resource_binding.graph.json
docs/runtime/scripts/tixl_mesh_draw_full_pbr_resource_binding_shell.py
docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_trace.json
docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_errors.json
docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/generated_full_pbr_resource_binding_probe.metal
```
