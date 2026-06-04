# Native Draw Shader Compile Proof

NativeDrawShaderCompileProof answers:

```text
does the ShaderProgram package contain an explicit native draw shader source that this diagnostic lane may accept as compile-proof input?
```

Runtime path:

```text
shader_program_package.json requestedDrawShader -> NativeDrawShaderCompileProof -> compile result/trace/errors artifacts
requestedDrawShader.nativeSource(MSL) -> NativeDrawShaderCompileProof -> MetalExplicitMslProof
```

## Boundary

This shell has two deliberately narrow paths.

The donor HLSL path is a compile diagnostic only. It does not invoke Metal and
does not draw pixels. It does not inspect lighting and must remain blocked
without an explicit native source.

The explicit native MSL path delegates to `MetalExplicitMslProof` for 8x8 compile/render/readback evidence.
That proves only that the supplied explicit MSL source compiled and rendered
through the proof harness.

This shell is not renderer or backend/pipeline integration, not Metal parity for
donor shaders, not TiXL parity, and not PBR visual correctness.

The current `requestedDrawShader` in the ShaderProgram package is donor
metadata:

```text
source: Lib:shaders/3d/mesh/mesh-Draw.hlsl
language: HLSL_TIXL_DONOR
vertexShaderEntry: vsMain
pixelShaderEntry: psMain
```

That donor package must fail this proof with
`native_draw_shader_compile.native_source_missing`. The result must preserve
`sourceLanguage`, `sourceDerivedFrom`, and `donorSource` so the blocked point is
visible in downstream diagnostics.

## Native Source Law

The only success path in this shell is an explicit
`requestedDrawShader.nativeSource` object:

```json
{
  "language": "MSL",
  "vertexEntry": "vertexMain",
  "fragmentEntry": "fragmentMain",
  "sourceText": "..."
}
```

An explicit MSL native source is copied into a temporary
`MetalExplicitMslProof` fixture and compiled/rendered by
`docs/runtime/scripts/metal_explicit_msl_proof_shell.py` with an 8x8 viewport
unless the native source or fixture specifies another viewport.

If that proof succeeds, this shell reports
`compiled_explicit_msl_with_metal_proof`, sets `actualCompilerRan`,
`actualMetalRan`, and `explicitMslMetalProof` true, and includes a `metalProof`
summary with status, width, height, `nonBlack`, and `varied`.

`nativeCompileParity` is true only for the explicit native MSL source that Metal
compiled in this proof. It does not claim TiXL/HLSL translation, renderer
integration, Metal parity of a donor shader, or PBR visual correctness.

If the Metal proof fails or blocks, this shell exits `1`, reports
`native_draw_shader_compile.metal_proof_failed`, and nests the Metal proof
errors for the caller. A missing Metal device may surface as
`blocked_metal_device_unavailable`; it is not a fake success.

If the Metal proof command/toolchain is unavailable or the child proof emits no
result artifact, this shell exits `1` with
`metal_explicit_msl_proof_unavailable` and
`native_draw_shader_compile.metal_proof_unavailable`. That unavailable branch is
structured and must not pass through traceback-shaped raw diagnostics.

Changing `requestedDrawShader.language`, package `status`, or
`compileParity` without `nativeSource` is still blocked as missing native
source.

## Closure Handoff

RuntimeClosureReport may still close the current `native_render_pipeline` lane
as `proven_with_bounded_native_backend` while this default proof is blocked.
The explicit MSL Metal proof exists, but it only proves supplied native MSL; it
does not discharge the TiXL donor HLSL boundary. The remaining closure work is
to provide explicit MSL for the TiXL draw shader, or prove/reject a real
HLSL-to-MSL translation lane for mesh draw shaders, before replacing the
bounded backend interface with native compile proof.

## First Proof

Fixture:

```text
docs/runtime/fixtures/native_draw_shader_compile_proof.graph.json
```

Runner:

```text
docs/runtime/scripts/native_draw_shader_compile_proof_shell.py <native_draw_shader_compile_proof.graph.json> <out_dir>
```

Artifacts:

```text
docs/runtime/artifacts/native_draw_shader_compile_proof/native_draw_shader_compile_result.json
docs/runtime/artifacts/native_draw_shader_compile_proof/native_draw_shader_compile_trace.json
docs/runtime/artifacts/native_draw_shader_compile_proof/native_draw_shader_compile_errors.json
```

Exit status is `0` only for `compiled_explicit_msl_with_metal_proof`. Donor HLSL,
missing native source cases, naive language/status flips, and failed Metal proof
cases exit `1`.
