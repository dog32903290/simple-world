# NativeRendererBackend Interface Contract

NativeRendererBackend answers:

```text
how does a packaged ShaderProgram enter the future native renderer without dragging in Vuo, JUCE UI, or raw shader strings?
```

This contract borrows the useful shape from my-world's `RenderBackend` boundary
and rewrites it as a simple_world runtime interface.

## Boundary

```text
ShaderProgram -> NativeRendererBackend -> CapturedFrame
```

This is not the Metal backend yet. It is the interface seam that a future Metal
backend must satisfy.

## Donor Evidence

The source donor is my-world:

```text
/Users/chenbaiwei/Projects/my-world/source/render/RenderBackend.h
/Users/chenbaiwei/Projects/my-world/source/render/ShaderPreviewInputBridge.h
```

The part worth carrying forward is the runtime boundary:

```text
compileShader
resize
renderFrame
captureFrame
release
backendStatus
last-valid-frame failure policy
```

The part we do not carry is the old app UI, workbench surfaces, or JUCE component
state.

## Interface Law

The backend interface must expose these operations as proof-visible artifacts:

```text
compileShader(shaderProgramPackage)
resize(width, height, scale)
renderFrame(frameInput)
captureFrame()
release()
backendStatus()
```

`compileShader` accepts a ShaderProgram package, not a raw source string. The
package must include source hash, language, stages, entry symbols, bindings, and
last-valid policy.

## Last Valid Law

Compile failure has one allowed live behavior:

```text
publish compile diagnostics
do not replace the live program
preserve last valid frame only when one exists
render no new frame when no valid program exists
```

This keeps shader editing live without letting a failed compile contaminate the
runtime state.

## Frame Input Law

The first interface proof carries only stable frame facts:

```text
timeSeconds
frameIndex
loudness
resolution
```

`loudness` is included because my-world already proved the useful path from
uniform evidence to render frame input. It is not a full audio analyzer port.

That bridge is defined separately:

```text
docs/runtime/SHADER_UNIFORM_BINDING_CONTRACT.md
docs/runtime/scripts/shader_uniform_binding_shell.py
docs/runtime/artifacts/shader_uniform_binding/
```

The first end-to-end headless connection is defined separately:

```text
docs/runtime/NATIVE_RENDER_PIPELINE_CONTRACT.md
docs/runtime/scripts/native_render_pipeline_shell.py
docs/runtime/artifacts/native_render_pipeline/
```

## First Proof

Fixture:

```text
docs/runtime/fixtures/native_renderer_backend_interface.graph.json
```

Runner:

```text
docs/runtime/scripts/native_renderer_backend_interface_shell.py
```

Artifacts:

```text
docs/runtime/artifacts/native_renderer_backend_interface/native_backend_interface.json
docs/runtime/artifacts/native_renderer_backend_interface/shader_compile_result.json
docs/runtime/artifacts/native_renderer_backend_interface/backend_status.json
docs/runtime/artifacts/native_renderer_backend_interface/render_frame_input.json
docs/runtime/artifacts/native_renderer_backend_interface/captured_frame_contract.json
docs/runtime/artifacts/native_renderer_backend_interface/native_renderer_backend_errors.json
```

The proof does not claim native GPU rendering. It proves the package and status
shape that native GPU rendering must obey.
