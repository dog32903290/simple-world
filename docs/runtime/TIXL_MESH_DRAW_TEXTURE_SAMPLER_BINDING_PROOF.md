# TiXL Mesh Draw Texture Sampler Binding Proof

TixlMeshDrawTextureSamplerBindingProof answers:

```text
can the bounded handwritten explicit MSL adapter prove only t2/t7 textures and
s0/s1 samplers at selected Metal binding indices with real Metal readback?
```

Runtime path:

```text
tixl_mesh_draw_texture_sampler_binding.graph.json -> tixl_mesh_draw_texture_sampler_binding_shell.py -> Metal compute sentinel probe -> result/trace/errors
```

## Boundary

This lane consumes:

```text
TIXL_MESH_DRAW_SHADER_SOURCE_AUDIT
TIXL_MESH_DRAW_RESOURCE_BINDING_PROOF
TIXL_MESH_DRAW_EXPLICIT_TRANSLATION_STRATEGY
```

The source audit must contain `t2 BaseColorMap Texture2D<float4>`,
`t7 BRDFLookup Texture2D<float4>`, `s0 WrappedSampler`, and
`s1 ClampedSampler`. The prior resource binding artifact must still declare
those four slots as unbound, and it must not already claim full PBR binding,
backend replacement readiness, or this texture/sampler mapping. The strategy
artifact must still select `handwritten_explicit_msl_adapter`.

This is not full texture/sampler mapping, not HLSL-to-MSL translation, not TiXL
runtime parity, not PBR visual correctness, not renderer integration, not
constant-buffer adapter completion, and not backend replacement.

## Adapter Mapping

For this bounded handwritten adapter subset, the fixture uses direct source
register-number mapping:

```text
t2 BaseColorMap -> Metal texture(2)
t7 BRDFLookup -> Metal texture(7)
s0 WrappedSampler -> Metal sampler(0)
s1 ClampedSampler -> Metal sampler(1)
```

The probe creates two tiny host textures with RGBA sentinel pixels, creates one
repeat sampler and one clamp-to-edge sampler, binds them at those exact Metal
indices, dispatches a compute kernel, and reads back four deterministic words:

```text
t2 direct sample: 0x11223344
t7 direct sample: 0xa1b2c3d4
s0 wrapped sample: 0x778899aa
s1 clamped sample: 0x55667788
```

The trace records source/prior/strategy validation before the Metal probe.

## Claims

On success only:

```text
sourceAuditArtifactConsumed: true
priorResourceBindingArtifactConsumed: true
actualMetalTextureSamplerProbeRan: true
t2BaseColorMapBindingProven: true
t7BrdfLookupBindingProven: true
s0WrappedSamplerBindingProven: true
s1ClampedSamplerBindingProven: true
boundedTextureSamplerMappingProven: true
boundedTextureSamplerMappingSubset: ["t2", "t7", "s0", "s1"]
```

Forbidden claims stay false:

```text
fullPbrResourceBinding: false
t8ShadergraphResourcesExpanded: false
backendReplacementReady: false
hlslToMslTranslation: false
tixlRuntimeParity: false
pbrVisualCorrectness: false
rendererIntegrationComplete: false
constantBufferAdapterComplete: false
```

## Blocked Cases

If `t2`, `t7`, `s0`, or `s1` is missing from the source audit, the shell exits
`1` before compiling Metal. If the prior resource binding artifact has widened
claims or has already bound this subset, it exits `1` before compiling Metal.
If fixture expectations widen to full PBR/backend readiness, it exits `1`
before compiling Metal.

If Metal is unavailable, the probe build fails, Metal compilation fails, or the
sentinel readback does not match exactly, the shell exits `1` and does not write
a successful generated Metal artifact.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_texture_sampler_binding.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_texture_sampler_binding_shell.py <tixl_mesh_draw_texture_sampler_binding.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_result.json
docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_trace.json
docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/tixl_mesh_draw_texture_sampler_binding_errors.json
docs/runtime/artifacts/tixl_mesh_draw_texture_sampler_binding/generated_texture_sampler_probe.metal
```
