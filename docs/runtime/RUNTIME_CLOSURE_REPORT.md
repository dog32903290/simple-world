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
unbound. The TiXL mesh draw HLSL-to-MSL verdict lane now rejects mechanical
translation for mesh draw parity from structured blocker facts: b0-b5 cbuffers,
t0-t7 textures/cubes, s0-s1 samplers, t8+ template resources, TiXL template
holes, duplicate `Params`, global frag state, derivatives, discard, MRT outputs,
D3D system semantics, D3D `mul(vector, matrix)`, and TextureCube
`SampleLevel`/`GetDimensions`. Those lanes narrow the blocker but do not replace
the remaining proof. The TiXL mesh draw explicit translation strategy lane now
selects `handwritten_explicit_msl_adapter` as the short-term route and leaves a
full cross-compiler unselected; it only records the strategy after the rejected
mechanical verdict. It still acknowledges that the observed adapter is limited
to t0/t1 packed mesh buffers and is not ready for full PBR resource binding or
backend replacement. The TiXL mesh draw constant buffer layout proof now
classifies b0-b5 HLSL cbuffer facts and reserves a bounded partial candidate
mapping after t0/t1, but it still requires native Metal packing proof before
those constants can be treated as adapter-ready. The next required work is
therefore:

```text
prove_native_metal_packing_for_handwritten_adapter_constant_buffers_b0_b5
map_handwritten_explicit_msl_adapter_textures_samplers_t2_t7_s0_s1
expand_t8_shadergraph_resources_and_set_mrt_stage_matrix_cube_pbr_reference_gates
replace_bounded_backend_interface_only_after_full_resource_binding_and_adapter_proof
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
