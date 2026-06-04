# TiXL Mesh Draw TextureCube PBR Reference Proof

This lane closes only:

```text
prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference
```

It does not claim full PBR parity, backend replacement, TiXL runtime parity, or
HLSL-to-MSL translation.

## Boundary

The proof consumes the current bounded upstream artifacts:

```text
stage/MRT/matrix proof
texture/sampler binding proof
b5 native packing proof
shadergraph t8 resources proof
HLSL-to-MSL rejection verdict
TiXL mesh Draw source audit
```

Stale, missing, or widened upstream claims block the proof before the Metal
probe runs.

## TextureCube API Mapping

The generated explicit Metal probe maps only this tiny API pair:

```text
TextureCube SampleLevel -> Metal texturecube.sample(..., level(0.0)) and level(1.0)
TextureCube GetDimensions -> Metal get_width(0), get_height(0), get_width(1), get_height(1), get_num_mip_levels()
```

The probe uses a 4x4 `texturecube<float>` sentinel with two mip levels and
reads back:

```text
dimensions: 4 x 4, mipLevels: 2
mip1Dimensions: 2 x 2
sampleLevel0Rgba8: [52, 86, 120, 255]
sampleLevel1Rgba8: [140, 30, 200, 255]
```

This is API evidence for `SampleLevel`/`GetDimensions` mapping only. It is not
proof of t3-t6 full texture binding, full IBL behavior, donor shader translation,
or renderer integration.

## Bounded PBR Visual Reference

The PBR reference is a bounded analytic sentinel computed from the two cube mip
samples plus fixed BRDF/roughness constants, then compared by exact RGBA8:

```text
boundedPbrVisualReferenceEstablished: true
sentinelRgba8: [68, 62, 54, 255]
pbrVisualCorrectness: false
fullPbrResourceBinding: false
backendReplacementReady: false
hlslToMslTranslation: false
TiXL runtime parity: false
```

The name is intentionally narrow. It proves that a deterministic reference
exists, is generated, and is compared at sentinel level. It does not prove visual
correctness of TiXL PBR.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_texturecube_pbr_reference.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_texturecube_pbr_reference_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_result.json
docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_trace.json
docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/tixl_mesh_draw_texturecube_pbr_reference_errors.json
docs/runtime/artifacts/tixl_mesh_draw_texturecube_pbr_reference/generated_texturecube_pbr_reference_probe.metal
```
