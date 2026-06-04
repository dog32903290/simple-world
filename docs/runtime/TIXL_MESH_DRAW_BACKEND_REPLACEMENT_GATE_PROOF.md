# TiXL Mesh Draw Backend Replacement Gate Proof

TixlMeshDrawBackendReplacementGateProof answers:

```text
has the backend replacement gate been evaluated, while keeping replacement
blocked until full PBR resource binding and an explicit adapter proof exist?
```

This is a guarded negative proof. It does not replace the native backend, does
not claim full PBR resource binding, and does not claim native GPU or TiXL
runtime parity.

## Boundary

The proof consumes the existing bounded artifacts:

```text
docs/runtime/artifacts/native_render_pipeline
docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json
docs/runtime/artifacts/tixl_mesh_draw_stage_mrt_matrix/tixl_mesh_draw_stage_mrt_matrix_result.json
docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json
```

It proves only that the replacement gate was evaluated and remains blocked:

```text
backendReplacementGateEvaluated: true
replacementBlockedBecauseFullBindingMissing: true
replacementBlockedBecauseAdapterProofMissing: true
boundedNativeBackendRemains: true
backendReplacementReady: false
fullPbrResourceBinding: false
hlslToMslTranslation: false
tixlRuntimeParity: false
nativeGpuParityComplete: false
```

If any consumed artifact widens a readiness claim, the gate fails. If the
resource binding artifact claims `fullPbrResourceBinding: true`, the gate also
requires an explicit adapter proof artifact; this lane does not invent that
proof, so the current fixture blocks that forged state.

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
