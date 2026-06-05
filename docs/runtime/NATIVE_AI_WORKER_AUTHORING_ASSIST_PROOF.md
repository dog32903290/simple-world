# Native AI Worker Authoring Assist Proof

NativeAIWorkerAuthoringAssistProof answers:

Can the AI worker turn a bounded product intent into a validated commandGraph
plan, build a runtime artifact, read diagnostics, and repair the authored patch
without direct graph mutation?

Acceptance line:

```text
bounded intent -> AI command plan -> commandGraph validation -> runtime artifact -> diagnostics
```

## Required Claims

- boundedIntentAuthoring: true
- commandPlanValidated: true
- commandGraphOnlyMutation: true
- runtimeArtifactRendered: true
- diagnosticFeedbackObserved: true
- directGraphMutationRejected: true
- broadNaturalLanguageAuthoring: false

## Worker Boundary

The AI worker is a bounded authoring assistant, not graph truth.

- It reads a structured intent contract.
- It proposes only allowed commandGraph commands.
- It validates the command plan before replay.
- It replays commands into editorGraph state.
- It builds runtimeGraph and frame artifacts from replayed command state.
- It reads diagnostics and may append repair commands.

AI proposal is not graph truth. direct editorGraph mutation is rejected before
any graph mutation happens. Unknown command ops are rejected before replay.

## Boundaries

- This is not broad natural-language patch authoring.
- This is not arbitrary prompt-to-graph.
- This is not a general autonomous artist.
- This does not bypass UI/importer/AI commandGraph law.
- This reuses the current bounded texture node vocabulary; it does not expand
  shader language, importer coverage, or final canvas interaction parity.
