# TiXL Mesh Draw Backend Replacement Gate Proof

TixlMeshDrawBackendReplacementGateProof answers:

```text
has the backend replacement gate been evaluated after native backend integration
and runtime equivalence exist?
```

This is the bounded replacement readiness proof for the TiXL Mesh Draw / PBR
lane. It consumes the positive full PBR resource binding proof, explicit adapter
proof, and native Metal backend integration proof.

## Boundary

The proof consumes the existing bounded artifacts:

```text
docs/runtime/artifacts/native_render_pipeline
docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_full_pbr_resource_binding/tixl_mesh_draw_full_pbr_resource_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_explicit_adapter_proof/tixl_mesh_draw_explicit_adapter_result.json
docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/tixl_mesh_draw_native_metal_backend_integration_result.json
docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json
docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json
docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json
```

It proves only bounded replacement readiness for this lane:

```text
backendReplacementGateEvaluated: true
replacementBlockedBecauseFullBindingMissing: false
replacementBlockedBecauseAdapterProofMissing: false
nativeMetalBackendIntegrationComplete: true
boundedNativeBackendRemains: false
backendReplacementReady: true
fullPbrResourceBinding: true
explicitAdapterProofPresent: true
hlslToMslTranslation: false
tixlRuntimeParity: true
nativeGpuParityComplete: true
```

If any consumed artifact widens a readiness, parity, or translation claim, the
gate fails. `hlslToMslTranslation` stays false because the proven route is the
selected handwritten explicit MSL adapter, not a generic translator.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_backend_replacement_gate.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_backend_replacement_gate_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_result.json
docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_trace.json
docs/runtime/artifacts/tixl_mesh_draw_backend_replacement_gate/tixl_mesh_draw_backend_replacement_gate_errors.json
```
