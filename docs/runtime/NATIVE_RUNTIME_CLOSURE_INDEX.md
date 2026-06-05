# Native Runtime Closure Index

This is the short entrypoint for the bounded native product/runtime closure.
The master ledger remains the routing authority:

```text
docs/superpowers/plans/2026-06-05-native-runtime-master-progress.md
```

Use this file to jump from each spine to its proof, fixture, shell/runtime
entrypoint, artifacts, and node:test guard. It is an index, not a wider claim.

Creator-facing node admission is now gated by:

```text
docs/contracts/NODE_ADMISSION_LEVELS.md
docs/contracts/node_admission.schema.json
docs/contracts/node_manifests/*.json
docs/contracts/proof_manifests/*.json
docs/contracts/failure_taxonomy.json
docs/contracts/artifact_observability.schema.json
```

Proof prose is not the product contract by itself. A node without an admission
manifest is not admitted as a runtime/Vuo creator-facing node.

## Current Closure

- Status: bounded product/runtime body closed at 100/100.
- Verification command: `node --test tests/*.test.js`.
- Latest verified result: 820/820 pass.
- Boundary: broad generality remains a nonclaim. This is not final GUI parity,
  arbitrary shader language, full file import, complete node-library parity, or
  complete heap eviction/hazard tracking.

## Closure Map

| Spine | Proof | Fixture | Shell / Native Probe | Artifact Root | Test |
| --- | --- | --- | --- | --- | --- |
| Full runtime architecture harness | `docs/runtime/FULL_RUNTIME_ARCHITECTURE_PROOF.md` | `docs/runtime/fixtures/full_runtime_architecture.graph.json` | `docs/runtime/scripts/full_runtime_architecture_shell.py` | `docs/runtime/artifacts/full_runtime_architecture/` | `tests/full_runtime_architecture.test.js` |
| commandGraph mutation law | `docs/runtime/FULL_RUNTIME_ARCHITECTURE_PROOF.md`; `docs/runtime/NATIVE_CANVAS_INTERACTION_COMMAND_LOOP_PROOF.md`; `docs/runtime/NATIVE_IMPORTER_COMMAND_INGEST_PROOF.md`; `docs/runtime/NATIVE_AI_WORKER_AUTHORING_ASSIST_PROOF.md` | `docs/runtime/fixtures/full_runtime_architecture.graph.json`; `docs/runtime/fixtures/native_canvas_interaction_command_loop.graph.json`; `docs/runtime/fixtures/native_importer_command_ingest.graph.json`; `docs/runtime/fixtures/native_ai_worker_authoring_assist.graph.json` | `docs/runtime/scripts/full_runtime_architecture_shell.py`; `docs/runtime/scripts/native_canvas_interaction_command_loop_shell.py`; `docs/runtime/scripts/native_importer_command_ingest_shell.py`; `docs/runtime/scripts/native_ai_worker_authoring_assist_shell.py` | `docs/runtime/artifacts/full_runtime_architecture/`; `docs/runtime/artifacts/native_canvas_interaction_command_loop/`; `docs/runtime/artifacts/native_importer_command_ingest/`; `docs/runtime/artifacts/native_ai_worker_authoring_assist/` | `tests/full_runtime_architecture.test.js`; `tests/native_canvas_interaction_command_loop.test.js`; `tests/native_importer_command_ingest.test.js`; `tests/native_ai_worker_authoring_assist.test.js` |
| runtimeGraph builder and cook order | `docs/runtime/FULL_RUNTIME_ARCHITECTURE_PROOF.md`; `docs/runtime/NATIVE_RUNTIME_GRAPH_INCREMENTAL_BUILDER_PROOF.md` | `docs/runtime/fixtures/full_runtime_architecture.graph.json`; `docs/runtime/fixtures/native_runtime_graph_incremental_builder.graph.json` | `docs/runtime/scripts/full_runtime_architecture_shell.py`; `docs/runtime/scripts/native_runtime_graph_incremental_builder_shell.py` | `docs/runtime/artifacts/full_runtime_architecture/`; `docs/runtime/artifacts/native_runtime_graph_incremental_builder/` | `tests/full_runtime_architecture.test.js`; `tests/native_runtime_graph_incremental_builder.test.js` |
| FrameScheduler clock and dirty propagation | `docs/runtime/FULL_RUNTIME_ARCHITECTURE_PROOF.md`; `docs/runtime/NATIVE_FRAME_SCHEDULER_LIVE_DIRTY_PROOF.md` | `docs/runtime/fixtures/full_runtime_architecture.graph.json`; `docs/runtime/fixtures/native_frame_scheduler_live_dirty.graph.json` | `docs/runtime/scripts/full_runtime_architecture_shell.py`; `docs/runtime/scripts/native_frame_scheduler_live_dirty_shell.py` | `docs/runtime/artifacts/full_runtime_architecture/`; `docs/runtime/artifacts/native_frame_scheduler_live_dirty/` | `tests/full_runtime_architecture.test.js`; `tests/native_frame_scheduler_live_dirty.test.js` |
| 2D texture GPU patch runtime | `docs/runtime/NATIVE_GPU_PATCH_RUNTIME_SLICE_PROOF.md`; `docs/runtime/NATIVE_TEXTURE_PATCH_PRODUCT_RUNTIME_PROOF.md` | `docs/runtime/fixtures/native_gpu_patch_runtime_slice.graph.json`; `docs/runtime/fixtures/native_texture_patch_product_runtime.graph.json` | `docs/runtime/scripts/native_gpu_patch_runtime_slice_shell.py`; `docs/runtime/native/native_gpu_patch_runtime_slice_probe.mm`; `docs/runtime/scripts/native_texture_patch_product_runtime_shell.py`; `docs/runtime/native/native_texture_patch_product_runtime_probe.mm` | `docs/runtime/artifacts/native_gpu_patch_runtime_slice/`; `docs/runtime/artifacts/native_texture_patch_product_runtime/` | `tests/native_gpu_patch_runtime_slice.test.js`; `tests/native_texture_patch_product_runtime.test.js` |
| Shader IR / codegen / cache | `docs/runtime/NATIVE_SHADER_IR_CODEGEN_REGISTRY_PROOF.md`; `docs/runtime/NATIVE_SHADER_IR_EXPRESSION_CORE_PROOF.md` | `docs/runtime/fixtures/native_shader_ir_codegen_registry.graph.json`; `docs/runtime/fixtures/native_shader_ir_expression_core.graph.json` | `docs/runtime/scripts/native_shader_ir_codegen_registry_shell.py`; `docs/runtime/scripts/native_shader_ir_expression_core_shell.py`; `docs/runtime/native/native_shader_ir_expression_core_compile_probe.mm` | `docs/runtime/artifacts/native_shader_ir_codegen_registry/`; `docs/runtime/artifacts/native_shader_ir_expression_core/` | `tests/native_shader_ir_codegen_registry.test.js`; `tests/native_shader_ir_expression_core.test.js` |
| Resource allocator / lifetime / residency | `docs/runtime/NATIVE_RESOURCE_LIFETIME_POLICY_PROOF.md`; `docs/runtime/NATIVE_METAL_HEAP_RESIDENCY_PROOF.md` | `docs/runtime/fixtures/native_resource_lifetime_policy.graph.json`; `docs/runtime/fixtures/native_metal_heap_residency.graph.json` | `docs/runtime/scripts/native_resource_lifetime_policy_shell.py`; `docs/runtime/scripts/native_metal_heap_residency_shell.py`; `docs/runtime/native/native_metal_heap_residency_probe.mm` | `docs/runtime/artifacts/native_resource_lifetime_policy/`; `docs/runtime/artifacts/native_metal_heap_residency/` | `tests/native_resource_lifetime_policy.test.js`; `tests/native_metal_heap_residency.test.js` |
| Native app / canvas surface and workflow | `docs/runtime/NATIVE_PRODUCT_CANVAS_SURFACE_PROOF.md`; `docs/runtime/NATIVE_HUMAN_APP_WORKFLOW_PROOF.md`; `docs/runtime/NATIVE_CANVAS_INTERACTION_COMMAND_LOOP_PROOF.md` | `docs/runtime/fixtures/native_product_canvas_surface.graph.json`; `docs/runtime/fixtures/native_human_app_workflow.graph.json`; `docs/runtime/fixtures/native_canvas_interaction_command_loop.graph.json` | `docs/runtime/scripts/native_product_canvas_surface_shell.py`; `docs/runtime/native/native_product_canvas_surface_probe.mm`; `docs/runtime/scripts/native_human_app_workflow_shell.py`; `docs/runtime/native/native_human_app_workflow_probe.mm`; `docs/runtime/scripts/native_canvas_interaction_command_loop_shell.py` | `docs/runtime/artifacts/native_product_canvas_surface/`; `docs/runtime/artifacts/native_human_app_workflow/`; `docs/runtime/artifacts/native_canvas_interaction_command_loop/` | `tests/native_product_canvas_surface.test.js`; `tests/native_human_app_workflow.test.js`; `tests/native_canvas_interaction_command_loop.test.js` |
| Importer command ingest | `docs/runtime/NATIVE_IMPORTER_COMMAND_INGEST_PROOF.md` | `docs/runtime/fixtures/native_importer_command_ingest.graph.json` | `docs/runtime/scripts/native_importer_command_ingest_shell.py` | `docs/runtime/artifacts/native_importer_command_ingest/` | `tests/native_importer_command_ingest.test.js` |
| AI worker repair and authoring assist | `docs/runtime/NATIVE_AI_WORKER_LIVE_REPAIR_PROOF.md`; `docs/runtime/NATIVE_AI_WORKER_AUTHORING_ASSIST_PROOF.md` | `docs/runtime/fixtures/native_ai_worker_live_repair.graph.json`; `docs/runtime/fixtures/native_ai_worker_authoring_assist.graph.json` | `docs/runtime/scripts/native_ai_worker_live_repair_shell.py`; `docs/runtime/scripts/native_ai_worker_authoring_assist_shell.py` | `docs/runtime/artifacts/native_ai_worker_live_repair/`; `docs/runtime/artifacts/native_ai_worker_authoring_assist/` | `tests/native_ai_worker_live_repair.test.js`; `tests/native_ai_worker_authoring_assist.test.js` |

## Nonclaim Parking Lot

- Native app/canvas: not final visual polish or full interaction parity.
- Shader IR/codegen: not arbitrary user shader language, HLSL translation,
  loops, user-defined functions, or TiXL parity.
- Texture runtime: not a complete texture node library.
- Importer: not full file-format import, migration, or asset resolution.
- Resource residency: not a complete heap allocator, eviction system, or
  backend hazard tracker.
- AI worker: not broad natural-language patch authoring or arbitrary
  prompt-to-graph.
