# Native Human App Workflow Proof

NativeHumanAppWorkflowProof answers:

Can the native runtime present a human-facing app workflow without letting skin
become graph truth?

Acceptance line:

```text
GraphContract -> native UI hierarchy -> commandGraph action -> runtime frame evidence
```

## Required UI Regions

- toolbar
- library
- canvas
- inspector
- diagnostics strip

## Required Claims

- nativeHumanFacingUi: true
- uiMutationUsesCommandGraph: true
- runtimeFrameLinked: true
- viewLocalGraphTruth: false

## Skin Boundary

The UI reads from graph, NodeSpec, NodeInstance, command log, runtime frame, and
diagnostics artifacts. Mutating controls dispatch commands. No inspector,
toolbar, canvas widget, or diagnostics strip may store persistent graph truth
outside commandGraph.

This is not marketing skin. This is not view-local graph truth. It is the first
human-facing native workflow surface attached to runtime evidence.

## Boundaries

- This is not full interaction parity with the final canvas.
- This is not complete undo/redo, save/load, timeline, or node graph editing.
- This does not replace future visual polish.
