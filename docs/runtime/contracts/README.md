# Runtime Contract Entrypoints

This README routes runtime contract work.

This README does not claim renderer completion, does not add visual nodes, and
does not replace the underlying contracts.

## Start Here

- `docs/runtime/LAYERING.md`: separates TiXL-like node runtime contract,
  proof / host adapter layer, and future native GPU backend.
- `docs/runtime/CONTRACT_GAPS.md`: bug triage rule and contract-gap workflow.
- `docs/contracts/NODE_ADMISSION_LEVELS.md`: creator-facing node admission gate
  for runtime, Vuo, proof-only, and blocked nodes.

## Runtime Contract Families

- `MY_WORLD_RUNTIME_CONTRACT.md`: node vocabulary and host-proof boundary.
- `COMMAND_STREAM_CONTRACT.md`: command law and ordered render operations.
- `RENDER_GRAPH_CONTRACT.md`: pass order, reads/writes, and hazard visibility.
- `RESOURCE_LIFETIME_CONTRACT.md`: allocation, view invalidation, and lifetime.
- `RENDERER_BACKEND_CONTRACT.md`: backend capability truth.
- `NATIVE_RENDERER_BACKEND_INTERFACE.md`: future native backend interface proof.

## Proof Boundary

Vuo, JS, Python, WebGL, and deterministic software artifacts are proof hosts.
They may prove a bounded contract but do not make the native renderer complete.

`NATIVE_RENDERER_BACKEND_INTERFACE.md` and
`docs/runtime/scripts/native_renderer_backend_interface_shell.py` prove an
interface/lifecycle boundary. They are not real Metal rendering.

If a bug report asks for behavior outside these boundaries, classify it in
`CONTRACT_GAPS.md` before changing implementation.
