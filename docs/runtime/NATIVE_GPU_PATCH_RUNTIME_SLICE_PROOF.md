# Native GPU Patch Runtime Slice Proof

NativeGpuPatchRuntimeSliceProof answers:

```text
can a command-authored 2D texture patch be replayed into a runtimeGraph,
scheduled by our frame clock, assigned texture resources, compiled into
explicit MSL, and rendered through a real Metal readback?
```

This is the first own-runtime GPU patch slice. It is intentionally narrower
than the full native canvas: the graph is authored as commands in a fixture,
not through an app UI, and the shader generator is a bounded handwritten
template for ConstantImage, Blob, and BlendImages.

## Proven Claims

```text
commandGraphReplayed: true
runtimeGraphBuilt: true
frameSchedulerRan: true
resourceAllocatorRan: true
shaderCodegenCacheBuilt: true
actualCompilerRan: true
actualMetalRan: true
```

`actualCompilerRan` and `actualMetalRan` are true only when the local machine
can build and run the Metal probe. If Metal is unavailable, the lane must block
without publishing fake frame stats.

## Nonclaims

```text
nativeCanvasComplete: false
aiWorkerRepairLoop: false
genericShaderIrComplete: false
vuoParity: false
backendReplacementReady: false
fullTixlCloneParity: false
```

This proof does not replace Vuo as the current UI surface. It proves the first
runtime-owned GPU path that a later UI or AI worker can drive through the same
commandGraph law.

## First Proof

Fixture:

```text
docs/runtime/fixtures/native_gpu_patch_runtime_slice.graph.json
```

Runner:

```text
docs/runtime/scripts/native_gpu_patch_runtime_slice_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/native_gpu_patch_runtime_slice/native_gpu_patch_runtime_slice_result.json
docs/runtime/artifacts/native_gpu_patch_runtime_slice/native_gpu_patch_runtime_slice_trace.json
docs/runtime/artifacts/native_gpu_patch_runtime_slice/native_gpu_patch_runtime_slice_errors.json
docs/runtime/artifacts/native_gpu_patch_runtime_slice/runtime_graph.json
docs/runtime/artifacts/native_gpu_patch_runtime_slice/resource_ledger.json
docs/runtime/artifacts/native_gpu_patch_runtime_slice/shader_cache.json
docs/runtime/artifacts/native_gpu_patch_runtime_slice/generated_patch.metal
docs/runtime/artifacts/native_gpu_patch_runtime_slice/frame_stats.json
```
