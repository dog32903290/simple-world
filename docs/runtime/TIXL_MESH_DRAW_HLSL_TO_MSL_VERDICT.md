# TiXL Mesh Draw HLSL-to-MSL Verdict

TixlMeshDrawHlslToMslTranslationVerdict answers:

```text
can the current TiXL mesh-Draw.hlsl source and proof ledger support mechanical
HLSL-to-MSL translation for mesh draw parity?
```

Verdict: mechanical translation for mesh draw parity is rejected for now. The
current artifacts classify the risk, but they do not prove HLSL-to-MSL
translation, full PBR resource binding, TiXL runtime parity, PBR visual
correctness, or backend replacement.

## Boundary

This is a verdict lane over existing artifacts:

```text
TIXL_MESH_DRAW_SHADER_SOURCE_AUDIT
TIXL_MESH_DRAW_RESOURCE_BINDING_PROOF
```

It is not an HLSL-to-MSL translator, not full PBR resource binding, not a
native compile proof, not TiXL parity, and not backend replacement. It does not
compile, render, rewrite shader text, generate MSL, or claim that a translator
could preserve mesh draw semantics.

The result may be `ok: true` only when the input artifacts validate and the
verdict remains conservative:

```text
status: rejected_for_mesh_draw_parity
mechanicalTranslationStatus: rejected_for_mesh_draw_parity
translationRiskClassified: true
mechanicalTranslationForMeshDrawParity: false
hlslToMslTranslation: false
fullPbrResourceBinding: false
tixlRuntimeParity: false
pbrVisualCorrectness: false
backendReplacementReady: false
```

## Structured Blocker Facts

The artifact records blocker facts as JSON, not prose-only notes. Required facts
include:

```text
b0-b5 cbuffers
t0-t7 texture/structured/cube resources
s0-s1 samplers
t8+ template resources
FLOAT_PARAMS
GLOBALS
FIELD_FUNCTIONS
FIELD_CALL
duplicate Params cbuffers
global frag state
ddx/ddy
discard
SV_Target0/1 MRT output
SV_VertexID, VPOS, SV_POSITION
mul(vector, matrix)
TextureCube SampleLevel/GetDimensions
missing PBR visual reference
partial resource binding only
```

These facts reject mechanical parity until a later lane proves the missing
translation semantics and binding contracts directly.

## Fail-Closed Cases

The shell exits `1` and writes blocked artifacts if:

```text
source audit or resource binding artifact is missing
source audit or resource binding artifact lacks required fields
source audit or resource binding claims are widened
fixture expected claims are widened
fixture requiredBlockerCodes omits any required blocker
```

In particular, `fullPbrResourceBinding`, `hlslToMslTranslation`, and
`backendReplacementReady` must stay false in this lane.

## First Verdict

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_hlsl_to_msl_verdict.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_hlsl_to_msl_verdict_shell.py <tixl_mesh_draw_hlsl_to_msl_verdict.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict/tixl_mesh_draw_hlsl_to_msl_verdict_result.json
docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict/tixl_mesh_draw_hlsl_to_msl_verdict_trace.json
docs/runtime/artifacts/tixl_mesh_draw_hlsl_to_msl_verdict/tixl_mesh_draw_hlsl_to_msl_verdict_errors.json
```
