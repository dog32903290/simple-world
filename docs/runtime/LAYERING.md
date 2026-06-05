# Runtime Layering

This file is the routing map for runtime proof language. It does not add
visual nodes, does not claim renderer completion, and does not replace the
existing contracts it points to.

## Layer 1: TiXL-like node runtime contract

The TiXL-like node runtime contract layer defines creator-facing node meaning:
ports, types, defaults, ranges, event/data flow, state policy, color role,
failure language, backend degradation policy, and TiXL/Vuo parity level.

Primary entrypoints:

```text
docs/contracts/NODE_ADMISSION_LEVELS.md
docs/contracts/vuo_node_admission_index.json
docs/contracts/node_manifests/*.json
docs/runtime/MY_WORLD_RUNTIME_CONTRACT.md
docs/runtime/COMMAND_STREAM_CONTRACT.md
docs/runtime/RENDER_GRAPH_CONTRACT.md
docs/runtime/RESOURCE_LIFETIME_CONTRACT.md
docs/runtime/RENDERER_BACKEND_CONTRACT.md
```

This layer answers what a node or runtime contract means. It does not prove a
host can render it.

## Layer 2: proof / host adapter layer

The proof / host adapter layer makes a bounded contract visible or executable
inside a host. Vuo / JS / Python / WebGL are proof hosts only. They can apply
pressure to a node or render contract, but they are not the product renderer.

Current proof-host roles:

```text
Vuo := visible body-layer host for connectable node and canvas proofs
JS := node:test and small contract verifier host
Python := shell/proof artifact host
WebGL := shader compile pressure host
softwareProof := deterministic artifact backend for command-to-frame tests
```

NativeRendererBackend interface proof is not real Metal rendering. It proves
the package/lifecycle boundary that a future backend must satisfy.

A deterministic captured sample is not GPU readback. Treat it as proof-host
evidence unless the proof explicitly names a native Metal probe and readback
artifact.

TiXL mesh draw resource binding proof does not equal full PBR binding,
HLSL-to-MSL translation, TiXL runtime parity, or backend replacement.

## Layer 3: future native GPU backend

The future native GPU backend layer is where real backend execution belongs:
Metal device/queue/heap ownership, shader compilation, resource views,
command-stream execution, render targets, GPU readback, presentation, and
backend-specific diagnostics.

Only files that explicitly run native probes or declare native backend
interfaces may speak about this layer. Interface proofs remain interface proofs
until a real backend executes the frame.

## Current File Classification

| File or tree | Layer | Meaning |
| --- | --- | --- |
| `docs/runtime/scripts/render_graph_shell.py` | proof / host adapter layer | Python proof shell for RenderGraph pass-order and resource hazard artifacts. |
| `docs/runtime/scripts/resource_lifetime_shell.py` | proof / host adapter layer | Python proof shell for resource allocation, view invalidation, and lifetime artifacts. |
| `docs/runtime/scripts/native_resource_api.py` | TiXL-like node runtime contract | Runtime-shaped resource API model used by proofs; not a GPU backend. |
| `docs/runtime/scripts/native_render_pipeline_shell.py` | proof / host adapter layer | Python shell that connects existing contracts into pipeline artifacts; not renderer completion. |
| `docs/runtime/scripts/native_renderer_backend_interface_shell.py` | proof / host adapter layer | Interface proof for `NativeRendererBackend`; not real Metal rendering. |
| `docs/runtime/fixtures/*` | TiXL-like node runtime contract | Declarative contract inputs. Fixtures are requests/evidence seeds, not runtime completion by themselves. |
| `docs/runtime/artifacts/*` | proof / host adapter layer | Captured proof outputs. Artifacts are evidence for their named proof only. |

Native probes under `docs/runtime/native/` may cross into future native GPU
backend evidence only when their proof names the real Metal operation and
publishes diagnostics/readback artifacts.

## Nonclaim Guard

Do not promote language across layers:

```text
Vuo proof -> not native GPU renderer
Python shell -> not product runtime backend
WebGL compile probe -> not full renderer
NativeRendererBackend interface proof -> not real Metal rendering
deterministic captured sample -> not GPU readback
mesh draw resource binding -> not full PBR / HLSL-to-MSL / TiXL parity / backend replacement
```
