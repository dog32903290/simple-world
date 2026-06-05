# Native GPU Patch Runtime Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first own-runtime GPU patch slice: commandGraph -> runtimeGraph -> FrameScheduler -> Metal resource allocator -> ConstantImage/Blob/Blend -> Output frame.

**Architecture:** Follow the repo's proof shape: fixture.graph.json -> Python shell -> result/trace/errors artifacts -> node:test freshness. Keep this lane bounded: it proves a 2D texture patch runtime skeleton and real Metal execution for one patch, not a full native canvas, not Vuo parity, and not generic TiXL coverage.

**Tech Stack:** Node test runner, Python proof shell, JSON fixtures/artifacts, ObjC++/Metal probe.

---

## Boundary

This lane must prove:

- commandGraph accepts graph mutations as the source of truth.
- runtimeGraph is derived from commandGraph/editorGraph data and contains executable cook order.
- FrameScheduler owns `u_time`, `u_deltaTime`, and `u_frame`.
- Resource allocator publishes texture handles, views, lifetimes, and pass ownership.
- Shader layer publishes generated explicit MSL source and cache keys for ConstantImage, Blob, and BlendImages.
- Metal probe compiles/renders/readbacks a nonblack, varied frame from the generated patch source.

It must not claim:

- native app canvas is complete.
- AI worker repair loop is implemented.
- generic shader IR/codegen for all TiXL nodes.
- Vuo body-layer parity.
- full TiXL clone parity.

## Files

- Create: `docs/runtime/NATIVE_GPU_PATCH_RUNTIME_SLICE_PROOF.md`
- Create: `docs/runtime/fixtures/native_gpu_patch_runtime_slice.graph.json`
- Create: `docs/runtime/scripts/native_gpu_patch_runtime_slice_shell.py`
- Create: `docs/runtime/native/native_gpu_patch_runtime_slice_probe.mm`
- Create: `docs/runtime/artifacts/native_gpu_patch_runtime_slice/native_gpu_patch_runtime_slice_result.json`
- Create: `docs/runtime/artifacts/native_gpu_patch_runtime_slice/native_gpu_patch_runtime_slice_trace.json`
- Create: `docs/runtime/artifacts/native_gpu_patch_runtime_slice/native_gpu_patch_runtime_slice_errors.json`
- Create: `docs/runtime/artifacts/native_gpu_patch_runtime_slice/runtime_graph.json`
- Create: `docs/runtime/artifacts/native_gpu_patch_runtime_slice/resource_ledger.json`
- Create: `docs/runtime/artifacts/native_gpu_patch_runtime_slice/shader_cache.json`
- Create: `docs/runtime/artifacts/native_gpu_patch_runtime_slice/generated_patch.metal`
- Create: `docs/runtime/artifacts/native_gpu_patch_runtime_slice/frame_stats.json`
- Test: `tests/native_gpu_patch_runtime_slice.test.js`

## Tasks

### Task 1: Contract, Fixture, and RED Test

- [ ] Add the proof contract naming the bounded claims and nonclaims.
- [ ] Add a fixture with commands that create `constant_bg`, `blob_fg`, `blend_1`, and `output_1`, connect them, set parameters, and request two frames.
- [ ] Add a failing Node test that runs the shell and expects result/trace/errors/runtime/resource/shader/frame artifacts.
- [ ] Run `node --test tests/native_gpu_patch_runtime_slice.test.js` and confirm it fails because the shell/contract/artifacts are missing.

### Task 2: Runtime Skeleton Shell

- [ ] Implement command replay from fixture into an editorGraph.
- [ ] Build runtimeGraph with typed ports and cook order.
- [ ] Add FrameScheduler state for two frames.
- [ ] Add resource ledger entries for each generated texture output.
- [ ] Add shader cache entries and generated MSL source.
- [ ] Publish path-clean artifacts.
- [ ] Run the focused test and confirm the non-Metal structure assertions pass while Metal assertions fail until the probe is present.

### Task 3: Native Metal Patch Probe

- [ ] Add an ObjC++ probe that compiles generated MSL, renders an offscreen RGBA texture, and emits JSON stats.
- [ ] Wire the shell to build/run the probe with `xcrun clang++`.
- [ ] Report blocked status without fake success if Metal is unavailable.
- [ ] On success, assert nonblack and varied readback.

### Task 4: Verification and Ledger

- [ ] Run `node --test tests/native_gpu_patch_runtime_slice.test.js`.
- [ ] Run nearby existing tests: `tests/frame_scheduler_contract.test.js`, `tests/native_resource_api.test.js`, `tests/metal_explicit_msl_proof.test.js`.
- [ ] Publish checked-in artifacts by running the shell against the fixture.
- [ ] Confirm artifacts are path-clean and current.

## Acceptance

The slice is accepted only when:

- `claims.commandGraphReplayed`, `runtimeGraphBuilt`, `frameSchedulerRan`, `resourceAllocatorRan`, `shaderCodegenCacheBuilt`, and `actualMetalRan` are true on machines with Metal.
- On machines without Metal, the lane blocks honestly with no frame stats.
- `backendReplacementReady`, `vuoParity`, `genericShaderIrComplete`, `aiWorkerRepairLoop`, and `nativeCanvasComplete` remain false.
- `frame_stats.json` reports `nonBlack: true` and `varied: true`.
- `runtime_graph.json`, `resource_ledger.json`, and `shader_cache.json` contain no absolute user paths.
