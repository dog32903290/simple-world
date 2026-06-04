# RenderGraph Contract

RenderGraph answers:

```text
which passes run in this frame, in what order, and which resources do they read or write?
```

It sits between `FrameScheduler` and `RendererBackend`. Shader passes reference
validated `ShaderProgram` packages instead of raw shader strings.

## Boundary

```text
FrameScheduler -> RenderGraph -> RendererBackend
ShaderGraph -> ShaderProgram -> RenderGraph
```

`RenderGraph` does not choose Vuo, WebGL2, Metal, or software proof. It produces
an explicit pass plan that any capable backend can validate.

## Pass Law

Each pass declares:

```text
id
domain
reads
writes
commands
clearPolicy
dependsOn
publish
shaderProgram
```

Pass order must be deterministic and derived from declared dependencies. A pass
must not read a resource before a previous pass writes it, unless the resource
is declared external.

## Resource Access Law

RenderGraph uses the same access names as the command-stream proof:

```text
RenderTargetWrite
DepthStencilWrite
ShaderResourceRead
UAVWrite
UnorderedAccessWrite
FrameOutputRead
```

When a resource moves from write access to read access, the plan emits a
`resourceBarrier` trace. This is still a contract proof, not a real GPU barrier.

## Resource Lifetime Law

RenderGraph declares resource use, but ResourceLifetime owns allocation,
reuse, reallocation, disposal, and view invalidation:

```text
docs/runtime/RESOURCE_LIFETIME_CONTRACT.md
docs/runtime/scripts/resource_lifetime_shell.py
docs/runtime/artifacts/resource_lifetime/
```

## First Proof

Fixture:

```text
docs/runtime/fixtures/render_graph_passes.graph.json
```

Runner:

```text
docs/runtime/scripts/render_graph_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/render_graph/render_graph_trace.json
docs/runtime/artifacts/render_graph/render_pass_plan.json
docs/runtime/artifacts/render_graph/resource_access_ledger.json
docs/runtime/artifacts/render_graph/render_graph_errors.json
```

This is the pass-level skeleton under `RendererBackend`. It proves pass ordering
and hazard visibility before we choose a real Metal/WebGL/DX backend.
