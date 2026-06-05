# Native RuntimeGraph Incremental Builder Proof

NativeRuntimeGraphIncrementalBuilderProof answers:

Can a live commandGraph topology edit rebuild runtimeGraph without throwing away
unaffected executable nodes?

Acceptance line:

```text
command replay -> runtimeGraph diff -> executable node reuse -> cook order artifact
```

## Required Claims

- builtFromCommandReplay: true
- incrementalRebuild: true
- unaffectedExecutableReused: true
- cookOrderRecomputed: true

## Boundary

runtimeGraph is built from commandGraph replay. Live topology edits enter as
commands, never as direct runtimeGraph patches. The builder computes structural
hashes from executable node type, params, and upstream dependencies; unchanged
hashes may reuse executable state, while added or downstream-affected nodes are
rebuilt.

This is not a general optimizer, scheduler replacement, full compiler pipeline,
or cross-frame cache eviction policy. It is the product runtime proof that
runtimeGraph can respond to live commandGraph edits without rebuilding the whole
graph blindly.
