# TiXL Mesh Draw Native Metal Backend Integration Proof

TixlMeshDrawNativeMetalBackendIntegrationProof answers:

```text
can the bounded TiXL Mesh Draw / PBR lane replace the deterministic backend
interface with a real native Metal backend proof?
```

This proof consumes the native render pipeline artifact, the full PBR resource
binding proof, and the explicit adapter proof. It runs a real Metal
render/readback probe and compares the current bounded PBR reference sentinel.
The backend replacement gate consumes this proof afterward; this proof does not
consume the gate, so the proof graph stays acyclic.

It proves only the bounded Mesh Draw / PBR lane:

```text
backendReplacementReady: true
nativeGpuParityComplete: true
tixlRuntimeParity: true
fullPbrResourceBinding: true
explicitAdapterProofPresent: true
hlslToMslTranslation: false
```

`hlslToMslTranslation` stays false because this route uses the selected
handwritten explicit MSL adapter. It does not claim a generic TiXL clone, Vuo
parity, or mechanical HLSL-to-MSL translation.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_native_metal_backend_integration.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_native_metal_backend_integration_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/tixl_mesh_draw_native_metal_backend_integration_result.json
docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/tixl_mesh_draw_native_metal_backend_integration_trace.json
docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/tixl_mesh_draw_native_metal_backend_integration_errors.json
docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/generated_native_metal_backend.metal
docs/runtime/artifacts/tixl_mesh_draw_native_metal_backend_integration/frame_stats.json
```
