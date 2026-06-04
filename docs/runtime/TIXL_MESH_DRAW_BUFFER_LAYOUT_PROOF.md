# TiXL Mesh Draw Buffer Layout Proof

TixlMeshDrawBufferLayoutProof answers:

```text
what native buffer packing facts must the next mesh-Draw.hlsl approximation keep fixed before any MSL draw work can be trusted?
```

Runtime path:

```text
tixl_mesh_draw_buffer_layout.graph.json -> TixlMeshDrawBufferLayoutProof -> buffer layout result/trace/errors artifacts
```

## Boundary

This is a layout proof. It reads TiXL donor evidence and publishes path-clean
metadata plus a compact layout contract. It is not a draw proof, not a render proof,
not Metal buffer parity, not TiXL runtime parity, and not visual correctness.

The shell reads:

```text
external/tixl/Core/Rendering/PbrVertex.cs
external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl
docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json
```

Artifacts must not contain donor source text. They may contain repo-relative
paths, SHA-256 hashes, line counts, field names, field types, offsets, sizes,
stride values, register/resource summaries, and blocker codes.

## Captured Layout Facts

`PbrVertex.cs` must expose an explicit 80-byte vertex stride:

```text
Position  float3 offset 0  size 12
Normal    float3 offset 12 size 12
Tangent   float3 offset 24 size 12
Bitangent float3 offset 36 size 12
TexCoord  float2 offset 48 size 8
TexCoord2 float2 offset 56 size 8
Selected  float  offset 64 size 4
ColorRGB  float3 offset 68 size 12
stride: 80 bytes
```

`mesh-Draw.hlsl` must expose `StructuredBuffer<int3> FaceIndices`, which fixes
face-index stride at 12 bytes. This lane records topology as TriangleList only
and fixes `drawVertexCount = faceCount * 3`. It does not claim triangulation.

The claim flags are:

```text
contractLayoutSummarized: true only on a successful layout summary
metalBufferPackingParity: false
tixlRuntimeParity: false
visualCorrectness: false
```

## Blocked Cases

If `PbrVertex.cs` is missing, the shell exits `1`, writes
`blocked_missing_donor_layout_source`, and keeps artifacts path-clean.

If the source audit artifact is missing, the shell exits `1`, writes
`blocked_missing_source_audit`, and keeps artifacts path-clean.

If `mesh-Draw.hlsl` is missing, the shell exits `1`, writes
`blocked_missing_donor_shader_source`, and keeps artifacts path-clean.

## First Proof

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_buffer_layout.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_buffer_layout_shell.py <tixl_mesh_draw_buffer_layout.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_result.json
docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_trace.json
docs/runtime/artifacts/tixl_mesh_draw_buffer_layout/tixl_mesh_draw_buffer_layout_errors.json
```
