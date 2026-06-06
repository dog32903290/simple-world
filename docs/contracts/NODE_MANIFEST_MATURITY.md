# Node Manifest Maturity

Node maturity records the highest layer that may rely on a node contract today.
It is a promotion gate, not a promise about future runtime behavior.

## Levels

- `admissionReady`: safe to know and reject/defer.
- `interactionReady`: safe to create, draw, connect, edit, save.
- `runtimeReady`: safe to validate, dirty, schedule, build runtimeGraph.
- `nativeExecutable`: safe to execute through native RuntimeOp/backend adapter.

## Native Use

- `known-only`: the node may appear in admission records, but product code should
  reject, defer, or keep it as proof-only.
- `editor-graph`: the node may participate in editor graph operations such as
  creation, drawing, connection, editing, and save/load.
- `runtime-graph`: the node may participate in runtime validation, dirtying,
  scheduling, and runtimeGraph construction.
- `execute-native`: the node may execute through a native RuntimeOp/backend
  adapter with proof evidence.

## Evidence Rule

Use the narrowest checked-in evidence for the maturity claim:

- `proof-only` admission is a hard boundary: it stays `admissionReady` with
  `known-only` native use even when the node is high-risk.
- Prefer a full node manifest path when promoting to `runtimeReady`.
- Use a proof artifact or test path for `nativeExecutable`.
- Fall back to a direct test path or source path only for lower maturity levels.

For high-risk Vuo nodes, a full manifest is enough to claim `runtimeReady` only
in the narrow sense that the node may enter validation, dirty tracking,
scheduling, and runtimeGraph construction. It does not imply native backend
execution.

Do not fill detailed runtime behavior for a node before evidence exists. Record
unknowns instead.

Common unknowns:

- `full-manifest-missing`
- `runtime-promotion-not-proven`
- `native-executable-proof-missing`
