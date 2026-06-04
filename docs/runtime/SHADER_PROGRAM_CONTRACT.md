# ShaderProgram Contract

ShaderProgram answers:

```text
which generated shader source, stages, entry symbols, and bindings may a renderer backend compile for this pass?
```

It sits between `ShaderGraph` and `RenderGraph`.

## Boundary

```text
ShaderGraph -> ShaderProgram -> RenderGraph -> RendererBackend
```

`ShaderGraph` owns visual node vocabulary and source assembly. `ShaderProgram`
owns the concrete package that a backend can validate: language, stage entry
symbols, binding layout, source hash, compile target, and failure policy.

`ShaderProgram` does not prove PBR correctness, Metal parity, DX11 parity, or a
finished renderer. It prevents a generated shader string from becoming an
uninspectable black box.

## Program Law

Each program declares:

```text
programId
sourceGraph
sourceArtifact
sourceHash
language
compileBackend
stages
entrySymbols
bindings
lastValidPolicy
diagnostics
```

The source hash is part of the contract. If live controls regenerate shader
source, the program identity changes. If live controls only update uniforms, the
program identity stays stable and the binding values change.

## Binding Law

Bindings are declared before backend compile:

```text
uniforms
samplers
textures
storageBuffers
constantBuffers
```

The first proof has no texture or sampler bindings. That is intentional:
`my_SphereSDF -> my_RaymarchField` proves field shader assembly first, before
TextureView and CommandStream parity are claimed.

## Failure Law

Compile or package failure must publish diagnostics and keep the live program
honest:

```text
publish compile errors
do not replace the live program with an invalid program
keep last valid program when one exists
output no program when no previous valid program exists
```

Downstream render passes may not pretend that a missing shader stage or entry
symbol is valid.

## First Proof

Fixture:

```text
docs/runtime/fixtures/shader_program_contract.graph.json
```

Runner:

```text
docs/runtime/scripts/shader_program_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/shader_program/shader_source.glsl
docs/runtime/artifacts/shader_program/shader_program_package.json
docs/runtime/artifacts/shader_program/shader_program_bindings.json
docs/runtime/artifacts/shader_program/shader_program_compile_request.json
docs/runtime/artifacts/shader_program/shader_program_last_valid_policy.json
docs/runtime/artifacts/shader_program/shader_program_errors.json
```

The existing WebGL2 probe still performs the actual GLSL pressure compile. This
contract packages the same shader source so the renderer layer can know what it
is accepting.
