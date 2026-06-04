# TiXL Mesh Draw B5 ShaderGraph Params Expansion Verdict

TixlMeshDrawB5ShadergraphParamsExpansionVerdict answers:

```text
can the duplicate b5 Params cbuffer in TiXL mesh Draw be expanded into concrete,
source-backed shadergraph parameter fields yet?
```

Status: yes, for one bounded source-backed fixture. The donor mesh shader contains:

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

The source audit and constant-buffer layout artifacts still record b5 `Params`
with `fields: []`, because those artifacts describe the unexpanded mesh donor
template. This lane adds the missing source-backed expansion artifact for a
concrete `DrawMesh.FragmentField <- SphereSDF.Result` fixture.

The current proven b5 expansion is:

```text
float3  SphereSDF_nG1CBDm_Center;  // offset 0, values -1.4845504, 0, 0.54366434
float   SphereSDF_nG1CBDm_Radius;  // offset 12, value 0.5
```

The generated name prefix follows TiXL `ShaderGraphNode.BuildNodeId`:

```text
<CSharpTypeName>_<ShortenGuid(SymbolChildId)>_
```

For the fixture child id `04426d9c-b039-4a92-9b1f-61186b4df2e5`,
`ShortenGuid` is `nG1CBDm`, so the prefix is `SphereSDF_nG1CBDm_`.

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

If b5 fields appear in source audit or layout artifacts, this lane still fails
closed. The source-backed expansion must live in this explicit ShaderGraph
parameter expansion artifact until the source audit/layout lanes are upgraded to
consume generated ShaderGraph fields directly.

If the upstream PointLights/b5 verdict no longer proves b3 PointLights, or if
any backend/PBR/translation/parity claim widens, this lane fails closed.

## Current Required Next

```text
prove_native_b5_packing_from_source_backed_shadergraph_params
```

The native proof must compile/read back a Metal buffer for the generated b5
layout. This source-backed expansion does not yet prove native b5 packing,
constant-buffer adapter completion, or backend replacement.

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
