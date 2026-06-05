# Native Canvas Interaction Command Loop Proof

NativeCanvasInteractionCommandLoopProof answers:

Can library, canvas, and inspector interactions all mutate the graph through the
same commandGraph path before runtime execution?

Acceptance line:

```text
library pick -> canvas place -> canvas connect -> inspector edit -> runtime frame
```

## Required Interaction Sources

- ui.library creates nodes through commandGraph.
- ui.canvas moves, selects, and connects nodes through commandGraph.
- ui.inspector edits params through commandGraph.
- runtime consumes the replayed editorGraph as runtimeGraph.

## Required Claims

- libraryMutationUsesCommandGraph: true
- canvasMutationUsesCommandGraph: true
- inspectorMutationUsesCommandGraph: true
- runtimeFrameLinked: true
- viewLocalGraphTruth: false

## Boundary

The native view hierarchy may own hit-test state, selection highlights, and
transient pointer events. Persistent node identity, params, edges, and positions
come from replayed commands. view-local graph truth is forbidden.

This is not final interaction parity, complete node editing, undo/redo, or
visual polish. It proves the first multi-region product interaction loop keeps
the graph mutation law intact.
