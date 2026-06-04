# TiXL Mesh Draw B5 ShaderGraph Params Expansion Verdict

TixlMeshDrawB5ShadergraphParamsExpansionVerdict answers:

```text
can the duplicate b5 Params cbuffer in TiXL mesh Draw be expanded into concrete,
source-backed shadergraph parameter fields yet?
```

Status: currently no. The donor mesh shader contains:

```text
cbuffer Params : register(b5)
{
    /*{FLOAT_PARAMS}*/
}
```

Those fields are not static fields in `mesh-Draw.hlsl`. TiXL fills that hole
through `GenerateShaderGraphCode`: it calls
`ShaderGraphNode.CollectAllNodeParams(...)`, injects the collected shader-code
parameters into `/*{FLOAT_PARAMS}*/`, and writes the runtime float values into
the `FloatParams` buffer.

The current source audit and constant-buffer layout artifacts still record b5
`Params` with `fields: []`. Therefore this lane must stay blocked until a
source-backed ShaderGraph parameter expansion artifact exists.

## Boundary

This is not native b5 packing, not constant-buffer adapter completion, not
texture/sampler mapping, not full PBR resource binding, not backend replacement,
not HLSL-to-MSL translation, not TiXL runtime parity, and not PBR visual
correctness.

The shell consumes:

```text
docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json
docs/runtime/artifacts/tixl_mesh_draw_constant_buffer_layout/tixl_mesh_draw_constant_buffer_layout_result.json
docs/runtime/artifacts/tixl_mesh_draw_pointlights_and_b5_packing/tixl_mesh_draw_pointlights_and_b5_packing_result.json
external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl
external/tixl/Operators/Lib/field/render/_/GenerateShaderGraphCode.cs
external/tixl/Operators/Lib/mesh/draw/DrawMesh.t3
```

## Fail-Closed Law

If b5 fields appear in source audit or layout artifacts, each field must have
source-backed ShaderGraph parameter provenance before this lane can accept it.
JSON fields without provenance are treated as invented fields.

If the upstream PointLights/b5 verdict no longer proves b3 PointLights, or if
any backend/PBR/translation/parity claim widens, this lane fails closed.

## Current Required Next

```text
produce_source_backed_shadergraph_param_expansion_artifact_for_b5
```

That future artifact must show the concrete ShaderGraph field source, generated
HLSL parameter declaration, float-buffer order, and packing requirements before
b5 can move into a native packing proof.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_b5_shadergraph_params_expansion.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_b5_shadergraph_params_expansion_shell.py <tixl_mesh_draw_b5_shadergraph_params_expansion.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_result.json
docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_trace.json
docs/runtime/artifacts/tixl_mesh_draw_b5_shadergraph_params_expansion/tixl_mesh_draw_b5_shadergraph_params_expansion_errors.json
```
