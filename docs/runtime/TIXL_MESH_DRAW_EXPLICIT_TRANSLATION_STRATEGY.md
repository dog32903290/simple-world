# TiXL Mesh Draw Explicit Translation Strategy

TixlMeshDrawExplicitTranslationStrategy answers:

```text
after mechanical HLSL-to-MSL mesh draw parity is rejected, which bounded
translation route should carry the next runtime closure lane?
```

Strategy: use `handwritten_explicit_msl_adapter` as the short-term mesh draw
route. The current mechanical HLSL-to-MSL path is rejected by the prior verdict,
and the full cross-compiler route is not selected for this bounded lane.

## Boundary

This is a strategy lane over existing artifacts:

```text
TIXL_MESH_DRAW_SHADER_SOURCE_AUDIT
TIXL_MESH_DRAW_HLSL_TO_MSL_VERDICT
TIXL_MESH_DRAW_RESOURCE_BINDING_PROOF
```

It is not a translator, not full PBR resource binding, not a full PBR shader,
not TiXL runtime parity, not PBR visual correctness, and not backend replacement.
It does not emit MSL, compile, render, patch source code, or claim that a
handwritten adapter is ready for the full TiXL mesh draw contract.

The result may be `ok: true` only when the input artifacts validate and the
strategy remains conservative:

```text
mechanicalTranslationRejected: true
selectedStrategy: handwritten_explicit_msl_adapter
fullCrossCompilerSelected: false
explicitAdapterReadyForFullPbr: false
fullPbrResourceBinding: false
backendReplacementReady: false
tixlRuntimeParity: false
hlslToMslTranslation: false
pbrVisualCorrectness: false
```

## Acceptance Gates

The shell accepts the lane only when:

```text
source audit artifact is valid
HLSL-to-MSL verdict artifact is valid
verdict.status == rejected_for_mesh_draw_parity
verdict keeps hlslToMslTranslation false
verdict keeps backendReplacementReady false
verdict keeps every required blocker fact
resource binding still observes only PbrVertices t0 and FaceIndices t1
PBR cbuffers/textures/samplers and t8+ resources are still pending
```

The currently observed adapter is only the packed mesh buffer path:

```text
PbrVertices t0 -> Metal buffer(0)
FaceIndices t1 -> Metal buffer(1)
```

Everything outside that t0/t1 adapter is still pending:

```text
b0-b5 cbuffers
t2-t7 textures/cubes
s0-s1 samplers
t8+ shadergraph resources
```

## Next Adapter Gates

The selected short-term strategy must still pass these gates before it can grow
toward full PBR:

```text
constant buffer layout b0-b5
texture/sampler mapping t2-t7/s0-s1
t8+ shadergraph expansion
MRT contract
stage semantics
matrix convention
cube mip query
PBR visual reference
```

## Fail-Closed Cases

The shell exits `1` and writes blocked artifacts if:

```text
source audit, HLSL-to-MSL verdict, or resource binding artifact is missing
source audit artifact loses mesh draw slot facts
verdict.status is not rejected_for_mesh_draw_parity
verdict claims hlslToMslTranslation true
verdict claims backendReplacementReady true
verdict omits any required blocker fact
resource binding claims fullPbrResourceBinding true
resource binding claims hlslToMslTranslation true
resource binding claims backendReplacementReady true
resource binding binds anything beyond the t0/t1 packed mesh buffers
fixture expected claims, gates, or blocker list are widened
```

`full_cross_compiler` remains a larger unselected route. It is not rejected as a
future architecture, but it is not the next runtime closure lane.

## First Strategy Artifact

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_explicit_translation_strategy.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_explicit_translation_strategy_shell.py <tixl_mesh_draw_explicit_translation_strategy.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json
docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_trace.json
docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_errors.json
```
