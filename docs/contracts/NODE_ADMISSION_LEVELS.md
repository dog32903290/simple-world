# Node Admission Levels

This file defines the creator-facing admission gate for nodes entering
`runtime`, `vuo`, or `proof-only` lanes. Prose proof documents may explain
context, but the machine-readable admission manifest is the gate.

No manifest means no creator-facing admission. Existing Vuo nodes, proof-only
adapters, generated batches, or runtime fixtures without a manifest are source
evidence only; they are not admitted runtime/Vuo product nodes until a
`docs/contracts/node_manifests/*.json` file declares their level, parity,
backend degradation, failure language, event flow, observability context, and
proof evidence.

## Admission

- `runtime`: admitted to native runtime execution. Requires ports, params,
  state, flow ownership, backend policy, failure codes, observability context,
  and a fresh proof manifest.
- `vuo`: admitted to Vuo-facing prototype or body-layer work. Requires the same
  contract fields plus explicit Vuo/TiXL parity levels.
- `proof-only`: allowed as bounded evidence or adapter pressure. Must not appear
  as a creator-facing runtime node without a new admission manifest.
- `blocked`: known node name or behavior that must not enter product/runtime
  until the missing contract or proof is supplied.

## Parity Levels

- `semantic-parity`: behavior matches the named source contract for admitted
  inputs, outputs, state, failures, and roundtrip.
- `visual-proof`: visual output is proven for bounded fixtures, but source
  runtime semantics are not fully claimed.
- `body-layer-adapter`: the node body is represented through a host-native or
  Vuo-compatible adapter with explicit nonclaims.
- `host-layer-proof`: host behavior such as windows, render targets, or event
  ticks is proven without claiming source backend parity.
- `not-parity`: no parity claim.

## Artifact Context

Every admitted node must make black-frame or stale-proof triage possible by
carrying this context in diagnostics or proof manifests:

```text
graphId, frameId, commandId, nodeId, backendId, artifactPath, diagnosticCode
```

If a node cannot provide this context, it may remain `proof-only` but cannot be
promoted to creator-facing runtime admission.

## Contract Entrypoints

- `node_admission.schema.json`: creator-facing node admission schema.
- `proof_manifest.schema.json`: machine-readable claim/nonclaim proof schema.
- `artifact_observability.schema.json`: shared diagnostic envelope for command,
  runtimeGraph, scheduler, resource, shader, backend, renderer, AI worker,
  importer, and canvas artifacts.
- `failure_taxonomy.json`: global failure codes and handling classes.
- `node_manifests/*.json`: admitted node contracts.
- `proof_manifests/*.json`: machine-readable proof claim manifests.
