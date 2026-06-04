# TiXL Mesh Draw Shader Source Audit

TixlMeshDrawShaderSourceAudit answers:

```text
what does TiXL mesh-Draw.hlsl depend on, where are its shader entries, what resources does it require, and what semantic blockers remain before any native compile/parity lane?
```

Runtime path:

```text
tixl_mesh_draw_shader_source_audit.graph.json -> TixlMeshDrawShaderSourceAudit -> source audit result/trace/errors artifacts
```

## Boundary

This is a source audit. It reads the ignored donor file at
`external/tixl/Operators/Lib/Assets/shaders/3d/mesh/mesh-Draw.hlsl`, follows
quoted `#include` directives through fixture-declared include roots, and
publishes path-clean metadata only.

It is not a compile proof, not a render proof, not TiXL parity, not
HLSL-to-MSL translation, not native backend integration, and not PBR visual
correctness. A successful audit may report `ok: true` with status
`audited_tixl_mesh_draw_source`, but that only means the source dependency
summary was produced.

Artifacts must not contain the full donor source text. They may contain
repo-relative paths, SHA-256 hashes, line counts, include names, entry points,
register/resource summaries, template hole names, and semantic blocker codes.

## Captured Source Facts

The default donor source is expected to expose:

```text
vertex entry: vsMain
pixel entry:  psMain
```

The direct include list must include the mesh donor's quoted includes, including
`shared/pbr-render.hlsl`. The recursive include graph must be able to record
`shared/pbr-render.hlsl -> shared/pbr.hlsl` when those files are present in the
checkout.

The result records required constant buffers, structured buffers, textures,
samplers, constants, template holes, and symbols as summaries. These summaries
are dependency facts, not proof that our runtime can bind or compile them.

## Blocked Cases

If the donor source is missing, the shell exits `1`, writes
`blocked_missing_donor_source`, and keeps the artifact path-clean. Missing donor
source is never a fake success.

If includes are unresolved, the shell records include errors and semantic
blockers instead of pretending the dependency graph is complete.

The claim flags are always false in this lane:

```text
hlslToMslTranslationProven
tixlParity
nativeCompileParity
pbrVisualCorrectness
```

## First Audit

Fixture:

```text
docs/runtime/fixtures/tixl_mesh_draw_shader_source_audit.graph.json
```

Runner:

```text
docs/runtime/scripts/tixl_mesh_draw_shader_source_audit_shell.py <tixl_mesh_draw_shader_source_audit.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_result.json
docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_trace.json
docs/runtime/artifacts/tixl_mesh_draw_shader_source_audit/tixl_mesh_draw_shader_source_audit_errors.json
```
