# TiXL Mesh Draw Constant Buffer Layout Proof

TixlMeshDrawConstantBufferLayoutProof answers:

```text
after the explicit translation strategy selects handwritten_explicit_msl_adapter,
what b0-b5 constant buffer layout facts and bounded binding policy can the next
adapter lane inspect without implementing the shader?
```

## Boundary

This is a constant-buffer layout and policy proof for the selected
`handwritten_explicit_msl_adapter` route. It is not a shader implementation,
not texture/sampler mapping, not full PBR resource binding, not backend replacement,
not HLSL-to-MSL translation, not TiXL runtime parity, and not PBR visual
correctness.

The shell reads:

```text
docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json
docs/runtime/artifacts/tixl_mesh_draw_explicit_translation_strategy/tixl_mesh_draw_explicit_translation_strategy_result.json
```

The result may be `ok: true` only when the source audit still contains the exact
b0-b5 register/name/type facts below and the strategy artifact still selects
`handwritten_explicit_msl_adapter`. Any widened strategy/backend/PBR claim fails
closed.

## Captured Constant Buffers

```text
b0 Transforms
CameraToClipSpace float4x4
ClipSpaceToCamera float4x4
WorldToCamera float4x4
CameraToWorld float4x4
WorldToClipSpace float4x4
ClipSpaceToWorld float4x4
ObjectToWorld float4x4
WorldToObject float4x4
ObjectToCamera float4x4
ObjectToClipSpace float4x4

b1 Params
Color float4
AlphaCutOff float
UseFlatShading float
SpecularAA float

b2 FogParams
FogColor float4
FogDistance float
FogBias float

b3 PointLights
Lights PointLight[8]
ActiveLightCount int

b4 PbrParams
BaseColor float4
EmissiveColor float4
Roughness float
Specular float
Metal float

b5 Params
```

The b5 `Params` buffer is the duplicate shadergraph/template params cbuffer. It
must stay explicitly disambiguated from b1 `Params`; b1 is mesh draw material
params, while b5 is shadergraph duplicate params. The current source audit has
no concrete b5 fields, so this lane records b5 as named and registered but not
natively packed.

## Binding Policy

The current observed adapter still binds only packed mesh buffers:

```text
PbrVertices t0 -> Metal buffer(0)
FaceIndices t1 -> Metal buffer(1)
```

This lane reserves a bounded partial policy for cbuffers after those two mesh
buffers:

```text
b0-b5 candidate constant-buffer range -> Metal buffer(2)..buffer(7)
```

That policy is not backend binding. It needs native packing proof before it can
be treated as a real Metal adapter contract. The claim flags remain conservative:

```text
selectedStrategyConsumed: true
constantBufferLayoutClassified: true
constantBufferBindingPolicyReady: bounded_partial
b0b5LayoutNeedsNativePackingProof: true
textureSamplerMapping: false
fullPbrResourceBinding: false
backendReplacementReady: false
hlslToMslTranslation: false
tixlRuntimeParity: false
pbrVisualCorrectness: false
```

## Fail-Closed Cases

The shell exits `1` and writes blocked artifacts if:

```text
fixture expectations widen claims
source audit artifact is missing or not ok
source audit loses any b0-b5 register/name/type/array fact
strategy artifact is missing or not ok
strategy artifact does not select handwritten_explicit_msl_adapter
strategy artifact widens full PBR, HLSL-to-MSL, TiXL parity, visual, or backend claims
```

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_constant_buffer_layout.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_constant_buffer_layout_shell.py <tixl_mesh_draw_constant_buffer_layout.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_trace.json
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_errors.json
```
