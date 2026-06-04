# TiXL Mesh Draw ShaderGraph Resources Expansion Proof

TixlMeshDrawShadergraphResourcesExpansionProof answers:

```text
does the current source-backed SphereSDF mesh Draw fixture prove any t8+
ShaderGraph resources behind the RESOURCES(t8) hook?
```

Status: yes, but the proven result is empty. The donor mesh shader contains:

```text
/*{RESOURCES(t8)}*/
```

The same shader already owns the ordinary base resources:

```text
t0 PbrVertices
t1 FaceIndices
t2 BaseColorMap
t3 EmissiveColorMap
t4 RSMOMap
t5 NormalMap
t6 PrefilteredSpecular
t7 BRDFLookup
```

TiXL fills the t8+ hook through `GenerateShaderGraphCode`. That source calls
`ShaderGraphNode.CollectResources`, then `InjectResourcesCode` parses the
`RESOURCES(t8)` hook and emits collected definitions starting at `t8`.
`ShaderGraphNode.CollectResources` recursively visits input shadergraph nodes
before calling `IGraphNodeOp.AppendShaderResources`.

For the current source-backed fixture, the visited root is:

```text
SphereSDF_nG1CBDm
```

`SphereSDF` exposes `GraphParam` inputs `Center` and `Radius`, but it does not
override `AppendShaderResources`. The interface default implementation is empty.
Therefore the current SphereSDF fixture has zero t8+ resources:

```text
resourceTypes: []
resourceReferences: []
resourceDefinitions: []
resourceViewsCount: 0
generatedResourceHlsl: ""
```

## Stage Append Evidence

The source also validates a bounded stage append behavior:

```text
GenerateShaderGraphCode.Resources -> SetPixelAndVertexShaderStage.VariousResources
```

`SetPixelAndVertexShaderStage` first binds ordinary `ShaderResources` from slot
0, then appends the `VariousResources` SRV list at `_shaderResourceViews.Length`
for both vertex and pixel shader stages. This proves source-stage ordering only.
It does not prove real SRV creation, renderer integration, or backend parity.

## Boundary

This proof does not prove non-empty t8 resources, real SRV creation, constant
buffer adapter completion, texture/sampler mapping, full PBR resource binding,
backend replacement, native compile parity, HLSL-to-MSL translation, TiXL
runtime parity, PBR visual correctness, or renderer integration.

The shell consumes the current b5 shadergraph params expansion artifact to keep
the SphereSDF root source-backed, then refuses any upstream artifact that widens
backend, parity, full PBR, or t8 resource claims.

## Current Required Next

The runtime closure report should no longer keep one oversized combined gate.
The remaining gates are narrower:

```text
prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter
prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference
replace_bounded_backend_interface_only_after_full_resource_binding_and_adapter_proof
```

The backend gate stays. This lane only removes the t8+ resource uncertainty for
the current SphereSDF fixture by proving the expansion is empty.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_shadergraph_resources_expansion.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_shadergraph_resources_expansion_shell.py <tixl_mesh_draw_shadergraph_resources_expansion.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_result.json
docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_trace.json
docs/runtime/artifacts/tixl_mesh_draw_shadergraph_resources_expansion/tixl_mesh_draw_shadergraph_resources_expansion_errors.json
```
