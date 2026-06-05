# TiXL Mesh Draw Explicit Adapter Proof

TixlMeshDrawExplicitAdapterProof answers:

can the selected `handwritten_explicit_msl_adapter` scope be compiled and run
through real Metal as an explicit adapter proof, using the existing selected
strategy, MSL approximation, and Metal explicit MSL evidence?

Pipeline:

```text
tixl_mesh_draw_explicit_adapter_proof.graph.json -> tixl_mesh_draw_explicit_adapter_proof_shell.py -> ObjC++ Metal probe -> result/trace/errors/generated MSL/frame_stats artifacts
```

This proof consumes:

```text
docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json
docs/runtime/artifacts/tixl_mesh_draw_msl_approx/tixl_mesh_draw_msl_approx_result.json
docs/runtime/artifacts/metal_explicit_msl_proof/metal_explicit_msl_result.json
```

It writes `generated_explicit_adapter.metal`, compiles it with Metal, renders a
small offscreen adapter frame, and publishes `frame_stats.json`.

Positive claims:

```text
explicitAdapterProof: true
explicitAdapterProofPresent: true
actualCompilerRan: true
actualMetalRan: true
```

Boundary claims that stay false:

```text
fullPbrResourceBinding: false
backendReplacementReady: false
hlslToMslTranslation: false
tixlRuntimeParity: false
nativeGpuParityComplete: false
pbrVisualCorrectness: false
```

Scope:

This is a named explicit adapter proof only. It proves that the selected
handwritten MSL adapter route can be materialized and run by Metal for this
bounded adapter lane. It does not consume a full PBR binding artifact, does not
translate TiXL HLSL, does not replace the backend, and does not claim TiXL
runtime parity.
