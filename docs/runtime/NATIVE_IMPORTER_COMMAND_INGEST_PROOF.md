# Native Importer Command Ingest Proof

NativeImporterCommandIngestProof answers:

Can an external patch document enter the native runtime without becoming graph
truth directly?

Acceptance line:

```text
external document -> import command stream -> replayed editorGraph -> runtimeGraph
```

## Required Claims

- importerMutationUsesCommandGraph: true
- externalDocumentStoredAsGraphTruth: false
- runtimeGraphBuiltFromReplay: true

## Boundary

The importer may parse external nodes, edges, params, and positions, but its
only mutation output is an ordered command stream. direct imported graph mutation is forbidden.
The runtimeGraph is built after command replay, not from the raw external
document.

This is not a full file-format importer, schema migration system, or robust
asset resolver. It is the bounded product/runtime proof that importer joins UI
and AI as a commandGraph-only mutation source.
