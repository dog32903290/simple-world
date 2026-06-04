# TiXL Mesh Draw Resource Binding Proof

TixlMeshDrawResourceBindingProof answers:

```text
which mesh-Draw.hlsl resource slots are actually bound by the current explicit
MSL approximation proof, and which audited TiXL resources remain declared but
unbound?
```

Runtime path:

```text
tixl_mesh_draw_resource_binding.graph.json -> tixl_mesh_draw_resource_binding_shell.py -> result/trace/errors binding ledger
```

## Boundary

This is a binding ledger over three existing artifacts:

```text
TIXL_MESH_DRAW_SHADER_SOURCE_AUDIT
TIXL_MESH_DRAW_BUFFER_LAYOUT_PROOF
TIXL_MESH_DRAW_MSL_APPROX_PROOF
```

It is not an HLSL-to-MSL translator, not PBR visual correctness, not TiXL
runtime parity, and not renderer/backend replacement. It does not compile or
draw anything new. It validates that the MSL approximation already observed
packed mesh buffer binding for `PbrVertices` and `FaceIndices`, then records the
rest of the TiXL shader resources as explicitly unbound in that approximation.

The shell reports:

```text
meshBufferBindingObserved: true only after all three input artifacts validate
fullPbrResourceBinding: false
hlslToMslTranslation: false
tixlRuntimeParity: false
backendReplacementReady: false
```

## Inputs

The source audit artifact must be:

```text
kind: TixlMeshDrawShaderSourceAudit
ok: true
status: audited_tixl_mesh_draw_source
resources: PbrVertices t0, FaceIndices t1, BaseColorMap t2, EmissiveColorMap t3,
RSMOMap t4, NormalMap t5, PrefilteredSpecular t6, BRDFLookup t7
samplers: WrappedSampler s0, ClampedSampler s1
cbuffers: b0 Transforms, b1 Params, b2 FogParams, b3 PointLights, b4 PbrParams, b5 Params
template hole: RESOURCES(t8)
```

The buffer layout artifact must be:

```text
kind: TixlMeshDrawBufferLayoutProof
ok: true
status: summarized_tixl_mesh_draw_buffer_layout
PbrVertex stride: 80 bytes
FaceIndices stride: 12 bytes
drawVertexCount formula: faceCount * 3
```

The MSL approximation artifact must be:

```text
kind: TixlMeshDrawMslApproxProof
ok: true
status: rendered_tixl_mesh_draw_msl_approximation
mslApproxBufferPackingObserved: true
hlslToMslTranslation: false
pbrVisualCorrectness: false
```

## Binding Ledger

`boundNow` is intentionally small:

```text
PbrVertices t0 -> Metal buffer(0), PbrVertex stride 80
FaceIndices t1 -> Metal buffer(1), FaceIndices stride 12, drawVertexCount faceCount * 3
```

The evidence is the MSL approximation frame/control digest pair. The control
digest must differ from the frame digest, because the prior proof used a
degenerate face-index control mesh to show the packed buffer read affects the
frame.

`declaredButUnbound` includes:

```text
b0 Transforms
b1 Params
b2 FogParams
b3 PointLights
b4 PbrParams
b5 shadergraph Params
t2 BaseColorMap
t3 EmissiveColorMap
t4 RSMOMap
t5 NormalMap
t6 PrefilteredSpecular
t7 BRDFLookup
s0 WrappedSampler
s1 ClampedSampler
t8+ injected resources
FLOAT_PARAMS
GLOBALS
FIELD_FUNCTIONS
FIELD_CALL
```

These are explicitly not bound in the current approximation.

## Blocked Cases

If any input artifact is missing, unreadable, wrong kind, not ok, wrong status,
or has widened claims, the shell exits `1` and writes stable result/trace/errors
artifacts. It does not synthesize a successful binding ledger from partial
evidence.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_resource_binding.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_resource_binding_shell.py <tixl_mesh_draw_resource_binding.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_trace.json
docs/runtime/artifacts/tixl_mesh_draw_resource_binding/tixl_mesh_draw_resource_binding_errors.json
```
