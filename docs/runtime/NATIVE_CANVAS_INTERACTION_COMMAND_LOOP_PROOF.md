# Native Canvas Interaction Command Loop Proof

NativeCanvasInteractionCommandLoopProof answers:

Can library, canvas, and inspector interactions all mutate the graph through the
same commandGraph path before runtime execution?

The proof shell consumes the C++ graph command dispatcher through:

```text
docs/runtime/scripts/cpp_graph_command_contract_shell.py
```

It does not replay a separate proof-only command model.

Acceptance line:

```text
library pick -> canvas place -> canvas connect -> inspector edit -> runtime frame
```

## Required Interaction Sources

- ui.library creates nodes through commandGraph.
- ui.canvas moves, selects, and connects nodes through commandGraph.
- ui.inspector edits params through commandGraph.
- runtime consumes the exported GraphDocument as runtimeGraph.

## Required Claims

- libraryMutationUsesCommandGraph: true
- canvasMutationUsesCommandGraph: true
- inspectorMutationUsesCommandGraph: true
- runtimeFrameLinked: true
- viewLocalGraphTruth: false
- sharedGraphStateInteractionCommands: true
- cppCommandDispatcher: true

## Boundary

The native view hierarchy may own hit-test state, selection highlights, and
transient pointer events. Persistent node identity, params, edges, and positions
come from replayed GraphState interaction commands. The runtime consumes the
exported `GraphDocument` / `RuntimeGraph`; view-local graph truth is forbidden.

This is not final interaction parity, complete node editing, undo/redo, or
visual polish. It proves the first multi-region product interaction loop keeps
the graph mutation law intact.
