# Native Texture Patch Product Runtime Proof

NativeTexturePatchProductRuntimeProof answers:

Can the native 2D texture runtime move beyond the bounded
ConstantImage/Blob/BlendImages slice and execute a stateful product texture
patch through Metal?

Acceptance line:

```text
Gradient -> Feedback -> RenderTarget
```

## Node Contracts

Node: Gradient
Question: what color field is generated directly from UV?
Conversion: uv -> texture
Family: texture source
Failure: invalid colors fall back to black/white diagnostic defaults.

Node: Feedback
Question: what previous-frame texture state is blended into this frame?
Conversion: texture + previous texture -> texture state
Family: feedback control
Failure: missing history starts from a clear texture and emits diagnostics.

Node: RenderTarget
Question: which texture becomes the runtime frame artifact?
Conversion: texture -> render target texture
Family: routing
Failure: missing input keeps last valid render target when available.

## Required Claims

- commandGraphReplayed: true
- runtimeGraphBuilt: true
- gradientMetalPass: true
- feedbackMetalPass: true
- renderTargetMetalPass: true
- actualMetalRan: true
- completeTextureRuntime: false
- completeShaderLanguage: false

## Boundaries

- This is not a complete texture runtime.
- This is not a complete shader language.
- This does not replace future node admission for filters, loaders, post FX,
  multipass materials, or arbitrary shader graphs.
