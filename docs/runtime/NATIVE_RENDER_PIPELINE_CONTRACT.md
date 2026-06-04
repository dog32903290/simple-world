# Native Render Pipeline Contract

NativeRenderPipeline answers:

```text
can one frame move from RenderGraph pass planning through draw-command evidence, live uniform evidence, ShaderProgram packaging, and into the native backend interface?
```

It is the first full headless connection of the my-world donor lines inside
simple_world.

## Boundary

```text
RenderGraph -> ResourceLifetime -> CommandStream -> ShaderUniformBinding -> ShaderProgram -> NativeRendererBackend -> CapturedFrame
```

This contract does not replace the smaller contracts. It only proves that their
artifacts can be wired together without hidden UI state.

## Pipeline Law

The pipeline runner must call each layer through its existing shell:

```text
render_graph_shell.py
resource_lifetime_shell.py
command_stream_pipeline_shell.py
shader_uniform_binding_shell.py
shader_program_shell.py
native_renderer_backend_interface_shell.py
```

It must not reimplement shader assembly, uniform evaluation, or backend status.
It only validates artifact compatibility and passes `RenderFrameInput` forward.
ResourceLifetime must receive RenderGraph's `resource_access_ledger.json`; the
main pipeline may not silently allocate render targets from a separate hand-made
resource fixture.

## Compatibility Law

Before rendering the frame, the pipeline checks:

```text
ResourceLifetime.status == ok
ResourceLifetime live Texture2D count > 0
RenderGraph.status == ok
RenderGraph pass plan exposes the CommandStream pass color write
CommandStream.ok == true
CommandStream.drawCalls > 0
uniform targetProgramId == shader programId
RenderFrameInput.sourceSnapshotOk == true
ShaderProgram.status == ok
NativeRendererBackend.importsOldUi == false
CapturedFrame.nonBlackSample == true
```

If any layer fails, the pipeline fails with diagnostics. It must not fabricate a
captured frame.

## First Proof

Fixture:

```text
docs/runtime/fixtures/native_render_pipeline.graph.json
```

Runner:

```text
docs/runtime/scripts/native_render_pipeline_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/native_render_pipeline/pipeline_summary.json
docs/runtime/artifacts/native_render_pipeline/pipeline_trace.json
docs/runtime/artifacts/native_render_pipeline/render_pass_plan.json
docs/runtime/artifacts/native_render_pipeline/resource_access_ledger.json
docs/runtime/artifacts/native_render_pipeline/resource_registry.json
docs/runtime/artifacts/native_render_pipeline/view_invalidation_ledger.json
docs/runtime/artifacts/native_render_pipeline/command_stream_summary.json
docs/runtime/artifacts/native_render_pipeline/command_stream_result.json
docs/runtime/artifacts/native_render_pipeline/render_frame_input.json
docs/runtime/artifacts/native_render_pipeline/captured_frame_contract.json
docs/runtime/artifacts/native_render_pipeline/native_render_pipeline_errors.json
```

This proof is still headless and deterministic. The next step after this is to
replace the deterministic interface capture with a real native GPU backend.
