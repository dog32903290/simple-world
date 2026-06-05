# Runtime Closure Report

RuntimeClosureReport answers:

```text
can the current headless runtime proof be treated as closed, while clearly
marking the native draw shader compile boundary as bounded and proving the
bounded TiXL Mesh Draw/PBR native Metal backend lane replacement-ready?
```

It is a lane-scoped closure ledger over existing artifacts, not a new renderer.
It closes only the current native_render_pipeline/headless proof lane plus the
bounded TiXL Mesh Draw/PBR backend replacement lane. This is bounded native
GPU/Metal/TiXL parity completion for that lane, not repo-wide runtime
completion, not generic TiXL clone parity, and not Vuo parity.

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

For the bounded backend state, the older `requiredNext` list named the remaining
draw-shader translation, full resource binding, and native compile proof work.
The TiXL
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
those constants can be treated as adapter-ready. The TiXL mesh draw constant
buffer native packing proof now compiles a tiny Metal probe and proves b0
Transforms, b1 material Params, b2 FogParams, and b4 PbrParams readback at the
reserved adapter slots. The TiXL mesh draw PointLights/b5 packing verdict now
consumes that prior proof and proves b3 PointLights native packing with a
separate Metal compute readback for `PointLight[8]` stride, field offsets, and
`ActiveLightCount`. b5 duplicate Params remains blocked because the current
source audit/layout artifact has no concrete shadergraph param fields, so the
full constant-buffer adapter is still not ready. The TiXL mesh draw b5
ShaderGraph params expansion verdict now narrows that blocker: b5 is the
`/*{FLOAT_PARAMS}*/` hole in `mesh-Draw.hlsl`, filled by
`GenerateShaderGraphCode` through `ShaderGraphNode.CollectAllNodeParams` and
the `FloatParams` buffer. It now provides a source-backed SphereSDF fixture
manifest for `SphereSDF_nG1CBDm_Center` and `SphereSDF_nG1CBDm_Radius`, but
native b5 Metal packing had not yet been compiled or read back. The TiXL mesh
draw b5 native packing proof now consumes that source-backed expansion and
proves the exact 16-byte b5 buffer at Metal `buffer(7)` with a real compute
readback. That closes the b5 native packing lane only; it does not prove the
combined b0-b5 adapter, texture/sampler mapping, t8+ resources, or backend
replacement. The TiXL mesh draw texture/sampler binding proof now consumes the
source audit, the prior unbound resource ledger, and the handwritten explicit
MSL adapter strategy, then runs a real Metal compute sentinel readback proving
only `t2 BaseColorMap -> texture(2)`, `t7 BRDFLookup -> texture(7)`,
`s0 WrappedSampler -> sampler(0)`, and `s1 ClampedSampler -> sampler(1)`.
That closes the four-slot texture/sampler subset only; t3-t6 are resolved by
the later full PBR resource binding proof. The TiXL mesh draw
ShaderGraph resources expansion proof now proves that the `RESOURCES(t8)` hook
is real, that `GenerateShaderGraphCode` starts injected resources at t8, and
that the current SphereSDF fixture has zero t8+ resources because SphereSDF does
not override `AppendShaderResources` and the `IGraphNodeOp` default is empty.
It also source-validates that `GenerateShaderGraphCode.Resources` is appended
through `SetPixelAndVertexShaderStage.VariousResources` after ordinary shader
resources, but it does not prove real SRV creation or renderer integration.
The runtime closure report now reads that proof artifact directly before it
removes the old combined t8 gate; if the artifact is missing, stale, or widened,
the closure report fails instead of silently narrowing `requiredNext`.
The TiXL mesh draw stage/MRT/matrix proof now parses the donor HLSL, confirms
that `vsMain(uint id : SV_VertexID)` performs procedural indexed vertex
addressing, `psInput.pixelPosition` carries `SV_POSITION`, `psOutput.Color`
writes `SV_Target0`, `psOutput.Normal` writes `SV_Target1`, the fragment path
contains `ddx`/`ddy` and `discard`, and the source uses D3D `mul(vector,
matrix)` order. It then runs a tiny explicit Metal adapter probe that verifies
`[[vertex_id]]`, `[[stage_in]]`, two color attachments, and the selected
vector-matrix convention by exact readback. It consumes the current source
audit, t8, b5 native packing, and texture/sampler artifacts only to block stale
or widened upstream claims. It does not translate TiXL donor HLSL to MSL; the
later TextureCube, full PBR binding, explicit adapter, and native backend
integration proofs close their own bounded lanes. The closure report
now reads that artifact directly before removing
`prove_stage_mrt_matrix_semantics_for_handwritten_mesh_draw_adapter` from
`requiredNext`.
The TiXL mesh draw TextureCube SampleLevel / GetDimensions proof now maps only
that API pair into a tiny explicit Metal
`texturecube.sample(..., level(0.0))`,
`texturecube.sample(..., level(1.0))`, `get_width(0)`, `get_height(0)`,
`get_width(1)`, `get_height(1)`, and `get_num_mip_levels()` probe, with exact
4x4/2-mip dimensions plus separate mip-level RGBA8 sentinel readbacks.
It also establishes `boundedPbrVisualReferenceEstablished` by generating and
comparing a deterministic analytic sentinel before removing
`prove_texturecube_samplelevel_getdimensions_and_pbr_visual_reference` from
`requiredNext`, but it still leaves generic `pbrVisualCorrectness: false` and
`hlslToMslTranslation: false`.
The TiXL mesh draw Full PBR Resource Binding proof now consumes the b0-b5 native
packing, t0/t1 mesh buffers, t2-t7 PBR textures/cube resources, s0/s1 samplers,
and current empty t8+ ShaderGraph resource lane, then proves the complete
bounded resource binding contract for this Mesh Draw/PBR fixture.
The TiXL mesh draw Explicit Adapter proof now consumes that full binding and
proves the handwritten explicit MSL adapter contract for the bounded lane,
without selecting a generic HLSL-to-MSL translator.
The TiXL mesh draw Native Metal Backend Integration proof now consumes the
native render pipeline, backend gate prerequisites, full PBR binding, and
explicit adapter proof, then runs a real Metal render/readback backend sentinel
that proves native backend integration and runtime equivalence for this lane.
The TiXL mesh draw Backend Replacement Gate proof now consumes the current
native render pipeline, resource binding, full PBR binding, explicit adapter,
native Metal backend integration, texture/sampler binding, t8 ShaderGraph
resources, stage/MRT/matrix, and TextureCube/PBR reference artifacts. It
evaluates the replacement gate and proves the bounded replacement is ready:
`backendReplacementGateEvaluated: true`,
`replacementBlockedBecauseFullBindingMissing: false`,
`replacementBlockedBecauseAdapterProofMissing: false`,
`boundedNativeBackendRemains: false`, `backendReplacementReady: true`,
`fullPbrResourceBinding: true`, `explicitAdapterProofPresent: true`,
`nativeMetalBackendIntegrationComplete: true`, `runtimeEquivalenceProof: true`,
`hlslToMslTranslation: false`, `tixlRuntimeParity: true`, and
`nativeGpuParityComplete: true`. The closure report records
`native_metal_backend_replacement_ready` and
`bounded_native_gpu_tixl_parity_complete` in `proven`.
The next required work list is therefore empty for this bounded closure lane:

```text
[]
```

That empty list now means the bounded Mesh Draw/PBR native GPU/Metal/TiXL lane
has replacement-ready parity proof. It still does not claim generic
HLSL-to-MSL translation, repo-wide TiXL clone parity, or Vuo parity.

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
