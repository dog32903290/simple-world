# Runtime Closure Report

RuntimeClosureReport answers:

```text
can the current headless runtime proof be treated as closed, while clearly
marking the native draw shader compile boundary as bounded?
```

It is a lane-scoped closure ledger over existing artifacts, not a new renderer.
It closes only the current native_render_pipeline/headless proof lane. It is
not repo-wide runtime completion, not Metal/native GPU parity completion, and
not TiXL parity completion.

## Boundary

The shell reads the existing proof artifacts under:

```text
docs/runtime/artifacts/native_render_pipeline
```

Required inputs:

```text
pipeline_summary.json
command_stream_summary.json
shader_program/shader_program_package.json
native_backend/native_backend_interface.json
native_backend/backend_status.json
native_render_pipeline_errors.json
```

The report marks `core_headless_pipeline` proven only when the native render
pipeline is ok, has at least one draw call, uses `drawCommandArtifact`, and has
`nonBlackSample == true`.

The native HLSL/Metal compile remains bounded when
`native_backend.nativeDrawBoundary.status == compileParityNotClaimed` and
`backendCanCompileNow == false`. That is an explicit next-work ledger entry,
not a failure of the current headless closure.

Newer proof lanes narrow that blocker but do not remove it. `MetalExplicitMslProof`
proves that explicit MSL source can go through real Metal compile, offscreen
render, and readback. `NativeDrawShaderCompileProof` can delegate explicit
`requestedDrawShader.nativeSource` MSL to that Metal proof. The current
native_render_pipeline `ShaderProgram` still requests the TiXL donor draw shader
as `HLSL_TIXL_DONOR` with no explicit native source, so the default
NativeDrawShaderCompileProof artifact remains `blocked_missing_native_source`.
That explicit MSL proof exists, but it does not discharge the TiXL donor HLSL
boundary.

For the bounded backend state, `requiredNext` names the remaining draw-shader
translation, full resource binding, and native compile proof work. The TiXL
donor source audit exists as a dependency map, and the TiXL mesh draw buffer
layout proof fixes `PbrVertex`/`FaceIndices` packing facts for the next
approximation. The TiXL mesh draw MSL approximation proof now shows that this
fixed layout can feed a tiny explicit MSL/Metal mesh draw through compile,
render, and readback. The TiXL mesh draw resource binding proof records that
only `PbrVertices t0 -> buffer(0)` and `FaceIndices t1 -> buffer(1)` are bound
today; PBR cbuffers, textures, samplers, and t8+ injected resources remain
unbound. Those lanes narrow the blocker but do not replace the remaining proof:

```text
prove_or_reject_hlsl_to_msl_translation_for_mesh_draw
bind_full_pbr_texture_sampler_set_after_hlsl_to_msl_translation
replace_bounded_backend_interface_after_resource_binding_and_hlsl_to_msl_proof
```

## Failure Law

The closure report fails if `native_render_pipeline_errors.json` is non-empty
or `pipeline_summary.ok` is not true. It does not hide those failures behind
bounded backend wording.

## First Proof

Fixture:

```text
docs/runtime/fixtures/runtime_closure_report.graph.json
```

Runner:

```text
docs/runtime/scripts/runtime_closure_report_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/runtime_closure_report/runtime_closure_report.json
docs/runtime/artifacts/runtime_closure_report/runtime_closure_trace.json
docs/runtime/artifacts/runtime_closure_report/runtime_closure_errors.json
```
