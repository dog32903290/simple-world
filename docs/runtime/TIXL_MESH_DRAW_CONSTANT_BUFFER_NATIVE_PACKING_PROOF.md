# TiXL Mesh Draw Constant Buffer Native Packing Proof

TixlMeshDrawConstantBufferNativePackingProof answers:

```text
can the handwritten_explicit_msl_adapter lane prove native Metal packing for
the first TiXL mesh draw constant buffers without implementing the full shader?
```

Status: partial native packing is proven for b0, b1, b2, and b4. The shell
generates a tiny MSL compute probe, compiles it through Metal, binds host byte
buffers at the reserved adapter slots, and reads sentinel values back from the
shader.

## Boundary

This is a native packing proof for part of the constant-buffer lane. It is not texture/sampler mapping,
not full PBR resource binding, not backend replacement, not HLSL-to-MSL
translation, not TiXL runtime parity, and not PBR visual correctness.

It consumes:

```text
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json
```

The proof keeps the full adapter conservative:

```text
nativePackingProofComplete: false
b0b5AdapterReady: false
textureSamplerMapping: false
fullPbrResourceBinding: false
backendReplacementReady: false
hlslToMslTranslation: false
tixlRuntimeParity: false
pbrVisualCorrectness: false
```

## Proven Packing

The native Metal probe proves these layouts by readback:

```text
b0 Transforms -> Metal buffer(2), size 640
CameraToClipSpace 0
ClipSpaceToCamera 64
WorldToCamera 128
CameraToWorld 192
WorldToClipSpace 256
ClipSpaceToWorld 320
ObjectToWorld 384
WorldToObject 448
ObjectToCamera 512
ObjectToClipSpace 576

b1 Params -> Metal buffer(3), size 32
Color 0
AlphaCutOff 16
UseFlatShading 20
SpecularAA 24

b2 FogParams -> Metal buffer(4), size 32
FogColor 0
FogDistance 16
FogBias 20

b4 PbrParams -> Metal buffer(6), size 48
BaseColor 0
EmissiveColor 16
Roughness 32
Specular 36
Metal 40
```

## Pending Packing

These remain pending:

```text
b3 PointLights
b5 duplicate Params
```

b3 is pending because `PointLight` element layout and array stride are not
proven by this tiny probe. b5 is pending because the source audit records the
duplicate shadergraph `Params` cbuffer, but has no concrete b5 fields to pack.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_constant_buffer_native_packing.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_constant_buffer_native_packing_shell.py <tixl_mesh_draw_constant_buffer_native_packing.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_result.json
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_trace.json
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/tixl_mesh_draw_constant_buffer_native_packing_errors.json
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_native_packing/generated_constant_buffer_native_packing_probe.metal
```
