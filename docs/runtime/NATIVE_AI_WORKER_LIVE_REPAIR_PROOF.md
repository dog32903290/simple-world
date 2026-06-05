# Native AI Worker Live Repair Proof

NativeAIWorkerLiveRepairProof answers:

Can the AI worker use live render artifacts as diagnostics, repair the graph
through commandGraph, and rerender evidence?

Acceptance line:

```text
graph -> render -> diagnostics -> repair command -> render
```

## Required Claims

- initialRenderRan: true
- diagnosticsReadFromRenderArtifact: true
- aiRepairUsedCommandGraph: true
- repairedRenderRan: true
- broadNaturalLanguageAuthoring: false

## Worker Boundary

The AI worker is not a chat sidecar. It is a bounded runtime worker that:

- reads render diagnostics;
- selects an allowed repair rule;
- appends a command to commandGraph;
- reruns the runtime proof;
- writes final evidence.

No direct graph JSON surgery is allowed. The repaired fixture may be written as
a replayable artifact, but its mutation must be represented as commands.

## Boundaries

- This is not broad natural-language patch authoring.
- This is not a general autonomous artist.
- This does not bypass UI/importer/AI commandGraph law.
