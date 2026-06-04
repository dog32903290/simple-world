# Metal Explicit MSL Proof

MetalExplicitMslProof answers:

```text
explicit MSL source -> real Metal compile -> offscreen render -> RGBA readback stats
```

Runtime path:

```text
metal_explicit_msl_proof.graph.json -> metal_explicit_msl_proof_shell.py -> ObjC++ Metal probe -> result/trace/errors/frame stats artifacts
```

## Boundary

This is a Mac-first proof lane for explicit MSL only.

It is not renderer integration, not NativeDrawShaderCompileProof integration,
not TiXL/HLSL translation, and not PBR parity. It does not claim that donor TiXL
HLSL or material behavior matches the native renderer.

The only success path is explicit MSL source that defines:

```text
vertex my_world_vertex
fragment my_world_fragment
```

The shell must report `actualCompilerRan: true` only after the probe asks Metal
to compile the MSL library. It must report `actualMetalRan: true` only after the
probe renders into an offscreen RGBA8Unorm texture and reads back RGBA bytes.

## First Proof

Fixture:

```text
docs/runtime/fixtures/metal_explicit_msl_proof.graph.json
```

Runner:

```text
docs/runtime/scripts/metal_explicit_msl_proof_shell.py <fixture> <out_dir>
```

Probe source:

```text
docs/runtime/native/metal_explicit_msl_probe.mm
```

Artifacts:

```text
docs/runtime/artifacts/metal_explicit_msl_proof/metal_explicit_msl_result.json
docs/runtime/artifacts/metal_explicit_msl_proof/metal_explicit_msl_trace.json
docs/runtime/artifacts/metal_explicit_msl_proof/metal_explicit_msl_errors.json
docs/runtime/artifacts/metal_explicit_msl_proof/frame_stats.json
```

If no Metal device is available, the shell exits `1` with
`blocked_metal_device_unavailable` and must not write a fake frame artifact.
Invalid MSL exits `1` with a compiler diagnostic and no frame success.
