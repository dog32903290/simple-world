# Native Draw Shader Compile Proof

NativeDrawShaderCompileProof answers:

```text
does the ShaderProgram package contain an explicit native draw shader source that this diagnostic lane may accept as compile-proof input?
```

Runtime path:

```text
shader_program_package.json requestedDrawShader -> NativeDrawShaderCompileProof -> compile result/trace/errors artifacts
```

## Boundary

This lane is a compile-only diagnostic proof shell.

It is not a renderer, not Metal parity, not TiXL parity, and not PBR visual correctness.
It does not draw pixels, inspect lighting, validate material appearance, or prove
that TiXL donor HLSL behaves the same in a native backend.

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

No Metal compiler command runs in this lane. A valid explicit native source is
therefore reported as `accepted_explicit_native_source`, which means package validation only.
It must not be reported as `compiled` until a real compiler command runs and its
stdout/stderr/status are captured in the artifacts.

Changing `requestedDrawShader.language`, package `status`, or
`compileParity` without `nativeSource` is still blocked as missing native
source.

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

Exit status is `0` only for `accepted_explicit_native_source`. Donor HLSL and
missing native source cases exit `1` because no native compile proof exists yet.
